#include <threadfns/multisend.h>
#include <threadfns/kvmap.h>
#include <mk5_exception.h>
#include <evlbidebug.h>
#include <getsok.h>
#include <getsok_udt.h>
#include <threadutil.h>
#include <auto_array.h>
#include <libudt5ab/udt.h>
#include <sstream>
#include <algorithm>
#include <sciprint.h>
#include <ftw.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <stdlib.h>   // for random

using namespace std;

DEFINE_EZEXCEPT(FileSizeException)


///////////////////////////////////////////////////////////////////
//          extract sequence number from file name;
//          last 8 characters should be an integer
///////////////////////////////////////////////////////////////////
uint32_t extract_file_seq_no(const string& s) {
    const string::size_type   dot = s.rfind( '.' );

    if( dot==string::npos )
        return (uint32_t)-1;

    uint32_t    sn;

    if( ::sscanf(s.substr(dot+1).c_str(), "%" SCNu32, &sn)!=1 )
        return (uint32_t)-1;

    return sn;
}




///////////////////////////////////////////////////////////////////
//          filemetadata
///////////////////////////////////////////////////////////////////
filemetadata::filemetadata():
    fileSize( (off_t)0 )
{}

filemetadata::filemetadata(const string& fn, off_t sz, uint32_t csn):
    fileSize( sz ), chunkSequenceNr( csn ), fileName( fn )
{}


///////////////////////////////////////////////////////////////////
//          chunk_location
///////////////////////////////////////////////////////////////////
chunk_location::chunk_location()
{}

chunk_location::chunk_location( string mp, string rel):
    mountpoint( mp ), relative_path( rel )
{}


///////////////////////////////////////////////////////////////////
//          mark6_vars_type
///////////////////////////////////////////////////////////////////
mark6_vars_type::mark6_vars_type():
    mk6( false ), packet_size( -1 ), packet_format( mk6_file_header::UNKNOWN_FORMAT )
{}

mark6_vars_type::mark6_vars_type(int32_t ps, mk6_file_header::packet_formats pf):
    mk6( true ), packet_size( ps ), packet_format( pf )
{}


///////////////////////////////////////////////////////////////////
//          multireadargs
///////////////////////////////////////////////////////////////////
multireadargs::~multireadargs() {
    // delete all memory pools
    for( mempool_type::iterator pool=mempool.begin(); pool!=mempool.end(); pool++)
        delete pool->second;
}

///////////////////////////////////////////////////////////////////
//          multifileargs
///////////////////////////////////////////////////////////////////

multifileargs::multifileargs(runtime* ptr, filelist_type fl, mark6_vars_type mk6):
    listlength( fl.size() ), rteptr( ptr ), filelist( fl ), mk6vars( mk6 )
{ EZASSERT2_NZERO(rteptr, cmdexception, EZINFO("null pointer runtime!")) }

multifileargs::~multifileargs() {
    // delete all memory pools
    for( mempool_type::iterator pool=mempool.begin(); pool!=mempool.end(); pool++)
        delete pool->second;
    // close all files
    for(fdmap_type::iterator curfd=fdmap.begin(); curfd!=fdmap.end(); curfd++)
        if( curfd->second>=0 ) {
            DEBUG(3, "Closing fd#" << curfd->second << " [" << curfd->first << "]" << endl);
            ::close( curfd->second );
        }
}

///////////////////////////////////////////////////////////////////
//          multinetargs
///////////////////////////////////////////////////////////////////
multinetargs::multinetargs(fdreaderargs* fd):
    fdreader( fd )
{ 
    EZASSERT2_NZERO(fdreader, cmdexception, EZINFO("null pointer fdreader!"))
    // Now we can properly initilize the fildedescriptor operations
    fdoperations = fdoperations_type( fdreader->netparms.get_protocol() );
}

multinetargs::~multinetargs() {
    // delete all memory pools
    for( mempool_type::iterator pool=mempool.begin(); pool!=mempool.end(); pool++)
        delete pool->second;
    
    bool alreadyclosed = false;

    // check all filedescriptors that were closed
    // if "our" fd was already closed, then we don't
    // have to to that again
    for( threadfdlist_type::iterator tidptr = threadlist.begin();
         !alreadyclosed && tidptr!=threadlist.end(); tidptr++ )
            alreadyclosed = (tidptr->second==fdreader->fd);

    if( !alreadyclosed )
        ::close_filedescriptor( fdreader );
    delete fdreader;
}

///////////////////////////////////////////////////////////////////
//         rsyncinitargs
///////////////////////////////////////////////////////////////////
rsyncinitargs::rsyncinitargs(string n, networkargs na):
    scanname( n ), conn( 0 ), netargs( na )
{}

rsyncinitargs::~rsyncinitargs() {
    if( this->conn ) {
        ::close_filedescriptor(this->conn);
        delete this->conn;
        this->conn = 0;
    }
}

////////////  file descriptor operations
DECLARE_EZEXCEPT(udtexcept)
DEFINE_EZEXCEPT(udtexcept)

// wrappers around UDT::recv and UDT::send because
// their signatures do not exactly match ::recv and ::send.
// The wrapper's signatures do.
ssize_t udtrecv(int s, void* b, size_t n, int f) {
    int   r = UDT::recv((UDTSOCKET)s, (char*)b, (int)n, f);
    if( r==UDT::ERROR ) {
        UDT::ERRORINFO&  udterror = UDT::getlasterror();
        DEBUG(-1, "udtrecv(" << s << ", .., n=" << n << " ..)/" << udterror.getErrorMessage() << " (" << udterror.getErrorCode() << ")" << endl);
        r = -1;
    }
    return (ssize_t)r;
}
ssize_t udtsend(int s, const void* b, size_t n, int f) {
    int   r = UDT::send((UDTSOCKET)s, (const char*)b, (int)n, f);

    if( r==UDT::ERROR ) {
        UDT::ERRORINFO&  udterror = UDT::getlasterror();
        DEBUG(-1, "udtsend(" << s << ", .., n=" << n << " ..)/" << udterror.getErrorMessage() << " (" << udterror.getErrorCode() << ")" << endl);
        r = -1;
    }
    return (ssize_t)r;
}


// On UDT connections we allow the ipd to be set
void setipd_tcp(int, int) {
    EZASSERT2(false, cmdexception, EZINFO("Setting IPD on tcp-based connections is a no-op"));
}

void setipd_udt(int fd, int ipd) {
    int          dummy;
    IPDBasedCC*  ccptr = 0;

    EZASSERT2( UDT::getsockopt(fd, SOL_SOCKET, UDT_CC, &ccptr, &dummy)==0, cmdexception,
               EZINFO("Failed to retrieve IPDBasedCC pointer from UDT socket"));
    EZASSERT2( ccptr, cmdexception, EZINFO("No congestion control instance found in UDT socket!?"));
    ccptr->set_ipd( ipd );
    DEBUG(4, "setipd_udt: set ipd=" << ipd << " on fd#" << fd << endl);
    return;
}


fdoperations_type::fdoperations_type() :
    writefn( 0 ), readfn( 0 ), setipdfn( 0 ), closefn( 0 )
{}

fdoperations_type::fdoperations_type(const string& protocol):
    writefn( 0 ), readfn( 0 ), setipdfn( 0 ), closefn( 0 )
{
    if( protocol=="tcp" ) {
        writefn  = &::send;
        readfn   = &::recv;
        closefn  = &::close;
        setipdfn = &setipd_tcp; // a no-op
    } else if( protocol=="udt" ) {
        writefn  = &udtsend;
        readfn   = &udtrecv;
        closefn  = &UDT::close;
        setipdfn = &setipd_udt;
    } else {
        THROW_EZEXCEPT(cmdexception, "unsupported protocol " << protocol);
    }
}


ssize_t fdoperations_type::read(int fd, void* ptr, size_t n, int f) const {
    ssize_t         r;
    unsigned char*  buf = (unsigned char*)ptr;

    while( n ) {
        r = readfn(fd, buf, (ssize_t)n, f);
        if( r<=0 )
            break;
        buf += r;
        n   -= (size_t)r;
    }
    return (ssize_t)(buf - (unsigned char*)ptr);
}

ssize_t fdoperations_type::write(int fd, const void* ptr, size_t n, int f) const {
    ssize_t              r;
    const unsigned char* buf = (const unsigned char*)ptr;

    while( n ) {
        r = writefn(fd, buf, (ssize_t)n, f);
        if( r<=0 )
            break;
        buf += r;
        n   -= (size_t)r;
    }
    return (ssize_t)(buf - (const unsigned char*)ptr);
}

int fdoperations_type::close(int fd) const {
    return closefn(fd);
}

void fdoperations_type::set_ipd(int fd, int ipd) const {
    setipdfn(fd, ipd);
    return;
}

// This one WILL throw if something's fishy.
// Read the metadata up to the double '\0'
string read_itcp_header(int fd, const fdoperations_type& fdops) {
    char          c;
    unsigned int  num_zero_bytes = 0;
    ostringstream oss;

    while( num_zero_bytes<2 ) {
        ASSERT_COND( fdops.readfn(fd, &c, 1, 0)==1 );
        if( c=='\0' )
            num_zero_bytes++;
        else
            num_zero_bytes = 0;
        oss << c;
    }
    return oss.str();
}


// Transformer functor: we know the strings we're passed match:
// /path/to/SCAN/SCAN.xxxxxxxx  (eight digits following)
struct chunkLocationMaker {
    chunk_location operator()(const string& path) {
        // find the 2nd last '/' and split the string there
        string::size_type   slash1 = path.rfind('/');
        string::size_type   slash2 = path.rfind('/', slash1-1);

        return chunk_location(path.substr(0, slash2), path.substr(slash2+1));
    }
};

// Use the find_recordingchunks() from mountpoints.h [it's multithreaded!]
// afterwards we transform the entries into a chunklist_type
chunklist_type get_chunklist(string scan, const mountpointlist_type& mountpoints) {
    filelist_type   chunks = find_recordingchunks(scan, mountpoints);
    chunklist_type  rv;

    transform(chunks.begin(), chunks.end(), back_inserter(rv), chunkLocationMaker());
    return rv;
}

multinetargs* mk_server(runtime* rteptr, netparms_type np) {
    return new multinetargs(net_server(networkargs(rteptr, np)));
}

// The multifileargs/multinetargs close function
void mfa_close(multifileargs* mfaptr) {
    for( threadfdlist_type::iterator tidptr = mfaptr->threadlist.begin();
         tidptr!=mfaptr->threadlist.end(); tidptr++ ) {
            // If fd non-negative close it. We know these are only files
            if( tidptr->second>=0 )
                ::close( tidptr->second );
            // And signal the thread
            ::pthread_kill(tidptr->first, SIGUSR1);
    }
}

void mna_close(multinetargs* mnaptr) {
    for( threadfdlist_type::iterator tidptr = mnaptr->threadlist.begin();
         tidptr!=mnaptr->threadlist.end(); tidptr++ ) {
            // If file descriptor non-negative, close it
            if( tidptr->second>=0 )
                mnaptr->fdoperations.closefn( tidptr->second );
            // And signal the thread
            ::pthread_kill(tidptr->first, SIGUSR1);
    }
    // As an encore, do close the lissnin' sokkit
    try {
        if( mnaptr->fdreader )
            ::close_filedescriptor( mnaptr->fdreader );
    } catch( std::exception& e ) {
        DEBUG(2, "mna_close/exception whilse closing listening socket " << e.what() << endl);
    } catch( ... ) {
        DEBUG(2, "mna_close/unknown exception whilse closing listening socket" << endl);
    }
}

void rsyncinit_close(rsyncinitargs* rsiptr) {
    try {
        if( rsiptr->conn )
            ::close_filedescriptor( rsiptr->conn );
    } catch( std::exception& e ) {
        DEBUG(2, "rsyncinit_close/exception whilse closing client socket " << e.what() << endl);
    } catch( ... ) {
        DEBUG(2, "rsyncinit_close/unknown exception whilse closing client socket" << endl);
    }
}

#ifdef O_LARGEFILE
    #define LARGEFILEFLAG  O_LARGEFILE
#else
    #define LARGEFILEFLAG  0
#endif



//////////////////////////////////////////////////////////
//////////////////////// initiator  //////////////////////
//////////////////////////////////////////////////////////

// We need a new first step. The first step will
// 1) compile a list of files locally present
// 2) connect to the destination flexbuff running "net2vbs"
//    and send the list of files
// 3) the remote flexbuff will inspect the files locally
//    already present and send back the list of files
//    which remain to be sent
// 4) the initiator takes this list and passes the files
//    downwards in such an order that the reads are striped
//    across the available mountpoints
// This is a produces which produces chunk descriptions

bool inset_fn(const string& v, const set<string>& s) {
    return s.find(v)!=s.end();
}

bool not_inset_fn(const string& v, const set<string>& s) {
    return s.find(v)==s.end();
}

// Helper to support finding elements in STL containers
template <typename T>
struct inset {
    inset( const T& s ):
        __setref( s )
    {}

    template <typename U>
    bool operator()(const U& element) const {
        return std::find(__setref.begin(), __setref.end(), element)!=__setref.end();
    }
    const T& __setref;
};


void rsyncinitiator(outq_type<chunk_location>* outq, sync_type<rsyncinitargs>* args) {
    chunklist_type   fl;
    rsyncinitargs*   rsyncinit = args->userdata;

    // Before anything, install signalhandler so we can be cancelled
    install_zig_for_this_thread(SIGUSR1);

    DEBUG(2, "rsyncinitiator/starting" << endl);
    // Get the file list for the indicated scan
    fl = get_chunklist(rsyncinit->scanname, rsyncinit->netargs.rteptr->mk6info.mountpoints);

    // If there's no files to sync, we're done very quickly! We don't need
    // to throw exceptions because it's not really exceptional, is it?
    if( fl.empty() ) {
        DEBUG(-1, "rsyncinitiator/no files found for scan '" << rsyncinit->scanname << "'" << endl);
        return;
    }
    DEBUG(4, "rsyncinitiator/got " << fl.size() << " files to sync" << endl);

    // Create the message. First the header [indicating this is an rsync
    // request], then the payload, which is a list of '\0'-separated file names
    // (note: only the _relative_ paths because we don't know where the
    // remote end has stored them)
    bool              cancelled;
    kvmap_type        hdr;
    ostringstream     payload;

    // prepare payload so we can inform the remote end how many bytes to read
    for( chunklist_type::iterator fptr=fl.begin(); fptr!=fl.end(); fptr++ )
        payload << fptr->relative_path << '\0';

    const string   payload_s( payload.str() );

    // Send two key/value pairs: the scan name +
    // the length of the file list that we'll be sending
    hdr.set( "requestRsync", rsyncinit->scanname );
    hdr.set( "payloadSize", payload_s.size() );

    const string   hdr_s( hdr.toBinary() );

    // Connect to remote side and store fdreader thing in our sync_type -
    // the cancellation function then can get us out of blocking request
    // should the user wish to cancel us
    SYNCEXEC(args,
             cancelled = args->cancelled;
             if( !cancelled )
                rsyncinit->conn = net_client(rsyncinit->netargs) );
    if( cancelled ) {
        DEBUG(-1, "rsyncinitiator/cancelled before initiating sync");
        return;
    }

    // Get a hold of the correct function pointers
    int               fd( rsyncinit->conn->fd );
    fdoperations_type fdops( rsyncinit->netargs.netparms.get_protocol() );

    // and send the initiating message
    fdops.write(fd, hdr_s.c_str(), hdr_s.size());
    fdops.write(fd, payload_s.c_str(), payload_s.size());

    DEBUG(4, "rsyncinitiator/init message sent, now waiting for reply ... " << endl);

    // Now wait for incoming reply
    uint32_t                   sz;
    kvmap_type::const_iterator szptr, typeptr;

    hdr.fromBinary( read_itcp_header(fd, fdops) );

    // We *must* have at least:
    //  rsyncreplysz: <amount of bytes>
    //  listtype: [have|need]
    //      (remote end will send the shortest list)
    EZASSERT((szptr=hdr.find("rsyncReplySz"))!=hdr.end(), cmdexception);
    EZASSERT((typeptr=hdr.find("listType"))!=hdr.end(), cmdexception);
    EZASSERT(typeptr->second=="have" || typeptr->second=="need", cmdexception);

    // Attempt to interpret the value as a number. Note: we do not
    // artificially cap the number - if you request 4TB of memory ... it
    // will #FAIL!
    EZASSERT2( ::sscanf(szptr->second.c_str(), "%" SCNu32, &sz)==1, cmdexception,
               EZINFO("Failed to parse reply size from meta data '" << szptr->second << "'") );

    DEBUG(4, "rsyncinitiator/reply sais we need to read " << sz << " bytes, list type = " << typeptr->second << endl);

    // Now read the reply
    auto_array<char>  flist( new char[ sz ] );
    ASSERT_COND( fdops.read(fd, &flist[0], (size_t)sz)==(ssize_t)sz );

    // Phew. Finally we have the reply. We don't need the network connection no more
    SYNCEXEC(args, ::close_filedescriptor(rsyncinit->conn))

    // Now we split it at '\0's to get at
    // the list of filessent to us
    bool             (*needcopy_fn)(const string&, const set<string>&);
    vector<string>     remote_lst = ::split(string(&flist[0], sz), '\0', true);
    set<string>        remote_set(remote_lst.begin(), remote_lst.end());
    chunklist_type     newfl;

    // We copy the chunks that we do need to send into a
    // second filelist_type (thanks C++! Take a look at Haskell please!)
    //
    // "have" => we must NOT send those files; the remote side already 
    //           has those, i.e. copy the file that are NOT in the remote
    //           set
    // "need" => we must ONLY send those files so we must only copy
    //           files that are actually IN this set
    if( typeptr->second=="have" )
        needcopy_fn = not_inset_fn;
    else
        needcopy_fn = inset_fn;
    // Ok, do it!
    for( chunklist_type::iterator fptr=fl.begin(); fptr!=fl.end(); fptr++ )
        if( needcopy_fn(fptr->relative_path, remote_set) )
            newfl.push_back( *fptr );

    DEBUG(2, "rsyncinitiator/after filtering there are " << newfl.size() << " files left to be sent" << endl);

    // Compile a mapping of files to send, organized by mount point
    typedef map<string, chunklist_type>   per_mp_type;
    per_mp_type            per_mp;
    
    for( chunklist_type::iterator fptr=newfl.begin(); fptr!=newfl.end(); fptr++ )
        per_mp[ fptr->mountpoint ].push_back( *fptr );

    // Now we clear the original file list and re-order the ones we need to
    // send
    fl.clear();

    // Now keep on round robin'ing over the mount points
    // to compile a list of files to xfer
    while( !per_mp.empty() ) {
        typedef list<per_mp_type::iterator>  erase_type;
        erase_type            toerase;
        per_mp_type::iterator mpptr;

        // Out of each Key (=mountpoint) pop the first file name.
        // [the fact the Key is still in the map implies there
        //  ARE/IS (a) file(s) to pop]
        for( mpptr=per_mp.begin(); mpptr!=per_mp.end(); mpptr++ ) {
            fl.push_back( mpptr->second.front() );
            mpptr->second.pop_front();
            // See - if the list has just become empty, we remove
            // the whole item
            if( mpptr->second.empty() )
                toerase.push_back( mpptr );
        }
        // Process all erasions
        for(erase_type::iterator eptr=toerase.begin(); eptr!=toerase.end(); eptr++)
            per_mp.erase( *eptr );
    }
    // The striping has been done, now all that's left is to push the files
    // downstream
    while( !fl.empty() ) {
        chunk_location   curchunk = fl.front();
        fl.pop_front();
        DEBUG(4, "rsyncinitiator/pushing " << curchunk.relative_path << " [" << curchunk.mountpoint << "]" << endl);
        if( outq->push(curchunk)==false )
            break;
    }
    DEBUG(2, "rsyncinitiator/done" << endl);
}

//////////////////////////////////////////////////////////
////////////////// New Parallelreader  ///////////////////
//////////////////////////////////////////////////////////

void parallelreader2(inq_type<chunk_location>* inq,  outq_type<chunk_type>* outq, sync_type<multireadargs>* args) {
    multireadargs*   mraptr = args->userdata;
    chunk_location   cl;

    // Before anything, install signalhandler so we can be cancelled
    install_zig_for_this_thread(SIGUSR1);

    DEBUG(4, "parallelreader[" << ::pthread_self() << "] starting" << endl);

    while( inq->pop(cl) ) {
        DEBUG(4, "parallelreader[" << ::pthread_self() << "] processing " << cl.relative_path << endl);

        // Push the file downstream
        int                    fd;
        off_t                  sz;
        //block                  b;
        ssize_t                rv;
        unsigned int           readcounter;
        const string           file( cl.mountpoint + "/" + cl.relative_path );
        mempool_type::iterator mempoolptr;

        ASSERT2_POS( fd=::open(file.c_str(), O_RDONLY|LARGEFILEFLAG),
                     SCINFO("failed to open " << file) );

        // As soon as we have the fd, tell the system WE are dealing with 'fd'
        SYNCEXEC(args, mraptr->threadlist[ ::pthread_self() ] = fd);

        ASSERT2_POS( sz = ::lseek(fd, 0, SEEK_END), SCINFO("failed to seek to end of '" << file << "'") );
        ASSERT2_ZERO( ::lseek(fd, 0, SEEK_SET), SCINFO("failed to seek to start of '" << file << "'") );
        // we use unsigned ints for blocksize, so it better fit
        EZASSERT2( sz <= UINT_MAX, FileSizeException, 
                   EZINFO("File '" << file.c_str() << "' too large, size: " << sz << "B, max: " << UINT_MAX << "B") );


        DEBUG(4, "parallelreader[" << ::pthread_self() << "] fd=" << fd << " sz=" << sz << endl);
#if 0
        // Messing with the memory pool might be better done
        // by one thread at a time ...
        SYNCEXEC(args,
                // Look up size in mempool and get a block
                mempoolptr = mraptr->mempool.find( sz );

                if( mempoolptr==mraptr->mempool.end() ) 
                    mempoolptr = mraptr->mempool.insert(
                        make_pair(sz, 
                                  new blockpool_type((unsigned int)sz, 
                                                     std::max((unsigned int)1, (unsigned int)(1.0e9/(double)sz)))
                                  )).first;
                 );

        b = mempoolptr->second->get();
#endif
        block  b( (size_t)sz );

        for ( readcounter = 0; readcounter < b.iov_len; readcounter += rv ) {
            rv = ::read(fd, 
                        (unsigned char*)b.iov_base + readcounter, 
                        b.iov_len - readcounter);
            ASSERT2_POS( rv, SCINFO("failed to read " << file) );
        }

        // Ok, we're done with fd
        SYNCEXEC(args, mraptr->threadlist[ ::pthread_self() ] = -1);

        ::close(fd);
        // Do some mongering on the file name
        uint32_t                        bsn;
        const vector<string>            elems = ::split(file, '/', true);
        const vector<string>::size_type vsz = elems.size();

        EZASSERT2(vsz>=3, cmdexception, EZINFO(": " << file << " - file name not consistent; too few '/' characters"));

        bsn = extract_file_seq_no( elems[vsz-1] );
        EZASSERT2(bsn!=(uint32_t)-1, cmdexception, EZINFO("Failed to extract sequence number from " << elems[vsz-1]));

        if( outq->push(chunk_type(filemetadata(elems[vsz-2]+"/"+elems[vsz-1], sz, bsn), b))==false )
            break;
        DEBUG(4, "parallelreader[" << ::pthread_self() << "] pushed " << elems[vsz-2] << "/" << elems[vsz-1] << " (" << sz << " bytes)" << endl);
    }
    DEBUG(4, "parallelreader[" << ::pthread_self() << "] done" << endl);
}

#if 0

//////////////////////////////////////////////////////////
///////////////////// Parallelreader  ////////////////////
//////////////////////////////////////////////////////////

void parallelreader(outq_type<chunk_type>* outq, sync_type<multifileargs>* args) {
    bool             done = false;
    chunk_location   file;
    multifileargs*   mfaptr = args->userdata;

    // Before anything, install signalhandler so we can be cancelled
    install_zig_for_this_thread(SIGUSR1);

    DEBUG(4, "parallelreader[" << ::pthread_self() << "] starting" << endl);

    // Lock the arguments, pop an item from the list, read contents, close
    // file and push
    while( !done ) {
        // Pop a file name from the list and at the same time
        // check if we're cancelled
        args->lock();
        done =  mfaptr->filelist.empty();
        if( !done ) {
            file = mfaptr->filelist.front();
            mfaptr->filelist.pop_front();
        }
        done = (done || args->cancelled);
        args->unlock();

        if( done )
            continue;

        DEBUG(3, "parallelreader[" << ::pthread_self() << "] processing " << file << endl);

        // Push the file downstream
        int                    fd;
        off_t                  sz;
        block                  b;
        mempool_type::iterator mempoolptr;

        ASSERT2_POS( fd=::open(file.c_str(), O_RDONLY|LARGEFILEFLAG),
                     SCINFO("failed to open " << file) );

        // As soon as we have the fd, tell the system WE are dealing with 'fd'
        SYNCEXEC(args, mfaptr->threadlist[ ::pthread_self() ] = fd);

        ASSERT2_POS( sz = ::lseek(fd, 0, SEEK_END), SCINFO("failed to seek " << file) );
        
        // we use unsigned ints for blocksize, so it better fit
        EZASSERT2( sz <= UINT_MAX, FileSizeException, 
                   EZINFO("File '" << file.c_str() << "' too large, size: " << sz << "B, max: " << UINT_MAX << "B") );

        DEBUG(4, "parallelreader[" << ::pthread_self() << "] fd=" << fd << " sz=" << sz << endl);

        // Look up size in mempool and get a block
        mempoolptr = mfaptr->mempool.find( sz );

        if( mempoolptr==mfaptr->mempool.end() ) 
            mempoolptr = 
                mfaptr->mempool.insert( make_pair(sz, new blockpool_type((unsigned int)sz, std::max((unsigned int)1, (unsigned int)(1.0e9/(double)sz)))) ).first;
        b = mempoolptr->second->get();
        ASSERT2_POS( ::read(fd, b.iov_base, b.iov_len),
                     SCINFO("failed to read " << file) );

        // Ok, we're done with fd
        SYNCEXEC(args, mfaptr->threadlist[ ::pthread_self() ] = -1);

        ::close(fd);
        // Do some mongering on the file name
        const vector<string>   elems = ::split(file, '/', true);

        if( outq->push(chunk_type(filemetadata(elems[2]+"/"+elems[3], sz), b))==false )
            done = true;
    }
    DEBUG(4, "parallelreader[" << ::pthread_self() << "] done" << endl);
}

#endif

//////////////////////////////////////////////////////////
///////////////////// Parallelsender  ////////////////////
//////////////////////////////////////////////////////////

// Only support TCP and UDT at the moment
// maybe multinetargs?
void parallelsender(inq_type<chunk_type>* inq, sync_type<networkargs>* args) {
    // the idea is to pop an item, open a new client connection and blurt
    // out the data
    int                rv;
    char               dummy[16];
    runtime*           rteptr  = 0;
    chunk_type         chunk;
    const networkargs& np( *args->userdata );
    fdoperations_type  fdops( np.netparms.get_protocol() );
    const bool         is_udt( np.netparms.get_protocol() == "udt" );

    DEBUG(4, "parallelsender[" << ::pthread_self() << "] starting" << endl);

    // Arrange for performance counter
    EZASSERT2_NZERO((rteptr = np.rteptr), cmdexception, EZINFO("null-pointer for runtime?"));

    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "ParallelSender", 0));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&       loscnt( rteptr->evlbi_stats.pkt_lost );
    ucounter_type&       pktcnt( rteptr->evlbi_stats.pkt_in );
    UDT::TRACEINFO       ti;

    // Our main loop!
    while( inq->pop(chunk) ) {
        DEBUG(3, "parallelsender[" << ::pthread_self() << "] processing " << chunk.tag.fileName << endl);
        // open new connection to wherever we're supposed to send to
        int            ipd = ipd_ns( np.netparms );
        kvmap_type     hdr;
        fdreaderargs*  conn = net_client( np );
        ostringstream  streamIds;

        if( np.netparms.get_protocol().find("tcp")==string::npos ) {
            EZASSERT2(ipd>=0, cmdexception, EZINFO("An IPD of <0 (" << ipd << ") is unacceptable"));
            fdops.set_ipd(conn->fd, ipd);
        }

        // Make the meta data
        hdr.set( "fileName", chunk.tag.fileName );
        hdr.set( "fileSize", chunk.tag.fileSize );

        size_t         sz;
        const string   streamId( hdr.toBinary() );
        unsigned char* ptr;

        // Blurt out the streamId followed by the binary data
        ASSERT_COND( fdops.write(conn->fd, streamId.c_str(), (ssize_t)streamId.size(), 0)==(ssize_t)streamId.size() );

        sz  = chunk.item.iov_len;
        ptr = (unsigned char*)chunk.item.iov_base;
        while( sz ) {
            const ssize_t  n = min((ssize_t)sz, (ssize_t)(2*1024*1024));

            rv = fdops.write(conn->fd, ptr, n, 0);

            if( rv!=n ) {
                DEBUG(-1, "Failed to send " << n << " bytes " << chunk.tag.fileName << endl);
                break;
            }
            ptr += n;
            sz  -= n;
            RTEEXEC(*rteptr, counter += n);
            if( is_udt && UDT::perfmon(conn->fd, &ti, true)==0 ) {
                RTEEXEC(*rteptr,
                        pktcnt += ti.pktSent;
                        loscnt += ti.pktSndLoss);
            }
        }
        // Ok, wait for remote side to acknowledge (or close the sokkit)
        // The read fails anyway even if the remote side did send something
        // (using UDT). The UDT lib is krappy!
        DEBUG(3, "parallelsender[" << ::pthread_self() << "] wait for remote" << endl);
        fdops.read(conn->fd, &dummy[0], 16, 0);

        DEBUG(3, "parallelsender[" << ::pthread_self() << "] closing file" << endl);
        // Done! Close file and lose memory resource!
        fdops.close( conn->fd );
        chunk.item = block();
        delete conn;
        DEBUG(3, "parallelsender[" << ::pthread_self() << "] done processing " << chunk.tag.fileName << endl);
    }
    DEBUG(4, "parallelsender[" << ::pthread_self() << "] done " << byteprint((double)counter, "byte") << endl);
}


// Helper function to do the accepting - based on the actual protocol.
fdprops_type::value_type* do_accept(fdreaderargs* fdr) {
    const string&             proto = fdr->netparms.get_protocol();
    fdprops_type::value_type* incoming = 0;

    // dispatch based on actual protocol
    if( proto=="unix" )
        incoming = new fdprops_type::value_type(do_accept_incoming_ux(fdr->fd));
    else if( proto=="udt" )
        incoming = new fdprops_type::value_type(do_accept_incoming_udt(fdr->fd));
    else
        incoming = new fdprops_type::value_type(do_accept_incoming(fdr->fd));

    return incoming;
}


//////////////////////////////////////////////////////////
///////////////////// Parallelnetreader  /////////////////
//////////////////////////////////////////////////////////

// Only support TCP and UDT at the moment

void parallelnetreader(outq_type<chunk_type>* outq, sync_type<multinetargs>* args) {
    runtime*                 rteptr = 0;
    uint32_t                 sz;
    kvmap_type               id_values;
    multinetargs*            mnaptr = args->userdata;
    fdreaderargs*            network = mnaptr->fdreader;
    kvmap_type::iterator     szptr, nmptr, rqptr, psptr;
    mempool_type::iterator   mempoolptr;
    const fdoperations_type& fdops( mnaptr->fdoperations );
    const bool               is_udt( network->netparms.get_protocol() == "udt" );


    // Before doing anything, register ourselves as a listener so we can get
    // cancelled whilst in a blocking wait
    install_zig_for_this_thread(SIGUSR1);
    SYNCEXEC(args, mnaptr->threadlist[ ::pthread_self() ] = network->fd);

    EZASSERT2_NZERO((rteptr = network->rteptr), cmdexception, EZINFO("null-pointer for runtime?"));

    // Grab a counter
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "ParallelNetReader", 0));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&       loscnt( rteptr->evlbi_stats.pkt_lost );
    ucounter_type&       pktcnt( rteptr->evlbi_stats.pkt_in );
    UDT::TRACEINFO       ti;

    // Do an accept on the server, read meta data - chunk # and chunk size,
    // suck in the data and pass on the tagged block
    DEBUG(4, "parallelnetreader[" << ::pthread_self() << "] starting" << endl);

    while( true ) {
        try {
            // Keep on accepting until nothing left to accept
            bool                      done = false;
            //block                     b;
            fdprops_type::value_type* incoming;
            // If this one fails, we better quit!
            try {
                incoming = do_accept(network);
            }
            catch( ... ) {
                break;
            }

            // First thing we do after acquiring a new fd is to register
            // it such that we get informed (and fd closed) upon
            // cancellation
            SYNCEXEC(args, mnaptr->threadlist[ ::pthread_self() ] = incoming->first; done=args->cancelled);
            if( done )
                break;

            DEBUG(3, "parallelnetreader[" << ::pthread_self() << "] incoming fd#" << incoming->first << " (" << incoming->second << ")" << endl);

            // First read the metadata:
            id_values.fromBinary( read_itcp_header(incoming->first, fdops) );

            // Assert we have the correct ones
            nmptr = id_values.find("fileName");
            szptr = id_values.find("fileSize");
            rqptr = id_values.find("requestRsync");
            psptr = id_values.find("payloadSize");

            // We must have either nmptr/szptr or rsync/payload, nothing else
            const bool   conds[4] = { nmptr!=id_values.end(), szptr!=id_values.end(),
                                      rqptr!=id_values.end(), psptr!=id_values.end() };

            EZASSERT2( (conds[0] && conds[1] && !(conds[2] || conds[3])) ||
                       (conds[2] && conds[3] && !(conds[0] || conds[1])),
                       cmdexception, EZINFO("Inconsistent request!") )

            //
            //   Two major modes of operation, depending on what 'message'
            //   came in
            //      nmptr + szptr?  => someone sending a file chunk
            //                         suck socket empty and blast to disk
            //      rqptr + psptr?  => someone sending a "request for rsync"
            //                         compile list of files we already have
            //                         and send diff list (the shortest one)
            //
            if( conds[0] ) {
                int             rv;
                uint32_t        n2read;
                unsigned char*  ptr;
                // Major mode 1: someone sent a chunk
                EZASSERT2( ::sscanf(szptr->second.c_str(), "%" SCNu32, &sz)==1, cmdexception,
                           EZINFO("Failed to parse file size from meta data '" << szptr->second << "'") );

                DEBUG(4, "parallelnetreader[" << ::pthread_self() << "] " << nmptr->second << " (" << szptr->second << " bytes)" << endl);

                // Now it's about time to start reading the file's contents
#if 0
                // Messing with the memory pool might be better done
                // by one thread at a time ...
                SYNCEXEC(args,
                    // Look up size in mempool and get a block
                    mempoolptr = mnaptr->mempool.find( sz );

                    if( mempoolptr==mnaptr->mempool.end() ) 
                        mempoolptr = mnaptr->mempool.insert(
                            make_pair(sz,
                                      new blockpool_type((unsigned int)sz,
                                                         std::max((unsigned int)1, (unsigned int)(1.0e9/sz)))
                                      )).first;
                    );
                b = mempoolptr->second->get();
#endif
                block    b( (size_t)sz );

                ptr    = (unsigned char*)b.iov_base;
                n2read = sz;
                while( n2read ) {
                    const ssize_t  n = min((ssize_t)n2read, (ssize_t)(2*1024*1024));

                    rv = fdops.read(incoming->first, ptr, n, 0);

                    if( rv!=n ) {
                        DEBUG(-1, "parallelnetreader[" << ::pthread_self() << "] " << nmptr->second << " failed to read " << n << " bytes after " << (sz-n2read) << " bytes" << endl);
                        break;
                    }
                    ptr    += n;
                    n2read -= n;
                    RTEEXEC(*rteptr, counter += n);
                    if( is_udt && UDT::perfmon(incoming->first, &ti, true)==0 ) {
                        RTEEXEC(*rteptr,
                                pktcnt += ti.pktRecv;
                                loscnt += ti.pktRcvLoss);
                    }
                }

                // Failure to push implies we should stop!
                // As does failure to read the whole chunk
                uint32_t    bsn = extract_file_seq_no(nmptr->second);
                EZASSERT2(bsn!=(uint32_t)-1, cmdexception, EZINFO(" Failed to extract sequence number from " << nmptr->second));

                if( n2read || outq->push( chunk_type(filemetadata(nmptr->second, (off_t)b.iov_len, bsn), b) )==false )
                    done = true;

                // already release our refcount on the block
                b = block();

                //
                //  End of Major mode 1/file chunk
                //
            } else {
                //  Major mode 2: rsync request
                char        dummy;

                EZASSERT2( ::sscanf(psptr->second.c_str(), "%" SCNu32, &sz)==1, cmdexception,
                           EZINFO("Failed to parse size from meta data '" << psptr->second << "'") );

                DEBUG(4, "parallelnetreader[" << ::pthread_self() << "] rsync request '" << rqptr->second << "' (" << psptr->second << " bytes payload)" << endl);

                auto_array<char> flist( new char[sz] );
                ASSERT_COND( fdops.read(incoming->first, &flist[0], (size_t)sz)==(ssize_t)sz );

                // Create a file list from what we received
                vector<string>           remote_lst = ::split(string(&flist[0], sz), '\0', true);
                //set<string>      remote_set(remote_lst.begin(), remote_lst.end());
                chunklist_type           fl = get_chunklist( rqptr->second, rteptr->mk6info.mountpoints );
                set<string>              local_set;
                vector<string>           have, have_not;

                // Create the set of local files
                for( chunklist_type::const_iterator fptr=fl.begin(); fptr!=fl.end(); fptr++ ) 
                    local_set.insert( fptr->relative_path );

                DEBUG(4, "parallelnetreader[" << ::pthread_self() << "] rsync request / remote list length " << remote_lst.size() << endl);
                DEBUG(4, "parallelnetreader[" << ::pthread_self() << "] rsync request / find " << local_set.size() << " files local" << endl);

                // Have to fucking brute force this!
                inset<set<string> >      have_local(local_set);
                vector<string>::iterator f, l;

                for(vector<string>::iterator ptr=remote_lst.begin(); ptr!=remote_lst.end(); ptr++)
                    if( have_local(*ptr) )
                        have.push_back(*ptr);
                    else
                        have_not.push_back(*ptr);

                // already clear out the meta data header
                id_values.clear();

                if( have.size()<have_not.size() ) {
                    // We have less files than we need. Set the list
                    // boundaries (of the file names we must transfer) and
                    // indicate that these are the files we HAVE
                    f = have.begin();
                    l = have.end();
                    id_values.set( "listType", "have" );
                } else {
                    // Ok we need to transfer the list of files we *need*
                    f = have_not.begin();
                    l = have_not.end();
                    id_values.set( "listType", "need" );
                }

                // Now we can construct the message payload
                ostringstream   os;

                for(vector<string>::iterator p=f; p!=l; p++)
                    os << *p << '\0';
                const string    pay = os.str();

                // Set the payload size in the message header
                id_values.set( "rsyncReplySz", pay.size() );

                // Now we can send back the full message, header first, then
                // payload
                const string    hdr = id_values.toBinary();
                fdops.write(incoming->first, hdr.c_str(), hdr.size());
                fdops.write(incoming->first, pay.c_str(), pay.size());

                // Do a dummy read - keep the sokkit open until remote end
                // has had a chance to read all the dataz
                fdops.read(incoming->first, &dummy, 1);
            }

            // Ok we're done with this filedescriptor, go back to monitoring
            // network->fd
            SYNCEXEC(args, mnaptr->threadlist[ ::pthread_self() ] = network->fd; done=args->cancelled);

            // Network's (hopefully) been sucked empty
            fdops.close(incoming->first);
            delete incoming;

            if( done )
                break;

        }
        catch( const exception& e) {
            DEBUG(2, "parallelnetreader[" << ::pthread_self() << "] caught " << e.what() << endl);
        }
        catch( ... ) {
            DEBUG(2, "parallelnetreader[" << ::pthread_self() << "] caught unknown exception" << endl);
        }
    }
    // Done while loop
    DEBUG(4, "parallelnetreader[" << ::pthread_self() << "] done" << endl);
}


template <typename InputIterator, typename OutputIterator>
void random_sort(InputIterator b, InputIterator e, OutputIterator out) {
    typedef std::vector<InputIterator>  inputiter_type;
    // How are we going to go about this? We want to randomly shuffle the
    // input range into the output iterator, without duplicating or
    // forgetting any of the elements out of the input range
    //
    // One possible way is:
    //    * create vector of iterators to individual elements in the range
    //    * pick a random element out of that vector
    //    * copy it to the output
    //    * delete it
    //    * repeat from step 2 until the vector is empty
    inputiter_type  inputiters;

    // fill the vector
    while( b!=e ) {
        inputiters.push_back( b );
        b++;
    }

    // Enter our major loop. 
    while( inputiters.size() ) {
        // Pick a random one
        typename inputiter_type::iterator        elem = inputiters.begin();
        const typename inputiter_type::size_type idx = (typename inputiter_type::size_type)( ::lrand48() % (long)inputiters.size() );

        std::advance(elem, idx);
        *out++ = **elem;
        inputiters.erase( elem );
    }
    return;
}

// Get the mountpoints from the mk6info struct, found in the runtime
multifileargs* get_mountpoints(runtime* rteptr, mark6_vars_type mk6) {
    if( rteptr->mk6info.mountpoints.empty() ) {
        DEBUG(-1, "get_mountpoints: no mountpoints to record on?" << endl);
    }
    EZASSERT2(!rteptr->mk6info.mountpoints.empty(), cmdexception, EZINFO("No mountpoints selected to record on?!"))

    // Now we randomize the list once such that successive runs of jive5ab
    // will stripe the data randomly over the disks
    filelist_type   randomized;

    random_sort(rteptr->mk6info.mountpoints.begin(), rteptr->mk6info.mountpoints.end(), back_inserter(randomized));
    
    return new multifileargs(rteptr, randomized, mk6);
}


// Function to recursively create paths
// http://stackoverflow.com/questions/2336242/recursive-mkdir-system-call-on-unix
int mkpath(const char* file_path_in, mode_t mode) {
    if( !file_path_in || !*file_path_in ) {
        errno = EINVAL;
        return -1;
    }
    char* file_path = ::strdup( file_path_in );
    if( !file_path ) {
        errno = ENOMEM;
        return -1;
    }

    for( char* p=::strchr(file_path+1, '/'); p; p=::strchr(p+1, '/')) {
        *p='\0';

        if( ::mkdir(file_path, mode)==-1 ) {
            if( errno!=EEXIST ) {
                *p='/';
                ::free( file_path );
                return -1;
            }
        }

        *p='/';
    }
    ::free( file_path );
    return 0;
}
//////////////////////////////////////////////////////////
///////////////////////// Parallelwriter /////////////////
//////////////////////////////////////////////////////////


#define MARK_MOUNTPOINT_BAD(msg) \
    DEBUG(-1, endl << \
              "#################### WARNING ####################" << endl << \
              "  mountpoint " << mountpoint << "  POSSIBLY BAD!"  << endl << \
              msg << \
              "  removing it from list!" << endl << \
              "#################################################" << endl) ; \
    SYNCEXEC(args, mfaptr->listlength -= 1; args->cond_broadcast());


void parallelwriter(inq_type<chunk_type>* inq, sync_type<multifileargs>* args) {
    // pop from the queue, then take a directory from the file list [the
    // file list now is a list of mount points], create file and dump
    // contents in it
    chunk_type              chunk;
    multifileargs*          mfaptr = args->userdata;
    const mark6_vars_type&  mk6vars( mfaptr->mk6vars );
    const bool              mk6( mfaptr->mk6vars.mk6 );

    DEBUG(4, "parallelwriter[" << ::pthread_self() << "] starting" << endl);

    while( inq->pop(chunk) ) {
        bool         written = false;
        set<string>  mp_seen; 

        // 'mp_seen' keeps track of which mountpoints we've seen. 

        DEBUG(4, "parallelwriter[" << ::pthread_self() << "] need to write " << chunk.tag.fileName << ", " << chunk.item.iov_len << " bytes (" << hex_t(chunk.item.iov_len) << ")" << endl);
        // Stay in while loop over mount points until we succeed in flushing
        // the data to disk.
        // Note to self: MAKE SURE NOT TO THROW INSIDE OF THIS LOOP!
        //               (We must put back the mountpoint right?!)
        while( !written ) {
            size_t    listlength = 0;
            string    mountpoint;

            // We need to wait for the filelist (i.e. mount point-list) to
            // become non-empty so 's we can pop an entry from it
            args->lock();

            while( (listlength=mfaptr->listlength)>0 && mfaptr->filelist.empty() )
                args->cond_wait();

            if( mfaptr->filelist.size()>0 ) {
                mountpoint = mfaptr->filelist.front();
                mfaptr->filelist.pop_front();
            }
            args->unlock();

            // If we were unsuccesfull in getting a mount point
            // there's very little we can do
            if( mountpoint.empty() )
                break;

            // Check if we already tried this mount point and decide
            // wether to wait for another one
            if( mp_seen.find(mountpoint)!=mp_seen.end() ) {
                // We've seen this one before.
                // If the size of our set >= the theoretical length of the list
                // we've seen ALL of the mount points
                //
                // [the 'theoretical list length' - "listlength", not
                // the current list length; other threads may still be
                // busy writing to another mount point]
                // 
                // If we haven't seen all of the mount points yet
                // we may continue.
                // In both cases we return the mount point to the stack of
                // mount points.
                SYNCEXEC(args, mfaptr->filelist.push_back(mountpoint); args->cond_signal());
                if( mp_seen.size()>=listlength )
                    break;
                else
                    continue;
            }
            mp_seen.insert( mountpoint );

            // Ok, we have location to write to
            int                  fd = -1, eno = 0;
            ssize_t              rv;
            uint64_t             bytes_written = 0;
            const string         fn = mountpoint + "/" + chunk.tag.fileName;
            fdmap_type::iterator fdptr;

            // When doing mk6 emulation, check if the file descriptor for
            // the current mountpoint is already open
            if( mk6 ) {
                SYNCEXEC(args,
                        if( (fdptr = mfaptr->fdmap.find(mountpoint))!=mfaptr->fdmap.end() )
                            fd = fdptr->second;
                        )
            }

            // If file descriptor<0, we must open it
            if( fd<0 ) {
                // Create the path - searchable for everyone, r,w,x for usr
                if( ::mkpath(fn.c_str(), 0755)!=0 ) {
                    MARK_MOUNTPOINT_BAD("Failed to create " << fn << " - " << ::strerror(errno) << endl)
                    continue;
                }

                // File is rw for owner, r for everyone else
                if( (fd=::open(fn.c_str(), O_CREAT|O_WRONLY|O_EXCL|LARGEFILEFLAG, 0644))<0 ) {
                    MARK_MOUNTPOINT_BAD("Failed to open " << fn << " - " << ::strerror(errno) << endl)
                    continue;
                }

                if( mk6 ) {
                    // If Mark6, we better write the file header. Because we *have*
                    // a chunk, we *know* what the size of the chunks are going to be
                    ssize_t         nw;
                    mk6_file_header fh( chunk.item.iov_len, mk6vars.packet_format, mk6vars.packet_size );

                    if( (nw=::write(fd, &fh, sizeof(mk6_file_header)))!=(ssize_t)sizeof(mk6_file_header) ) {
                        MARK_MOUNTPOINT_BAD("Failed to write Mark6 file header - " << fn << " - " << ::strerror(errno) << endl)
                        continue;
                    }
                }
            }

            // Ok, we have an open file descriptor - do write Mark6 block header, if we need to
            if( mk6 ) {
                ssize_t          nw;
                mk6_wb_header_v2 wb((int32_t)chunk.tag.chunkSequenceNr, (int32_t)chunk.item.iov_len);

                // If we fail to write, remember to error code and make sure
                // that the system does not try to write the chunk data.
                if( (nw=::write(fd, &wb, sizeof(mk6_wb_header_v2)))!=(ssize_t)sizeof(mk6_wb_header_v2) ) {
                    eno           = errno;
                    bytes_written = chunk.item.iov_len;
                }
            }
            
            DEBUG(4, "    parallelwriter[" << ::pthread_self() << "] attempt " << fn << endl);
        
            // Dump contents into file, save errno
            while ( bytes_written < chunk.item.iov_len ) {
                rv  = ::write(fd, ((char*)chunk.item.iov_base) + bytes_written, 
                              chunk.item.iov_len - bytes_written);
                if ( rv <= 0 ) {
                    eno = errno;
                    break;
                }
                else {
                    bytes_written += rv;
                }
            }
            DEBUG(4, "    parallelwriter[" << ::pthread_self() << "] result " << (bytes_written==(uint64_t)chunk.item.iov_len) << endl);
            // close file already [unless we're emulating Mark6 mode]
            if( !mk6 ) {
                ::close( fd );
                fd = -1;
            }

            // Now inspect how well it went
            written = (bytes_written==(uint64_t)chunk.item.iov_len);

            if( !written ) {
                // Oh dear, failed to write. Mountpoint bad?
                MARK_MOUNTPOINT_BAD("  failed to write " << chunk.item.iov_len << " bytes to " << fn << endl << 
                                    "    - " << ::strerror(eno) << endl)
                if( !mk6 && ::unlink( fn.c_str() )!=0 ) {
                    DEBUG(-1, "  oh and also failed to unlink(2) " << fn << endl);
                }
            } else {
                // Writing to file finished succesfully, now put back
                // mountpoint on the list and wake up only one waiter
                SYNCEXEC(args,
                    mfaptr->filelist.push_back(mountpoint); args->cond_signal();
                    mfaptr->fdmap.insert(make_pair(mountpoint, fd)) );
            }
        }
        // If we did not manage to write this chunk anywhere, we might as
        // well quit
        if( !written ) {
            DEBUG(-1, "    parallelwriter[" << ::pthread_self() << "] did not write #" << chunk.tag.chunkSequenceNr
                      << " into " << chunk.tag.fileName << endl);
            break;
        }
    }

    DEBUG(4, "parallelwriter[" << ::pthread_self() << "] done" << endl);
}


//////////////////////////////////////////////////////////
///////////////////////// chunkmaker /////////////////////
//////////////////////////////////////////////////////////


void chunkmaker(inq_type<block>* inq, outq_type<chunk_type>* outq, sync_type<std::string>* args) {
    block           b;
    uint32_t        chunkCount = 0;
    const string&   scanName( *args->userdata );

    while( inq->pop(b) ) {
        ostringstream   fn_s;

        // Got a new block. Come up with the correct chunk name
        fn_s << scanName << "/" << scanName << "." << format("%08u", chunkCount);

        DEBUG(4, "chunkmaker: created chunk " << fn_s.str() << " (size=" << b.iov_len << ")" << endl);

        if( outq->push(chunk_type(filemetadata(fn_s.str(), (off_t)b.iov_len, chunkCount), b))==false )
            break;
        // Maybe, just *maybe* it might we wise to increment the chunk count?! FFS!
        chunkCount++;
        b = block();
    }
}

// For Mark6 the file name is just the scan name with ".mk6" appended (...)
// The multiwriter will open <mountpoint>/<fileName> and dump the chunk in there
void mk6_chunkmaker(inq_type<block>* inq, outq_type<chunk_type>* outq, sync_type<std::string>* args) {
    block           b;
    uint32_t        chunkCount = 0;
    const string    fileName( *args->userdata/*+".mk6"*/ );

    while( inq->pop(b) ) {
        ostringstream   fn_s;

        if( outq->push(chunk_type(filemetadata(fileName, (off_t)b.iov_len, chunkCount), b))==false )
            break;
        chunkCount++;
        b = block();
    }
}
