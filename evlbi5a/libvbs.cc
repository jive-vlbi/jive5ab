// Project includes
#include <libvbs.h>
#include <auto_array.h>
#include <evlbidebug.h>
#include <regular_expression.h>
#include <dosyscall.h>
#include <mutex_locker.h>
#include <directory_helper_templates.h>

// Standardized C++ headers
#include <iostream>
#include <map>
#include <set>
#include <list>
#include <algorithm>
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
#include <pthread.h>    // for thread safety! ;-)

using namespace std;

DECLARE_EZEXCEPT(vbs_except)
DEFINE_EZEXCEPT(vbs_except)


/////////////////////////////////////////////////////
//
//  Each chunk detected for a recording
//  gets its own metadata
//
/////////////////////////////////////////////////////
struct filechunk_type {
    // Note: no default c'tor
    // construct from full path name
    filechunk_type(string const& fnm):
        pathToChunk( fnm ), chunkFd( -1 ), chunkOffset( 0 )
    {
        // At this point we assume 'fnm' looks like
        // "/path/to/file/chunk.012345678"
        string::size_type   dot = fnm.find_last_of('.');

        if( dot==string::npos )
            throw EINVAL;
            //throw string("error parsing chunk name ")+fnm+": no dot found!";

        // Get the chunk size
        int  fd = ::open( fnm.c_str(), O_RDONLY );
        if( fd<0 )
            throw errno;
            //throw string("error opening ")+fnm+": "+string(::strerror(errno));
        chunkSize = ::lseek(fd, 0, SEEK_END);
        ::close( fd );

        // Note: we must instruct strtoul(3) to use base 10 decoding. The
        // numbers start with a loooot of zeroes mostly and if you pass "0"
        // as third arg to strtoul(3) "accept any base, derive from prefix"
        // this throws off the automatic number-base detection [it would
        // interpret the number as octal].
        chunkNumber = (unsigned int)::strtoul(fnm.substr(dot+1).c_str(), 0, 10);
    }

    filechunk_type(filechunk_type const& other):
        pathToChunk( other.pathToChunk ), chunkSize( other.chunkSize ),
        chunkFd( -1 ), chunkOffset( other.chunkOffset ), chunkNumber( other.chunkNumber )
    { }

    int open_chunk( void ) const {
        if( chunkFd<0 ) {
            chunkFd = ::open( pathToChunk.c_str(), O_RDONLY );
            DEBUG(5, "OPEN[" << pathToChunk << "] = " << chunkFd << ::strerror(errno) << endl);
        }
        return chunkFd;
    }

    void close_chunk( void ) const {
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

    private:
        // no default c'tor!
        filechunk_type();
};

// note that we let the filechunks be an automatically sorted container
// Comparison operator for filechunk_type - sort by chunkNumber exclusively!
//
bool operator<(filechunk_type const& l, filechunk_type const& r) {
    return l.chunkNumber < r.chunkNumber;
}

typedef set<filechunk_type>             filechunks_type;


////////////////////////////////////////////////////////////////
//
//  Prototypes so we can use the calls; implementation is at the
//  bottom of this module
//
////////////////////////////////////////////////////////////////
filechunks_type scanRecording(string const& recname, direntries_type const& mountpoints);
filechunks_type scanRecordingMountpoint(string const& recname, string const& mp);
filechunks_type scanRecordingDirectory(string const& recname, string const& dir);

////////////////////////////////////////
//
//  isMountpoint:
//
//  functor predicate, returns true
//  if directory entry is named
//  "disk[0-9]+" and is a directory
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



///////////////////////////////////////////////////////////
//
//      Mapping of filedescriptor to open file
//      shared data structure -> must be thread safe
//
// ////////////////////////////////////////////////////////
struct openfile_type {
    off_t                           filePointer;
    off_t                           fileSize;
    filechunks_type                 fileChunks;
    filechunks_type::iterator       chunkPtr;

    // No default c'tor!
    openfile_type(filechunks_type const& fcs):
        filePointer( 0 ), fileSize( 0 ), fileChunks( fcs )
    {
        for(chunkPtr=fileChunks.begin(); chunkPtr!=fileChunks.end(); chunkPtr++) {
            // Offset is recording size counted so far
            chunkPtr->chunkOffset = fileSize;
            // And add the current chunk to the recording size
            fileSize += chunkPtr->chunkSize;
        }
        chunkPtr = fileChunks.begin();
        DEBUG(5, "openfile_type: found " << fileSize << " bytes in " << fileChunks.size() << " chunks" << endl);
    }

    // The copy c'tor must take care of initializing the filechunk iterator
    // to point it its own filechunks, not at the other guys'
    openfile_type(openfile_type const& other):
        filePointer( 0 ), fileSize( other.fileSize ),
        fileChunks( other.fileChunks ), chunkPtr( fileChunks.begin() )
    {}

    ~openfile_type() {
        // unobserve all chunks
        for( chunkPtr=fileChunks.begin(); chunkPtr!=fileChunks.end(); chunkPtr++)
            chunkPtr->close_chunk();
    }
    private:
        openfile_type();
};

typedef map<int, openfile_type>         openedfiles_type;

pthread_rwlock_t                        openedFilesLock = PTHREAD_RWLOCK_INITIALIZER;
openedfiles_type                        openedFiles;


////////////////////////////////////////////////////////
//
// Upon shutdown, the library will close all open files
//
// /////////////////////////////////////////////////////
struct cleanup_type {
    cleanup_type() {
        // here we could do initialization
    }

    ~cleanup_type() {
        // Before clearing the caches, do clear the open files
        // Actually, that is taken care of by the openfile d'tor
        openedFiles.clear();
    }
};
static cleanup_type                     cleanup = cleanup_type();



//////////////////////////////////////////
//
//  int vbs_open()
//
//  Verify that the current root dir
//  exists and that we have sufficient
//  privileges to look inside it.
//
//  Return 0 on success, -1 on error and
//  sets errno.
//
/////////////////////////////////////////
int vbs_open(char const* recname, char const* const rootdir ) {
    if( recname==0 || *recname==0 || rootdir==0 || *rootdir=='\0' ) {
        errno = EINVAL;
        return -1;
    }

    // Test if rootdir is a sensible dir & find all
    // flexbuf mountpoints there
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

    // scan rootdir for mountpoints
    try {
        direntries_type mps = dir_filter(string(rootdir), isMountpoint());

        if( mps.empty() ) {
            // No mountpoints?
            errno = ENOENT;
            return -1;
        }

        // Now that we have mountpoints, we can scan those for 
        // the recording chunks - basically we now fall into
        // vbs_open2() as we have an array of mountpoints to scan
        auto_array<char const*>         vbsdirs( new char const*[ mps.size()+1 ] );
        direntries_type::const_iterator curmp = mps.begin();

        // Get array of "char*" and add a terminating 0 pointer
        for(unsigned int i=0; i<mps.size(); i++, curmp++)
            vbsdirs[i] = curmp->c_str();
        vbsdirs[ mps.size() ] = 0;

        return ::vbs_open2(recname, &vbsdirs[0]);
    }
    catch( int eno ) {
        errno = eno;
        return -1;
    }
    errno = EINVAL;
    return -1;
}

/////////////////////////////////////////////////////////////////////
//
// vbs_open2(recname, char const* const* rootdirs)
//
// Assume 'rootdirs' is an array of mountpoints. Scan each mountpoint
// for chunks of the recording by the name 'recname'
//
////////////////////////////////////////////////////////////////////
int vbs_open2( char const* recname, char const* const* rootdirs ) {
    // Sanity checks
    if( recname==0 || *recname=='\0' || rootdirs==0 ) {
        errno = EINVAL;
        return -1;
    }

    // Assume all of the entries in the rootdirs ARE mountpoints
    direntries_type  newmps;

    for( ; *rootdirs; rootdirs++)
        newmps.insert( string(*rootdirs) );

    // Ok, scan all mountpoints for chunks of the recording
    filechunks_type  chunks = scanRecording(recname, newmps);

    if( chunks.empty() ) {
        // nothing found?
        errno = ENOENT;
        return -1;
    }

    // Rite! We must allocate a new file descriptor!
    rw_write_locker lockert( openedFilesLock );

    const int fd = (openedFiles.empty() ? INT_MAX : (openedFiles.begin()->first - 1));
    openedFiles.insert( make_pair(fd, openfile_type(chunks)) );
    return fd;
}


//////////////////////////////////////////////////
//
//  int vbs_read(int fd, void* buf, size_t count)
//
//  read bytes from a previously opened recording
//
//////////////////////////////////////////////////

ssize_t vbs_read(int fd, void* buf, size_t count) {
    // we need read-only access to the int -> openfile_type mapping
    rw_read_locker             lockert( openedFilesLock );
    unsigned char*             bufc = (unsigned char*)buf;
    openedfiles_type::iterator fptr = openedFiles.find(fd) ;

    if( fptr==openedFiles.end() ) {
        errno = EBADF;
        return -1;
    }
    if( buf==0 ) {
        errno = EFAULT;
        return -1;
    }

    // when reading zero bytes, we're done. We even have done
    // a basic level of error checking ... (according to POSIX
    // that's ok)
    // http://pubs.opengroup.org/onlinepubs/009695399/functions/read.html
    if( count==0 )
        return 0;

    // Read bytes from file!
    int              realfd;
    size_t           nr = count;
    openfile_type&   of = fptr->second;
    filechunks_type& chunks = of.fileChunks;

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
        if( (realfd=chunk.open_chunk())<0 )
            break;

        // How much bytes can we read?
        off_t   n2r = min((off_t)nr, chunk.chunkOffset+chunk.chunkSize - of.filePointer);
        ssize_t actualread;

        if( n2r<=0 ) {
            // None at all, apparently. Move to next block!
            chunk.close_chunk();
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
    // we need read-only access to the int -> openfile_type mapping
    rw_read_locker             lockert( openedFilesLock );
    off_t                      newfp;
    openedfiles_type::iterator fptr  = openedFiles.find(fd);

    if( fptr==openedFiles.end() ) {
        errno = EBADF;
        return -1;
    }

    openfile_type&  of   = fptr->second;

    switch( whence ) {
        case SEEK_SET:
            newfp = offset;
            break;
        case SEEK_END:
            newfp = of.fileSize + offset;
            break;
        case SEEK_CUR:
            newfp = of.filePointer + offset;
            break;
        default:
            errno = EINVAL;
            return (off_t)-1;
    }
    if( newfp<0 ) {
        errno = EINVAL;
        return (off_t)-1;
    }
    // If the new file pointer is equal to the current file pointer,
    // we're done very quickly ...
    if( newfp==of.filePointer )
        return of.filePointer;

    // We've got the new file pointer!
    // Now skip to the chunk what contains the pointer
    filechunks_type::iterator newchunk   = of.fileChunks.begin();
    
    while( newchunk!=of.fileChunks.end() && newfp>(newchunk->chunkOffset+newchunk->chunkSize) )
        newchunk++;

    // unobserve current chunk if new chunk is different
    if( of.chunkPtr!=newchunk && of.chunkPtr!=of.fileChunks.end() )
        of.chunkPtr->close_chunk();

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
    // we need write access to the int -> openfile_type mapping
    rw_write_locker            lockert( openedFilesLock );
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
//  void scanRecording(string const&)
//
//  scan mountpoints for the requested
//  recording
//
/////////////////////////////////////////

filechunks_type scanRecording(string const& recname, direntries_type const& mountpoints) {
    filechunks_type  rv;

    // Loop over all mountpoints and check if there are file chunks for this
    // recording
    for(direntries_type::const_iterator curmp=mountpoints.begin(); curmp!=mountpoints.end(); curmp++) {
        filechunks_type tmp = scanRecordingMountpoint(recname, *curmp);
        rv.insert(tmp.begin(), tmp.end());
    }
    return rv;
}

filechunks_type scanRecordingMountpoint(string const& recname, string const& mp) {
    struct stat     dirstat;
    const string    dir(mp+"/"+recname);

    if( ::lstat(dir.c_str(), &dirstat)<0 ) {
        if( errno==ENOENT )
            return filechunks_type();
        DEBUG(4, "scanRecordingMountpoint(" << recname << ", " << mp << ")/::lstat() fails - " << ::strerror(errno) << endl);
        throw errno;
    }
    // OK, we got the status. If it's not a directory ...
    if( !S_ISDIR(dirstat.st_mode) )
        throw ENOTDIR;

    // Go ahead and scan the directory for chunks
    return scanRecordingDirectory(recname, dir);
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

filechunks_type scanRecordingDirectory(string const& recname, string const& dir) {
    DIR*            dirp;
    filechunks_type rv;
    direntries_type chunks;

    if( (dirp=::opendir(dir.c_str()))==0 ) {
        DEBUG(4, "scanRecordingDirectory(" << recname << ", " << dir << ")/ ::opendir fails - " << ::strerror(errno) << endl);
        throw errno;
    }
    chunks = dir_filter(dirp, isRecordingChunk(recname));
    ::closedir(dirp);

    for(direntries_type::const_iterator p=chunks.begin(); p!=chunks.end(); p++)
        rv.insert( filechunk_type(dir+"/"+*p) );
        //DEBUG(-1, "scanRecordingDirectory(" << recname << ", " << dir << ") chunk " << *p << endl);
    return rv;
}
