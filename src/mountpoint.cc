// Implementations
#include <mountpoint.h>
#include <stringutil.h>
#include <mutex_locker.h>
#include <regular_expression.h>
#include <evlbidebug.h>
#include <directory_helper_templates.h>
#include <threadutil.h>
#include <dosyscall.h>
#include <libvbs.h>

#include <iostream>
#include <set>
#include <map>
#include <list>
#include <algorithm>
#include <iterator>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <ftw.h>
#include <fnmatch.h>
#include <signal.h>

// For checking existance of directory(ies)
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>


using namespace std;

DEFINE_EZEXCEPT(mountpoint_exception)


///////////////////////////////////////////////////////////////////
//
//           First up: a lot of support code
//
///////////////////////////////////////////////////////////////////

mountpointinfo_type::mountpointinfo_type():
    f_size( 0 ), f_free( 0 )
{}

mountpointinfo_type::mountpointinfo_type(uint64_t s, uint64_t f):
    f_size( s ), f_free( f )
{}

///////////////////////////////////////////////////////////////////
//   Capture information on which path in the file system
//   hierarchy is mounted on which physical device
///////////////////////////////////////////////////////////////////
sysmountpoint_type::sysmountpoint_type(string const& p, string const& dev):
    path( p ), device( dev )
{}

///////////////////////////////////////////////
//   Wrapper around ::pthread_create(3) -
//   creates a joinable thread with
//   ALL signals blocked.
//   Takes same arguments as ::pthread_create(3)
//   apart from the pthread_attr_t struct
///////////////////////////////////////////////
int mp_pthread_create(pthread_t* thread, void *(*start_routine)(void*), void *arg) {
    int             pr, createrv;
    sigset_t        oldSig, newSig;
    pthread_attr_t  attr;

    // Make sure we have a joinable thread
    if( (pr=::pthread_attr_init(&attr))!=0 ) {
        DEBUG(-1, "mp_pthread_create: pthread_attr_init fails - " << evlbi5a::strerror(pr) << endl);
        return pr;
    }
    if( (pr=::pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))!=0 ) {
        DEBUG(-1, "mp_pthread_create: pthread_attr_setdetachstate fails - " << evlbi5a::strerror(pr) << endl);
        return pr;
    }
    // Install a fully filled signal set (i.e. block all of 'm) and save the
    // current one
    if( sigfillset(&newSig)!=0 ) {
        DEBUG(-1, "mp_pthread_create: sigfillset fails - " << evlbi5a::strerror(errno) << endl);
        return errno;
    }
    if( (pr=::pthread_sigmask(SIG_SETMASK, &newSig, &oldSig))!=0 ) {
        DEBUG(-1, "mp_pthread_create: pthread_sigmask (setting new mask) fails - " << evlbi5a::strerror(pr) << endl);
        return pr;
    }
    // Now we're in a determined state and we can safely create the 
    // thread. We save the return value of this one specifically because
    // that will be the eventual return value. We do the cleanup calls
    // and if they'll fail we inform the user that but do not return *those*
    // error value(s)
    createrv = ::pthread_create(thread, &attr, start_routine, arg);
    if( createrv!=0 )
        DEBUG(-1, "mp_pthread_create: pthread_create fails - " << evlbi5a::strerror(createrv) << endl);

    // Cleanup phase: put back old signal mask & destroy pthread attributes
    if( (pr=::pthread_sigmask(SIG_SETMASK, &oldSig, 0))!=0 )
        DEBUG(-1, "mp_pthread_create: pthread_sigmask (put back old mask) fails - " << evlbi5a::strerror(pr) << endl);
    if( (pr=::pthread_attr_destroy(&attr))!=0 )
        DEBUG(-1, "mp_pthread_create: pthread_attr_destroy fails - " << evlbi5a::strerror(pr) << endl);

    // Phew. Finally done.
    return createrv;
}

// We must be able to transform a list of patterns (strings) into regexes
// such that we may match entries to them


// Keep a list of regex pointers
// HV: no, we just keep a list of patterns
// HV/BE: no, we keep a list of matchables, which might be either regex
//        or shell globbing!
struct matchable_type {
    virtual bool    matches( const string& s ) const = 0;
    virtual string  pattern( void ) const = 0;
    virtual ~matchable_type() {};
};

// shell globbing uses ::fnmatch(3)
struct shellglobbing_type: public matchable_type {
    shellglobbing_type(const string& glob):
        __m_glob_pattern(glob)
    { }

    virtual bool matches( const string& s ) const {
        return ::fnmatch(__m_glob_pattern.c_str(), s.c_str(), FNM_PATHNAME)==0;
    }
    virtual string pattern( void ) const {
        return "shell:"+__m_glob_pattern;
    }

    ~shellglobbing_type() {}

    const string    __m_glob_pattern;
};

// regex matchable, see regex(3)
struct regexglob_type: public matchable_type {

    regexglob_type(const string& re):
        __m_regex_pattern(re)
    { }

    virtual bool matches( const string& s ) const {
        return __m_regex_pattern.matches(s);
    }
    virtual string pattern( void ) const {
        return __m_regex_pattern.pattern();
    }

    ~regexglob_type() {}

    const Regular_Expression   __m_regex_pattern;
};

struct nullmatching_type: public matchable_type {
    virtual bool matches( const string& ) const {
        // this one should never be called
        throw std::logic_error("null mountpoint matcher should *never* be called. Mail+yell verkouter@jive.eu");
    }
    virtual string pattern( void ) const {
        return "nullmatching_type";
    }
};

typedef list<matchable_type*>        regexlist_type;

// We must call delete on all elements in the regexlist_type
// Using this struct we can easily do a for_each() in the destructor of the
// object which holds a regexlist_type
struct delete_matchable {
    void operator()(matchable_type* p) const {
        delete p;
    }
};

// Analyze a list of patterns and come up with:
//   1.) the set of unique top-level directories that we
//       need to start searching for
//   2.) the maximum search depth per start point
//
// In fact, we can return a map of <path> to [<pattern>, ...] (+ maximum
// search depth)!
// MAP: string => LIST(pattern)
//
// Rationale:
//   Input: list of patterns, e.g.:
//          /mnt/disk[0-7], /mnt/vsn/{12,13,14}, ^/path/to/[0-9]+$
//          1               2                    3
//
//   it can be seen that patterns 1 and 2 both can start their search from
//   the "/mnt" path. Pattern 3 should start searching from "/path/to".
//
//   Also, pattern 3 is a regex pattern, 1 and 2 are shell globbing
//
// The reason for (pre) optimizing this is that we do not want [n]ftw(3) to
// grovel over ALL files/directories in the file system - we want to
// restrict its search as much as we can.
struct mpsettings_type {
    // Simple c'tor + d'tor
    mpsettings_type() :
        maxdepth(0)
    {}

    ~mpsettings_type() { 
        for_each(regexes.begin(), regexes.end(), delete_matchable());
    }

    unsigned int    maxdepth;
    regexlist_type  regexes;

    private:
        // can't do assignment + copy, because of the regexlist_type data
        // member
        //mpsettings_type(const mpsettings_type&);
        mpsettings_type& operator=(const mpsettings_type&);
};
typedef map<string, mpsettings_type> mpmap_type;


struct anal_result {
    string          prefix;
    unsigned int    depth;
    matchable_type* pattern;

    anal_result(const string& pfx, matchable_type* patternptr, unsigned int d) :
        prefix(pfx), depth(d), pattern(patternptr)
    { EZASSERT2(patternptr!=0, mountpoint_exception, EZINFO("Null-pointer for pattern is not allowed")); }

    private:
        // no default c'tor
        anal_result();
};


#if 0
// In-place modification of strings ...
struct subber {
    subber(const string& p, const string& r, bool all=false):
        pattern(p), replacement(r), replace_all(all)
    {}

    string& operator()(string& s) {
        string::size_type  ptr = s.find(pattern, 0);
        
        while( ptr!=string::npos ) {
            s.replace(ptr, pattern.size(), replacement);

            // skip over the replacement
            ptr += replacement.size();

            // depending on wether to replace first or all instances of
            // pattern ...
            ptr  = replace_all ? s.find(pattern, ptr+1) : string::npos;
        }
        return s;
    }
    const string    pattern;
    const string    replacement;
    const bool      replace_all;
};
struct sanitizer {
    string operator()(const string& part) const {
        if( part=="*" )
            return ".+";
        string  cpy(part);
        return subber("?", ".", true)(subber("*", ".*", true)(cpy));
    }
};
struct sanitizer {
    string operator()(const string& part) const {
        return part;
    }
};

#endif

// return the number of pieces (==maxdepth) and the "prefix" - the
// leading part of the path that does not contain regex patterns as well as
// the sanitized pattern:
//   - multiple consecutive slashes are removed
//     (taken care of by the ::split(..., true) call
//   - replace '*' by '.*'. An entry consisting of just "*" will be replaced
//     by ".+" because that is the intent. This implements the difference
//     between:
//          /tmp/foo/*         and     /tmp/foo*
//     the intent of the first is to match all entries under /tmp/foo/
//     (requiring at least one character, hence ".+") whereas the latter
//     would match /tmp/foo, /tmp/foo1, /tmp/foobar, should those entries
//     exist.
//   - '?' will be replaced by '.'
// these steps are implemented by the sanitize functor above.
anal_result analyze_pattern(const patternlist_type::value_type& pattern) {
    // We only support absolute paths! (Note: also in regex format)
    EZASSERT2(::isValidPattern(pattern), mountpoint_exception,
              EZINFO("Invalid path '" << pattern << "': only absolute paths supported"));
    // Support for the null pattern
    if( pattern==noMountpoint )
        return anal_result(noMountpoint, new nullmatching_type(), 0);

    // split the pattern into pieces at '/' boundaries
    vector<string>                 pieces = ::split(pattern, '/', true);
    vector<string>::iterator       start;
    vector<string>::iterator       lastWithoutPattern;
    // If any of these characters appear in a path part, we assume it
    // (potentially) is a regex ...
    static const Regular_Expression rxPattern("[][\\*\\?\\{\\}\\(\\)\\.]");
    // If the actual pattern matches this, we interpret it as regex,
    // otherwise as shell glob
    static const Regular_Expression rxRegex("^\\^.*\\$$");

    // If the pattern started with "^/", account for that
    start = pieces.begin();
    if( pieces.size()>0 && pieces[0]=="^" )
        start++;

    // Start looking which piece looks like it contains a regex
    for(lastWithoutPattern=start; lastWithoutPattern!=pieces.end(); lastWithoutPattern++)
        if( rxPattern.matches(*lastWithoutPattern) )
            break;
    // Form the path of all leading pieces that are not regexes
    string  pfx("/");
    for(vector<string>::iterator p=start; p!=lastWithoutPattern; p++, (p!=lastWithoutPattern?pfx+="/":pfx))
        pfx += *p;

    //cout << "analyze_pattern: prefix=" << pfx << "  pattern=" << pattern << endl;
    matchable_type*     matcher = 0;
    const unsigned int  depth = (unsigned int)pieces.size() - (unsigned int)distance(pieces.begin(), lastWithoutPattern);

    if( rxRegex.matches(pattern) )
        matcher = new regexglob_type(pattern);
    else
        matcher = new shellglobbing_type(pattern);
    return anal_result(pfx, matcher, depth);
}


mpmap_type analyze_patterns(const patternlist_type& patterns) {
    // iterate over all the patterns
    mpmap_type                       rv;
    patternlist_type::const_iterator p;

    for(p=patterns.begin(); p!=patterns.end(); p++) {
        const anal_result      res = analyze_pattern(*p);
        mpmap_type::iterator   mptr;

        if( (mptr=rv.find(res.prefix))==rv.end() ) {
            // this prefix did not exist yet in the result set
            pair<mpmap_type::iterator,bool> insres;

            insres = rv.insert( make_pair(res.prefix, mpsettings_type()) );
            EZASSERT2(insres.second==true, mountpoint_exception,
                      EZINFO("Failed to insert entry in MAP mountpoint => LIST(regex) for " << res.prefix));
            mptr = insres.first;
        }

        // mptr points to a mpsettings_type, update the record
        mptr->second.regexes.push_back( res.pattern );
        mptr->second.maxdepth = max(res.depth, mptr->second.maxdepth);
    }
    return rv;
}



// Let's process a mpmap_type entry:
//  .first  = key   = the root path
//  .second = value = contains maxdepth & list of regexes to match

// For [n]ftw we need global info :-(
namespace mp_ftw {
    static pthread_mutex_t   mpLock = PTHREAD_MUTEX_INITIALIZER;

    // Here we store the actual 'global' stuff for [n]ftw. Only copy data
    // here after you've succesfully locked ...
    static unsigned int          maxDepth;
    static mountpointlist_type*  mountpointSet = 0;
    static regexlist_type const* regexList = 0;
}

#define KEES(a) case (a): return string(#a); 
string flag2str(int ftwf) {
    switch(ftwf) {
        KEES(FTW_D) 
        KEES(FTW_F) 
        KEES(FTW_DNR) 
        KEES(FTW_DP) 
        KEES(FTW_NS) 
        KEES(FTW_SL) 
        KEES(FTW_SLN)
        default:
            return string("(unknown)");
    }
}

int match_dirname(const char* path, const struct stat* , int flag, struct FTW* ftwp) {
    // Only interested in directories
    if( flag!=FTW_D )
        return 0;

    // We don't want to throw here - that would upset the calling 'nftw()'
    // that we're inside of!
    if( mp_ftw::regexList==0 || mp_ftw::mountpointSet==0 ) {
        cerr << "match_dirname: regexList or mounpointset (or both) are NULL" << endl;
        return 1;
    }

    // Match each path to all regexes and if they match - add the path to
    // the set!
    for( regexlist_type::const_iterator reptrptr=mp_ftw::regexList->begin(); reptrptr!=mp_ftw::regexList->end(); reptrptr++ )
        if( (*reptrptr)->matches(path) )
            mp_ftw::mountpointSet->insert(string(path));

    // Stop processing if we've gone as deep as we must Make use of newer
    // new ftw [fancier nftw(3), which in itself was an improvement on
    // ftw(3) ...] features, like being able to skip going deeper into the
    // hierarchy. When stuck with not-so-fancy nftw(3), one actually has to
    // grovel over the WHOLE tree below the starting path given to nftw(3)
    // ... OUCH.
    if( (unsigned int)(ftwp->level)>=mp_ftw::maxDepth )
#ifdef FTW_ACTIONRETVAL
        return FTW_SKIP_SUBTREE;
#else
        return 0;
#endif

#if 0
    cout << "[" << path << "]:";
    for( regexlist_type::const_iterator reptr=mp_ftw::regexList->begin(); reptr!=mp_ftw::regexList->end(); reptr++ ) {
        cout << "  " << *reptr << "=";
        if( ::fnmatch(reptr->c_str(), path, FNM_PATHNAME)==0 ) {
            mp_ftw::mountpointSet->insert(string(path));
            cout << "1";
        } else {
            cout << "0";
        }
    }
    cout << endl;
#endif
    return 0;
}

// Functor for directory-helper-template which checks if
// a given file name matches "/path/to/SCAN/SCAN.[0-9]{8}"
// Note: be suffix aware!
struct isScanChunk {

    // Handle "_ds<suffix>" in isScanChunk
    // if scan name already contains "_ds" we assume user only looking for a
    // specific suffix. Otherwise we add all suffixes.
    isScanChunk(const string& scan):
        __m_regex( (scan.find("_ds")==string::npos) ?
                        string("^.*/(")+scan+"(_ds[^_\\.]+)?)/\\1\\.[0-9]{8}$" :
                        string("^.*/(")+scan+")/\\1\\.[0-9]{8}$" )
    {}

    bool operator()(const string& path) const {
        //DEBUG(4, "isScanChunk(" << path << ") - re:" << __m_regex.matches(path) << " file:" << isRegularFile(path) << endl);
        return __m_regex.matches(path) && isRegularFile(path);
    }

    bool isRegularFile(const string& path) const {
        struct stat  st;
        if( ::stat(path.c_str(), &st)!=0 ) {
            DEBUG(-1, "isScanChunkPredicate: stat(" << path << ") - " << evlbi5a::strerror(errno) << endl);
            return false;
        }
        return (st.st_mode & S_IFREG)==S_IFREG;
    }
    const Regular_Expression  __m_regex;
};

// For multithreaded chunk finders - we must pass a couple of arguments to
// each thread:
//     * the directory it's supposed to scan
//     * an ouputiterator; we will use this one to append our findings to
//       whatever others have found
//     * pointer to mutex for locking said accumulator
template <typename OutputIter>
struct scanChunkFinderArgs {

    scanChunkFinderArgs(const string& mountpoint, const string& scan, OutputIter acc, pthread_mutex_t* mtx):
        __m_accumulator(acc), __m_path(mountpoint), __m_scan(scan), __m_mutex(mtx), __m_predicate(scan)
    {}

    OutputIter         __m_accumulator;
    const string       __m_path;
    const string       __m_scan;
    pthread_mutex_t*   __m_mutex;
    const isScanChunk  __m_predicate;
};

// predicate to match directory names against a regex including "_ds<suffix>"
// in case datastreams were active
struct isRecording {
    isRecording(string const& recname):
        // If the recording name included "_dsXXX" we assume one is looking
        // for that data stream explictly. Otherwise we'll accumulate all
        // data streams for that recording
        __m_regex( string("^")+escape(recname)+(recname.find("_ds")==string::npos ? "(_ds[^_\\.]+)?" : "")+"$" )
    {}

    bool operator()(string const& entry ) const {
        DEBUG(5, "isRecording: checking entry " << entry << " against " << __m_regex.pattern() << endl);
        return __m_regex.matches(entry);
    }

    Regular_Expression  __m_regex;

    private:
        isRecording();
        isRecording( isRecording const& );
};

// The thread function will delete the storage for the
// scanChunkFinderArgs
template <typename OutputIter>
void* scanChunkFinder(void* args) {
    scanChunkFinderArgs<OutputIter>* scfa = (scanChunkFinderArgs<OutputIter>*)args;

    // we execute inside a thread - don't let exceptions escape
    try {
        DIR*             dirp;
        struct stat      dirstat;
        isRecording      predicate( scfa->__m_scan );
        direntries_type  dirs;

        // Now that we support datastreams, we must potentially filter
        // many recordings
        //
        // 02 Jun 2022: commit 2c741f2e6e18471bcaa79c674ee1459fa0c14468 (after v3.0.0
        // tagging) introduced a different way of testing/iterating over
        // directory entries, using ::opendir(...)
        //
        // It kept on using the old ASSERT2_COND( ...==0 ) macro, effectively
        // requesting ::opendir() to return a NULL pointer. If the call is
        // succesfull (which it should be), the assert would fire ..., i.e.
        // always. Fixed by requiring ::opendir() to return a non-NULL
        // result, which works a lot better!
        ASSERT2_NZERO( dirp=::opendir(scfa->__m_path.c_str()),
                       SCINFO(" failed to open mountpoint directory " << scfa->__m_path) );

        dirs = dir_filter(dirp, predicate);
        ::closedir( dirp );

        // Now loop over the matched recording names and collect all those
        for(direntries_type::const_iterator p=dirs.begin(); p!=dirs.end(); p++) {
            string const dir( scfa->__m_path + "/" + *p );

            if( ::lstat(dir.c_str(), &dirstat)<0 ) {
                if( errno!=ENOENT )
                    DEBUG(2, "scanChunkFinder(scan=" << scfa->__m_scan << ", path=" << scfa->__m_path << ")/::lstat() fails on " << *p << " - " << evlbi5a::strerror(errno) << endl);
                continue;
            }

            // OK, we got the status. If it's not a directory ...
            if( !S_ISDIR(dirstat.st_mode) )
                continue;

            // Filter the directory entries
            direntries_type  de = dir_filter(dir, scfa->__m_predicate);

            ::pthread_mutex_lock(scfa->__m_mutex);
            std::copy(de.begin(), de.end(), scfa->__m_accumulator);
            ::pthread_mutex_unlock(scfa->__m_mutex);
        }
    }
    catch( const exception& ex ) {
        DEBUG(-1, "scanChunkFinder[" << scfa->__m_path << "]: " << ex.what() << endl);
    }
    catch( int e ) {
        DEBUG(-1, "scanChunkFinder[" << scfa->__m_path << "]: caught errno=" << e << " - " << evlbi5a::strerror(e) << endl);
    }
    catch( ... ) {
        DEBUG(-1, "scanChunkFinder[" << scfa->__m_path << "]: caught unknown exception" << endl);
    }
    // Ok, delete the storage
    delete scfa;
    return (void*)0;
}


///////////////////////////////////////////////////////////////////
//
//           These are the actual user functions
//
///////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////
//           Find mountpoints
///////////////////////////////////////////////////////////////////
mountpointlist_type find_mountpoints(const string& pattern) {
    return find_mountpoints(patternlist_type(&pattern, &pattern+1));
}

mountpointlist_type find_mountpoints(const patternlist_type& patterns) {
    mpmap_type          mountpoints = analyze_patterns(patterns);
    mountpointlist_type mps;

    DEBUG(5, "find_mountpoints: entry with " << patterns.size() << " entries" << std::endl);

    // Loop over all detected "start points" - the leading parts of patterns
    // not containing globbing expressions.
    for(mpmap_type::const_iterator p=mountpoints.begin(); p!=mountpoints.end(); p++) {
        // Special handling for the no mountpoint mountpoint
        if( p->first == noMountpoint ) {
            mps.insert( p->first );
            continue;
        }
        // Grab lock on the FTW globals
        mutex_locker    lck( mp_ftw::mpLock );

        // Set up the external info for the NFTW call
        mp_ftw::maxDepth      = p->second.maxdepth;
        mp_ftw::regexList     = &p->second.regexes;
        mp_ftw::mountpointSet = &mps;

        DEBUG(5, "   running nftw(" << p->first << "): maxDepth=" << mp_ftw::maxDepth << std::endl);

        // Ok, safe to call nftw now
        int nftw_flags = 0;
#ifdef FTW_ACTIONRETVAL
        nftw_flags |= FTW_ACTIONRETVAL;
#endif
        ::nftw(p->first.c_str(), &match_dirname, 256, nftw_flags);

        // Put back known bad values
        mp_ftw::maxDepth      = 0;
        mp_ftw::regexList     = 0;
        mp_ftw::mountpointSet = 0;
    }

    // mps is a set of existing directories that match the user's pattern(s).
    // Now it's time to wield out the ones that physically exist on the root
    // file system: e.g. directories created on the root file system where
    // external disks _could_ be mounted - e.g. the Mark6 modules.
    // 
    // For the Mark6, the directories /mnt/disk/[1-4]/[0-7] always exist but
    // wether or not they refer to mounted disk(s) depends on wether or not
    // the module is activated and mounted.
    //
    // In order to protect the system, we should NOT stripe data into
    // directories on the root file system.
    mountpointlist_type                    nonroot;
    insert_iterator<mountpointlist_type>   appender(nonroot, nonroot.begin());
    const sysmountpointlist_type           sysmountpoints = find_sysmountpoints();
    sysmountpointlist_type::const_iterator rootDevice     = sysmountpoints.end();

    // Step 1.) Find the root device
    for(sysmountpointlist_type::const_iterator curmp=sysmountpoints.begin();
        curmp!=sysmountpoints.end() && rootDevice==sysmountpoints.end();
        curmp++)
            if( curmp->path=="/" )
                rootDevice = curmp;
           
    DEBUG(4, "Found root device: path=" << rootDevice->path << ", device=" << rootDevice->device << endl); 
    EZASSERT2(rootDevice!=sysmountpoints.end(), mountpoint_exception, EZINFO(" - your system does not have a root file system?!"));

    // Step 2.) Go through all selected directories, find the longest prefix
    //          to find out on which device it lives. Filter out the ones
    //          that live on the root device
    for(mountpointlist_type::const_iterator mp=mps.begin(); mp!=mps.end(); mp++) {
        sysmountpointlist_type::const_iterator pfx = rootDevice;

        // Find the longest prefix
        for(sysmountpointlist_type::const_iterator smp=sysmountpoints.begin(); smp!=sysmountpoints.end(); smp++)
            if( mp->compare(0, smp->path.size(), smp->path)==0 && /* current sysmount 'smp' is prefix of path 'mp' */
                (mp->size()>smp->path.size() ? (mp->at(smp->path.size())=='/') : true) && /* is it a full _directory_ prefix,
                                                                                          not just arbitrary string prefix? */
                smp->path.size()>pfx->path.size() /* and it is a *longer* prefix */)
                pfx = smp;
        // If the pfx points at the rootDevice, don't copy the current
        // mountpoint to the output set
        const bool notSelected( pfx==rootDevice && *mp!=noMountpoint );
        DEBUG(4, "find_mountpoints: " << (notSelected?("not "):("")) << "selecting " << *mp <<
                 ", it is on path=" << pfx->path << ", device=" << pfx->device << endl);
        if( notSelected )
            continue;
        *appender++ = *mp;
    }
    return nonroot;
}

// Tests if the mountpoint list is literally just ["null"]
bool is_null_diskset(const mountpointlist_type& mpl) {
    return mpl.size()==1 && *mpl.begin()==noMountpoint;
}


///////////////////////////////////////////////////////////////////
//           Find scan chunks
//  Chunks for a specific scan always look like:
//       /..../SCAN/SCAN.xxxxxxxx
//  with xxxxxxxx an eight-digit sequence number
//  TODO FIXME XXX
//  if no "_ds<suffix>" found in scan name, find all data streams
///////////////////////////////////////////////////////////////////

typedef std::list<pthread_t*>               threadlist_type;
typedef back_insert_iterator<filelist_type> appender_type;

filelist_type find_recordingchunks(const string& scan, const mountpointlist_type& mountpoints) {
    int                 create_error;
    filelist_type       rv;
    appender_type       appender(rv);
    threadlist_type     threads;
    pthread_mutex_t     mtx = PTHREAD_MUTEX_INITIALIZER;

    // For each mountpoint, start a thread
    // We remember the pthread_create() return value of the last created
    // thread; if we did not succeed in creating all threads, it will be
    // non-zero
    create_error = 0;
    for(mountpointlist_type::const_iterator mp=mountpoints.begin(); mp!=mountpoints.end(); mp++) {
        pthread_t*                          tidptr = new pthread_t;
        scanChunkFinderArgs<appender_type>* scfa = new scanChunkFinderArgs<appender_type>(*mp, scan, appender, &mtx);

        if( (create_error=mp_pthread_create(tidptr, &scanChunkFinder<appender_type>, scfa))!=0 ) {
            DEBUG(-1, "find_recordingchunks: failed to create thread [" << *mp << "] - " << evlbi5a::strerror(create_error) << endl);
            delete tidptr;
            delete scfa;
            break;
        }
        threads.push_back( tidptr );
    }
    // Wait for completion of threads that have succesfully started
    for(threadlist_type::iterator tidptrptr=threads.begin(); tidptrptr!=threads.end(); tidptrptr++) {
        ::pthread_join( **tidptrptr, 0 );
        delete *tidptrptr;
    }

    // If we did not create a thread for all mountpoints - give up
    EZASSERT2(create_error==0, mountpoint_exception, EZINFO("Failed to create a thread - " << evlbi5a::strerror(errno)));
    return rv;
}



///////////////////////////////////////////////////////////////////////////
//
//      Two functions
//      * Get total/free space on a single mountpoint 
//      * tally up the total free space & used across all mountpoints
//
///////////////////////////////////////////////////////////////////////////
mountpointinfo_type statmountpoint(string const& mp) {
    uint64_t        bs;
    struct statvfs  stat;

    EZASSERT2( ::statvfs((mp+"/.").c_str(), &stat)==0, mountpoint_exception,
               EZINFO("Failed to stat " << mp) );

    bs = stat.f_frsize;
    DEBUG(4, "statmountpoint: " << mp << " frsize=" << stat.f_frsize << " #total=" << stat.f_blocks <<
              " #avail=" << stat.f_bavail << " (f_bsize=" << stat.f_bsize << " f_bfree=" << stat.f_bfree << ")" << endl);
    return mountpointinfo_type(stat.f_blocks * bs, stat.f_bavail * bs);

}
mountpointinfo_type statmountpoints(mountpointlist_type const& mps) {
    mountpointinfo_type rv;

    for(mountpointlist_type::const_iterator p=mps.begin(); p!=mps.end(); p++) {
        mountpointinfo_type mpi( statmountpoint(*p) );
        rv.f_size += mpi.f_size;
        rv.f_free += mpi.f_free;
    }
    return rv;
}


///////////////////////////////////////////////////////////////////
//
//       Retrieve, in an O/S specific manner, the list
//       of mountpoints and which physical devices they are
//
///////////////////////////////////////////////////////////////////

// Not all API's provide "_r" reentrant functions so we'll just use a mutex to DIY
#include <mutex_locker.h>
static pthread_mutex_t  fsstat_lock = PTHREAD_MUTEX_INITIALIZER;

#if defined(__APPLE__)
    // Under Mac OSX we use getfsstat(2)
    #include <sys/param.h>
    #include <sys/mount.h>

    sysmountpointlist_type find_sysmountpoints( void ) {
        mutex_locker           scopedLock( fsstat_lock );
        const int              nmp = ::getfsstat(0, 0, MNT_NOWAIT);
        struct statfs*         fs  = new struct statfs[nmp];
        sysmountpointlist_type mps;

        ASSERT2_COND(::getfsstat(fs, nmp*sizeof(struct statfs), MNT_NOWAIT)!=-1, delete [] fs);
        for(int i=0; i<nmp; i++)
            mps.push_back( sysmountpoint_type(fs[i].f_mntonname, fs[i].f_mntfromname) );
        // clean up
        delete [] fs;
        return mps;
    }

#elif defined(__OpenBSD__) || defined(__FreeBSD__)
    // On OpenBSD we use getfsent(3) and friends
    #include <fstab.h>

    sysmountpointlist_type find_sysmountpoints( void ) {
        mutex_locker           scopedLock( fsstat_lock );
        struct fstab*          fs;
        sysmountpointlist_type mps;

        ASSERT2_COND(::setfsent()==1, SCINFO(" failed to open fstab file?"));
        while( (fs=::getfsent())!=0 )
            mps.push_back( sysmountpoint_type(fs->fs_file, fs->fs_spec) );
        ::endfsent();
        // clean up
        return mps;
    }

#else // ! __APPLE__ && ! __OpenBSD__
    // Everywhere else we use getmntent(3)
    #include <stdio.h>
    #include <stdlib.h>
    #include <mntent.h>

    sysmountpointlist_type find_sysmountpoints( void ) {
        mutex_locker           scopedLock( fsstat_lock );
        FILE*                  mtab;
        struct mntent*         mnt;
        sysmountpointlist_type mps;

        // Open mtab file
        ASSERT_NZERO( (mtab=::setmntent("/etc/mtab", "r")) );

        // Read all entries
        while( (mnt=::getmntent(mtab))!=0 )
            mps.push_back( sysmountpoint_type(mnt->mnt_dir, mnt->mnt_fsname) );

        ::endmntent( mtab );
        return mps;
    }

#endif

// internal function
bool isValidPattern(const string& pattern) {
    static const Regular_Expression  valid_pattern( "^((\\^/.*\\$)|/.*)$" );
    //cout << "isValidPattern(" << pattern << "): " << valid_pattern.matches(pattern) << endl;
    return valid_pattern.matches(pattern) || pattern==noMountpoint;
}

