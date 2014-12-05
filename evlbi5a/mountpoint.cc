#include <mountpoint.h>
#include <stringutil.h>
#include <mutex_locker.h>
#include <regular_expression.h>
#include <evlbidebug.h>
#include <directory_helper_templates.h>

#include <iostream>
#include <set>
#include <map>
#include <list>
#include <algorithm>
#include <iterator>
#include <memory>

#include <pthread.h>
#include <ftw.h>
#include <fnmatch.h>
#include <signal.h>


using namespace std;

DEFINE_EZEXCEPT(mountpoint_exception)

///////////////////////////////////////////////////////////////////
//
//           First up: a lot of support code
//
///////////////////////////////////////////////////////////////////


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
        DEBUG(-1, "mp_pthread_create: pthread_attr_init fails - " << ::strerror(pr) << endl);
        return pr;
    }
    if( (pr=::pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))!=0 ) {
        DEBUG(-1, "mp_pthread_create: pthread_attr_setdetachstate fails - " << ::strerror(pr) << endl);
        return pr;
    }
    // Install a fully filled signal set (i.e. block all of 'm) and save the
    // current one
    if( sigfillset(&newSig)!=0 ) {
        DEBUG(-1, "mp_pthread_create: sigfillset fails - " << ::strerror(errno) << endl);
        return errno;
    }
    if( (pr=::pthread_sigmask(SIG_SETMASK, &newSig, &oldSig))!=0 ) {
        DEBUG(-1, "mp_pthread_create: pthread_sigmask (setting new mask) fails - " << ::strerror(pr) << endl);
        return pr;
    }
    // Now we're in a determined state and we can safely create the 
    // thread. We save the return value of this one specifically because
    // that will be the eventual return value. We do the cleanup calls
    // and if they'll fail we inform the user that but do not return *those*
    // error value(s)
    createrv = ::pthread_create(thread, &attr, start_routine, arg);
    if( createrv!=0 )
        DEBUG(-1, "mp_pthread_create: pthread_create fails - " << ::strerror(createrv) << endl);

    // Cleanup phase: put back old signal mask & destroy pthread attributes
    if( (pr=::pthread_sigmask(SIG_SETMASK, &oldSig, 0))!=0 )
        DEBUG(-1, "mp_pthread_create: pthread_sigmask (put back old mask) fails - " << ::strerror(pr) << endl);
    if( (pr=::pthread_attr_destroy(&attr))!=0 )
        DEBUG(-1, "mp_pthread_create: pthread_attr_destroy fails - " << ::strerror(pr) << endl);

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

    ~regexglob_type() {}

    const Regular_Expression   __m_regex_pattern;
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
    // Match each path to all regexes and if they match - add the path to
    // the set!

    // Stop processing if we've gone as deep as we must Make use of newer
    // new ftw [fancier nftw(3), which in itself was an improvement on
    // ftw(3) ...] features, like being able to skip going deeper into the
    // hierarchy. When stuck with not-so-fancy nftw(3), one actually has to
    // grovel over the WHOLE tree below the starting path given to nftw(3)
    // ... OUCH.

    if( (unsigned int)(ftwp->level)>mp_ftw::maxDepth )
#ifdef FTW_ACTIONRETVAL
        return FTW_SKIP_SUBTREE;
#else
        return 0;
#endif

    if( flag!=FTW_D )
        return 0;

    // We don't want to throw here - that would upset the calling 'nftw()'
    // that we're inside of!
    if( mp_ftw::regexList==0 || mp_ftw::mountpointSet==0 ) {
        cerr << "match_dirname: regexList or mounpointset (or both) are NULL" << endl;
        return 1;
    }

    // Loop over all matchables
    for( regexlist_type::const_iterator reptrptr=mp_ftw::regexList->begin(); reptrptr!=mp_ftw::regexList->end(); reptrptr++ )
        if( (*reptrptr)->matches(path) )
            mp_ftw::mountpointSet->insert(string(path));
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
struct isScanChunk {
    isScanChunk(const string& scan):
        __m_regex(string("^.*/")+scan+"/"+scan+"\\.[0-9]{8}$")
    {}

    bool operator()(const string& path) const {
        //DEBUG(4, "isScanChunk(" << path << ") - re:" << __m_regex.matches(path) << " file:" << isRegularFile(path) << endl);
        return __m_regex.matches(path) && isRegularFile(path);
    }

    bool isRegularFile(const string& path) const {
        struct stat  st;
        if( ::stat(path.c_str(), &st)!=0 ) {
            DEBUG(-1, "isScanChunkPredicate: stat(" << path << ") - " << ::strerror(errno) << endl);
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
        __m_accumulator(acc), __m_path(mountpoint+"/"+scan), __m_mutex(mtx), __m_predicate(scan)
    {}

    OutputIter         __m_accumulator;
    const string       __m_path;
    pthread_mutex_t*   __m_mutex;
    const isScanChunk  __m_predicate;
};

// The thread function will delete the storage for the
// scanChunkFinderArgs
template <typename OutputIter>
void* scanChunkFinder(void* args) {
    scanChunkFinderArgs<OutputIter>* scfa = (scanChunkFinderArgs<OutputIter>*)args;

    // we execute inside a thread - don't let exceptions escape
    try {
        // Filter the directory entries
        direntries_type  de = dir_filter(scfa->__m_path, scfa->__m_predicate);

        ::pthread_mutex_lock(scfa->__m_mutex);
        std::copy(de.begin(), de.end(), scfa->__m_accumulator);
        ::pthread_mutex_unlock(scfa->__m_mutex);
    }
    catch( const exception& ex ) {
        DEBUG(-1, "scanChunkFinder[" << scfa->__m_path << "]: " << ex.what() << endl);
    }
    catch( int e ) {
        DEBUG(-1, "scanChunkFinder[" << scfa->__m_path << "]: caught errno=" << e << " - " << ::strerror(e) << endl);
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

    // Loop over all detected "start points" - the leading parts of patterns
    // not containing globbing expressions.
    for(mpmap_type::const_iterator p=mountpoints.begin(); p!=mountpoints.end(); p++) {
        // Grab lock on the FTW globals
        mutex_locker    lck( mp_ftw::mpLock );

        // Set up the external info for the NFTW call
        mp_ftw::maxDepth      = p->second.maxdepth;
        mp_ftw::regexList     = &p->second.regexes;
        mp_ftw::mountpointSet = &mps;

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
    return mps;
}




///////////////////////////////////////////////////////////////////
//           Find scan chunks
//  Chunks for a specific scan always look like:
//       /..../SCAN/SCAN.xxxxxxxx
//  with xxxxxxxx an eight-digit sequence number
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
        pthread_t*  tidptr = new pthread_t;

        if( (create_error=mp_pthread_create(tidptr, &scanChunkFinder<appender_type>,
                                           new scanChunkFinderArgs<appender_type>(*mp, scan, appender, &mtx)))!=0 ) {
            DEBUG(-1, "find_recordingchunks: failed to create thread [" << *mp << "] - " << ::strerror(create_error) << endl);
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
    EZASSERT2(create_error==0, mountpoint_exception, EZINFO("Failed to create a thread - " << ::strerror(errno)));
    return rv;
}


bool isValidPattern(const string& pattern) {
    static const Regular_Expression  valid_pattern( "^((\\^/.*\\$)|/.*)$" );
    //cout << "isValidPattern(" << pattern << "): " << valid_pattern.matches(pattern) << endl;
    return valid_pattern.matches(pattern);
}
