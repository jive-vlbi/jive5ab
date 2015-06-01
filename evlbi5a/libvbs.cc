// Project includes
#include <libvbs.h>
#include <auto_array.h>
#include <evlbidebug.h>
#include <regular_expression.h>
#include <dosyscall.h>
#include <directory_helper_templates.h>

// Standardized C++ headers
#include <iostream>
#include <map>
#include <set>
#include <list>
#include <string>
#include <cstddef>
#include <cerrno>
#include <cstring>
//#include <csignal>
#include <cstdlib>
#include <climits>   // INT_MAX

// Old-style *NIX headers
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;

DECLARE_EZEXCEPT(vbs_except)
DEFINE_EZEXCEPT(vbs_except)


void scanRecording(string const& recname);
void scanRecordingMountpoint(string const& recname, string const& mp);
void scanRecordingDirectory(string const& recname, string const& dir);

/////////////////////////////////////////////////////
//
//           The detected mountpoints
//
/////////////////////////////////////////////////////
typedef list<string>  mountpoints_type;
typedef set<int>      observers_type;

static direntries_type  mountpoints = direntries_type();

void findMountPoints( string const& rootDir );



// Keep a global mapping of recording => filechunks
// note that we let the filechunks be an automatically sorted container

struct filechunk_type {
    // Note: no default c'tor
    // construct from full path name
    filechunk_type(string const& fnm):
        pathToChunk( fnm ), chunkFd( -1 ), chunkOffset( 0 )
    {
        // Get the chunk size
        int  fd = ::open( fnm.c_str(), O_RDONLY );
        if( fd<0 )
            throw errno;
            //throw string("error opening ")+fnm+": "+string(::strerror(errno));
        chunkSize = ::lseek(fd, 0, SEEK_END);
        ::close( fd );

        // At this point we assume 'fnm' looks like
        // "/path/to/file/chunk.012345678"
        string::size_type   dot = fnm.find_last_of('.');

        if( dot==string::npos )
            throw EINVAL;
            //throw string("error parsing chunk name ")+fnm+": no dot found!";

        // Note: we must instruct strtoul(3) to use base 10 decoding. The
        // numbers start with a loooot of zeroes mostly and if you pass "0"
        // as third arg to strtoul(3) "accept any base, derive from prefix"
        // this throws off the automatic number-base detection
        chunkNumber = (unsigned int)::strtoul(fnm.substr(dot+1).c_str(), 0, 10);
    }

    int open_chunk( int observer ) const {
        if( chunkFd<0 ) {
            chunkFd = ::open( pathToChunk.c_str(), O_RDONLY );
            DEBUG(5, "OPEN[" << pathToChunk << "] = " << chunkFd << ::strerror(errno) << endl);
        }
        chunkObservers.insert( observer );
        return chunkFd;
    }

    void close_chunk( int observer ) const {
        // remove observer from set of observers
        observers_type::iterator p = chunkObservers.find( observer );
        if( p!=chunkObservers.end() )
            chunkObservers.erase( p );
        // If set of observers is not empty, don't do nuttin'
        if( chunkObservers.size() )
           return; 
        // return to initial state
        if( chunkFd>=0 ) {
            ::close(chunkFd);
            DEBUG(5, "CLOS[" << pathToChunk << "]" << endl);
        }
        chunkFd  = -1;
        return;
    }

    ~filechunk_type() {
        // don't leak file descriptors
        // Too bad if any observers left but that
        // should technically be impossible.
        if( chunkFd>=0 )
            ::close( chunkFd );
    }

    // Note: declare chunkOffset as mutable such that we can later, after
    // the chunks have been sorted and put into a set, update the
    // chunkOffset value to what it should be. [Elements in a set are const
    // in order to guarantee that their sorting order is not compromised
    // as you alter the element - in this case WE know that the sorting only
    // depends on the value of 'chunkNumber' so we can safely edit
    // chunkOffset w/o worrying about compromising the set]
    string                 pathToChunk;
    off_t                  chunkSize;
    mutable int            chunkFd;
    mutable off_t          chunkOffset;
    unsigned int           chunkNumber;
    mutable observers_type chunkObservers;

    private:
        // no default c'tor!
        filechunk_type();
};

// Comparison operator for filechunk_type - sort by chunkNumber exclusively!
bool operator<(filechunk_type const& l, filechunk_type const& r) {
    return l.chunkNumber < r.chunkNumber;
}

typedef set<filechunk_type>             filechunks_type;
typedef map<string, filechunks_type>    chunkcache_type;

// Keep metadata per recording
struct metadata_type {
    off_t   recordingSize;
};
typedef map<string, metadata_type>      metadatacache_type;

static chunkcache_type                  chunkCache;
static metadatacache_type               metadataCache;

// Mapping of filedescriptor to open file
struct openfile_type {
    int                             fd;
    off_t                           filePointer;
    filechunks_type::iterator       chunkPtr;
    metadatacache_type::iterator    metadataPtr;

    // No default c'tor!
    openfile_type(metadatacache_type::iterator p, int f):
        fd( f ), filePointer( 0 ), metadataPtr( p )
    { 
        // Initialize chunk-pointer to point at first chunk
        chunkPtr = chunkCache[metadataPtr->first].begin();
    }

    ~openfile_type() {
        // unobserve all chunks
        filechunks_type&  chunks = chunkCache[metadataPtr->first];
        for( chunkPtr=chunks.begin(); chunkPtr!=chunks.end(); chunkPtr++)
            chunkPtr->close_chunk( fd );
    }
    private:
        openfile_type();
};
typedef map<int, openfile_type>         openedfiles_type;
openedfiles_type                        openedFiles;


// Upon shutdown, the library will close all open files
struct cleanup_type {
    cleanup_type() {
        // here we could do initialization
    }

    ~cleanup_type() {
        // Before clearing the caches, do clear the open files
        // Actually, that is taken care of by the openfile d'tor
        openedFiles.clear();
        chunkCache.clear();
        metadataCache.clear();
    }
};
static cleanup_type                     cleanup = cleanup_type();



//////////////////////////////////////////
//
//  int vbs_init()
//
//  Verify that the current root dir
//  exists and that we have sufficient
//  privileges to look inside it.
//
//  Return 0 on success, -1 on error and
//  sets errno.
//
//  EBUSY if there are currently files
//  still open
//
/////////////////////////////////////////

int vbs_init( char const* const rootdir ) {
    if( rootdir==0 ) {
        errno = EINVAL;
        return -1;
    }
    if( !openedFiles.empty() ) {
        errno = EBUSY;
        return -1;
    }

    // Set new root dir
    struct stat status;

    // Propagate failure to stat the rootdir 
    if( ::lstat(rootdir, &status)<0 )
        return -1;
    // Verify that it is a DIR that we have permission to enter into and
    // read
    if( !S_ISDIR(status.st_mode) ) {
        errno = ENOTDIR;
        return -1;
    }
    if( (status.st_mode & S_IRUSR)==0 ||
        (status.st_mode & S_IXUSR)==0 ) {
            errno = EPERM;
            return -1;
    }

    // clear all caches and really set rootDir
    mountpoints.clear();
    chunkCache.clear();
    metadataCache.clear();

    ::findMountPoints( string(rootdir) );
    return 0;
}

int vbs_init2( char const* const* rootdirs ) {
    if( rootdirs==0 ) {
        errno = EINVAL;
        return -1;
    }
    if( !openedFiles.empty() ) {
        errno = EBUSY;
        return -1;
    }

    // Assume all of the entries in the rootdirs ARE mountpoints
    // do check they're accessible directories though
    struct stat      status;
    direntries_type  newmps;

    for( ; *rootdirs; rootdirs++) {
        // Propagate failure to stat the rootdir 
        if( ::lstat(*rootdirs, &status)<0 )
            break;
        // Verify that it is a DIR that we have permission to enter into and
        // read
        if( !S_ISDIR(status.st_mode) ) {
            errno = ENOTDIR;
            break;
        }
        if( (status.st_mode & S_IRUSR)==0 ||
            (status.st_mode & S_IXUSR)==0 ) {
                errno = EPERM;
                break;
        }
        // Ok, seems good. Append to list of mountpoints
        newmps.insert( string(*rootdirs) );
    }
    // If mp is not-null, something went wrong during
    // checking of the mountpoints. Do not change current state 
    // of the library.
    if( *rootdirs ) {
        int eno = errno;
        // make sure that the DEBUG() stuff does not ruin the errno
        DEBUG(-1, "vbs_init2: fails at [" << *rootdirs << "]: " << ::strerror(eno) << endl);
        errno = eno;
        return -1;
    }

    // clear all caches and really set rootDir
    chunkCache.clear();
    metadataCache.clear();
    mountpoints = newmps;
    return 0;
}


//////////////////////////////////
//
//  vbs_open(char const* nm)
//
//  Scan mountpoints under rootDir
//  for subdirectories named 'nm'
//  and scan those for fileChunks
//
//////////////////////////////////

int vbs_open(char const* recname) {
    int rv = -1;
    try {
        // Scan current mount points for files for recording 'recname'
        scanRecording( recname );
        // If recname not in metadata cache ... bummer!
        metadatacache_type::iterator    metadataPtr = metadataCache.find(recname);

        if( metadataPtr==metadataCache.end() )
            throw ENOENT;

        // Okiedokie, the recording exists. Allocate a new filedescriptor
        // and put it in the opened files thingy
        rv = INT_MAX - openedFiles.size();
        openedFiles.insert( make_pair(rv, openfile_type(metadataPtr, rv)) );
    }
    catch( int eno ) {
        errno = eno;
    }
    catch( exception const& e ) {
        DEBUG(4, "vbs_open: " << e.what() << endl);
        errno = EINVAL;
    }
    // Do not catch (...) because we cannot translate an unknown
    // exception into a sensible error code
    return rv;
}

//////////////////////////////////
//
//  int vbs_close(int fd)
//
//  close a previously opened
//  recording
//
//////////////////////////////////

ssize_t vbs_read(int fd, void* buf, size_t count) {
    unsigned char*             bufc = (unsigned char*)buf;
    openedfiles_type::iterator fptr = openedFiles.find(fd);

    if( fptr==openedFiles.end() ) {
        errno = EBADF;
        return -1;
    }

    // Read bytes from file!
    int              realfd;
    size_t           nr = count;
    openfile_type&   of = fptr->second;
    filechunks_type& chunks = chunkCache[of.metadataPtr->first];

    // Cant read past eof
    if( of.chunkPtr==chunks.end() )
        return 0;

    // While we need to read bytes
    while( nr ) {
        // If we hit eof whilst reading that's not an error but we'd better
        // stop reading
        if( of.chunkPtr==chunks.end() )
            break;

        // Ok, we might be adressing inside a valid chunk
        const filechunk_type& chunk = *of.chunkPtr;

        // If we cannot open the current chunk
        if( (realfd=chunk.open_chunk(fd))<0 )
            break;

        // How much bytes can we read?
        off_t   n2r = min((off_t)nr, chunk.chunkOffset+chunk.chunkSize - of.filePointer);
        ssize_t actualread;

        if( n2r<=0 ) {
            // None at all, apparently. Move to next block!
            chunk.close_chunk(fd);
            if( of.chunkPtr!=chunks.end() )
                of.chunkPtr++;
            continue;
        }
        // Ok. Seek into the realfd
        ::lseek(realfd, of.filePointer - chunk.chunkOffset, SEEK_SET);
        // And read them dang bytes!
        if( (actualread=::read(realfd, bufc, (size_t)n2r))<0 )
            break;
        // Update pointers
        bufc           += actualread;
        nr             -= actualread;
        of.filePointer += actualread;
    }
    return (ssize_t)(count-nr);
}

//////////////////////////////////////////////////
//
//  int vbs_lseek(int fd, off_t offset, int whence)
//
//  see lseek(2)
//
/////////////////////////////////////////////////

off_t vbs_lseek(int fd, off_t offset, int whence) {
    off_t                      newfp;
    openedfiles_type::iterator fptr  = openedFiles.find(fd);

    if( fptr==openedFiles.end() ) {
        errno = EBADF;
        return -1;
    }

    openfile_type&  of   = fptr->second;
    metadata_type&  meta = of.metadataPtr->second;

    switch( whence ) {
        case SEEK_SET:
            newfp = offset;
            break;
        case SEEK_END:
            newfp = meta.recordingSize + offset;
            break;
        case SEEK_CUR:
            newfp = of.filePointer + offset;
            break;
        default:
            errno = EINVAL;
            return (off_t)-1;
    }
#if 0
    if( newfp<0 || newfp>meta.recordingSize ) {
        errno = EINVAL;
        return (off_t)-1;
    }
#endif
    // If the new file pointer is equal to the current file pointer,
    // we're done very quickly ...
    if( newfp==of.filePointer )
        return of.filePointer;


    // We've got the new file pointer!
    // Now skip to the chunk what contains the pointer
    filechunks_type&          filechunks = chunkCache[of.metadataPtr->first];
    filechunks_type::iterator newchunk = filechunks.begin();
    
    while( newchunk!=filechunks.end() && newfp>(newchunk->chunkOffset+newchunk->chunkSize) )
        newchunk++;

    // unobserve current chunk if new chunk is different
    if( of.chunkPtr!=newchunk )
        of.chunkPtr->close_chunk( fd );

    // Ok, update open file status
    of.filePointer = newfp;
    of.chunkPtr    = newchunk;

    return of.filePointer;
}

//////////////////////////////////
//
//  int vbs_close(int fd)
//
//  close a previously opened
//  recording
//
//////////////////////////////////

int vbs_close(int fd) {
    openedfiles_type::iterator fptr = openedFiles.find(fd);

    if( fptr==openedFiles.end() ) {
        errno = EBADF;
        return -1;
    }
    openedFiles.erase( fptr );
    return 0;
}

#if 0
//////////////////////////////////////////
//
//  int  vbs_setdbg(int newlevel)
//
//  Set a new debug level. Higher positive
//  numbers yield more detailed output.
//
//  Always succeeds and returns the 
//  previous debug level
//
/////////////////////////////////////////
int vbs_setdbg(int newlevel) {
    int   rv = dbglev_fn( newlevel );
    // prefent function signatures to be printed - always
    fnthres_fn( newlevel+1 );
    return rv;
}
#endif



////////////////////////////////////////
//
//  void findMountPoints( void )
//
//  scan current rootDir for directories
//  named "diskNNNNN"
//
/////////////////////////////////////////

struct isMountpoint {
    bool operator()(string const& entry) const {
        Regular_Expression      rxDisk("^disk[0-9]{1,}$");
        struct stat             status;
        string::size_type       slash = entry.find_last_of("/");

        // IF there is a slash, we skip it, if there isn't, we
        // use the whole string
        if( slash==string::npos )
            slash = 0;
        else
            slash += 1;
        DEBUG(5, "isMountpoint: checking name " << entry.substr(slash) << endl);
        if( !rxDisk.matches(entry.substr(slash)) )
            return false;
        if( ::lstat(entry.c_str(), &status)<0 ) {
            DEBUG(4, "predMountpoint: ::lstat fails on " << entry << " - " << ::strerror(errno) << endl);
            return false;
        }
        // We must have r,x access to the directory [in order to descend into it]
        return S_ISDIR(status.st_mode) && (status.st_mode & S_IRUSR) && (status.st_mode & S_IXUSR);
    }
};

void findMountPoints( string const& rootDir ) {
    DEBUG(5, "findMountPoints[" << rootDir << "]: enter with " << mountpoints.size() << " mountpoints" << endl);
    if( mountpoints.size() )
        return;
    mountpoints = dir_filter(rootDir, isMountpoint());
    DEBUG(5, "findMountPoints[" << rootDir << "]: exit with " << mountpoints.size() << " mountpoints" << endl);
}


////////////////////////////////////////
//
//  void scanRecording(string const&)
//
//  scan mountpoints for the requested
//  recording
//
/////////////////////////////////////////

void scanRecording(string const& recname) {
    // Loop over all mountpoints and check if there are file chunks for this
    // recording
    for(direntries_type::const_iterator curmp=mountpoints.begin(); curmp!=mountpoints.end(); curmp++)
        scanRecordingMountpoint(recname, *curmp);
    // Ok, all chunks have been gathered, now do the metadata - if anything
    // was found, that is
    if( chunkCache.find(recname)==chunkCache.end() )
        return;
    // Ok, at least *some* chunks have been found!
    metadata_type&    meta   = metadataCache[recname];
    filechunks_type&  chunks = chunkCache[recname];

    // Loop over all chunks - we add up the total size AND we compute, for
    // each chunk, the current offset wrt to the beginning of the recording
    meta.recordingSize = (off_t)0;
    for(filechunks_type::iterator fcptr=chunks.begin(); fcptr!=chunks.end(); fcptr++) {
        // Offset is recording size counted so far
        fcptr->chunkOffset = meta.recordingSize;
        // And add the current chunk to the recording size
        meta.recordingSize += fcptr->chunkSize;
        DEBUG(5, "scanRecording: " << fcptr->pathToChunk << " (" << fcptr->chunkNumber << ")" << endl);
    }
    DEBUG(5, "scanRecording: " << recname << " found " << meta.recordingSize << " bytes in " << chunks.size() << " chunks" << endl);
}

void scanRecordingMountpoint(string const& recname, string const& mp) {
    struct stat     dirstat;
    const string    dir(mp+"/"+recname);

    if( ::lstat(dir.c_str(), &dirstat)<0 ) {
        if( errno==ENOENT )
            return;
        DEBUG(4, "scanRecordingMountpoint(" << recname << ", " << mp << ")/::lstat() fails - " << ::strerror(errno) << endl);
        throw errno;
    }
    // OK, we got the status. If it's not a directory ...
    if( !S_ISDIR(dirstat.st_mode) )
        throw ENOTDIR;

    // Go ahead and scan the directory for chunks
    scanRecordingDirectory(recname, dir);
}

struct isRecordingChunk {
    isRecordingChunk(string const& recname):
        __m_regex( string("^")+recname+"\\.[0-9]{8}$" )
    {}

    bool operator()(string const& entry) const {
        DEBUG(5, "checking entry " << entry << " against " << __m_regex.pattern() << endl);
        return __m_regex.matches(entry);
    }

    Regular_Expression  __m_regex;

    private:
        isRecordingChunk();
};

void scanRecordingDirectory(string const& recname, string const& dir) {
    DIR*            dirp;
    direntries_type chunks;

    if( (dirp=::opendir(dir.c_str()))==0 ) {
        DEBUG(4, "scanRecordingDirectory(" << recname << ", " << dir << ")/ ::opendir fails - " << ::strerror(errno) << endl);
        throw errno;
    }
    chunks = dir_filter(dirp, isRecordingChunk(recname));
    ::closedir(dirp);

    for(direntries_type::const_iterator p=chunks.begin(); p!=chunks.end(); p++)
        chunkCache[recname].insert( filechunk_type(dir+"/"+*p) );
        //DEBUG(-1, "scanRecordingDirectory(" << recname << ", " << dir << ") chunk " << *p << endl);
}
