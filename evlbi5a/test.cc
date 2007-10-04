// c++ headers
#include <iostream>
#include <string>
#include <sstream>
#include <exception>
#include <map>
#include <vector>
#include <algorithm>


// system headers (for sockets and, basically, everything else :))
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/timeb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include <stdlib.h>
#include <limits.h>

// our own stuff
#include <dosyscall.h>
#include <ioboard.h>
#include <xlrdevice.h>
#include <evlbidebug.h>
#include <playpointer.h>
#include <transfermode.h>
#include <runtime.h>
#include <hex.h>
#include <streamutil.h>
#include <stringutil.h>

using namespace std;
extern int       h_errno;


// Main major loop termination condition
volatile bool done = false;

void dostop( int s ) {
    DEBUG(1, "Stopping because SIG#" << s << " raised" << endl);
    done = true;
}

// For displaying the events set in the ".revents" field
// of a pollfd struct after a poll(2) in human readable form.
// Usage:
// struct pollfd  pfd;
//   <... do polling etc ...>
//
// cout << "Events: " << eventor(pfd.revents) << endl;
struct eventor {
    eventor( short e ):
        events( e )
    {}
    short events;
};
ostream& operator<<( ostream& os, const eventor& ev ) {
    short e( ev.events );

    if( e&POLLIN )
        os << "IN,";
    if( e&POLLERR )
        os << "ERR, ";
    if( e&POLLHUP )
        os << "HUP, ";
    if( e&POLLPRI )
        os << "PRI, ";
    if( e&POLLOUT )
        os << "OUT, ";
    if( e&POLLNVAL )
        os << "NVAL!, ";
    return os;
}


// exception thrown when failure to insert command into
// mk5a commandset map
struct cmdexception:
    public std::exception
{
    cmdexception( const std::string& m ):
        __msg( m )
    {}

    virtual const char* what( void ) const throw() {
        return __msg.c_str();
    }

    virtual ~cmdexception() throw()
    {}

    const std::string __msg;
};




// ...
void setfdblockingmode(int fd, bool blocking) {
    int  fmode;

    ASSERT_POS(fd);
 
    fmode = ::fcntl(fd, F_GETFL);
    fmode = (blocking?(fmode&(~O_NONBLOCK)):(fmode|O_NONBLOCK));
    ASSERT2_ZERO( ::fcntl(fd, F_SETFL, fmode),
                  SCINFO("fd=" << fd << ", blocking=" << blocking); );
    return;
}



// Open a connection to <host>:<port> via the protocol <proto>.
// Returns the filedescriptor for this open connection.
// It will be in blocking mode.
// Throws if something fails.
int getsok( const string& host, unsigned short port, const string& proto ) {
    int                s;
    int                fmode;
    int                soktiep( SOCK_STREAM );
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent*   pptr;
    struct sockaddr_in src, dst;

    // If it's UDP, we change soktiep [type of the socket] from
    // SOCK_STREAM => SOCK_DGRAM. Otherwise leave it at SOCK_STREAM.
    if( proto=="udp" )
        soktiep = SOCK_DGRAM;

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(proto.c_str())), SCINFO(" - proto: '"<<proto << "'") );
    DEBUG(3, "Got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=::socket(PF_INET, soktiep, pptr->p_proto) );
    DEBUG(2, "Got socket " << s << endl);

    // Set in NONblocking mode
    fmode = fcntl(s, F_GETFL);
    fmode &= ~O_NONBLOCK;
//    fmode |= O_NONBLOCK;
    ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );

    // Bind to local
    src.sin_family      = AF_INET;
    src.sin_port        = 0;
    src.sin_addr.s_addr = INADDR_ANY;
    ASSERT2_ZERO( ::bind(s, (const struct sockaddr*)&src, slen), ::close(s) );

    // Fill in the destination adress
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons( port );

    // First try the simple conversion, otherwise we need to do
    // a lookup
    if( inet_aton(host.c_str(), &dst.sin_addr)==0 ) {
        struct hostent*  hptr;

        DEBUG(2, "Attempt to lookup " << host << endl);
        ASSERT2_NZERO( (hptr=::gethostbyname(host.c_str())),
                       ::close(s); SCINFO(" - " << hstrerror(h_errno) << " '" << host << "'") );
        memcpy(&dst.sin_addr.s_addr, hptr->h_addr, sizeof(dst.sin_addr.s_addr));
        DEBUG(2, "Found it: " << hptr->h_name << " [" << inet_ntoa(dst.sin_addr) << "]" << endl);
    }

    // Seems superfluous to use "dst.sin_*" here but those are the actual values
    // that get fed to the systemcall...
    DEBUG(2, "Trying " << host << "{" << inet_ntoa(dst.sin_addr) << "}:" << ntohs(dst.sin_port) << " ... " << endl);
    // Attempt to connect
    ASSERT2_ZERO( ::connect(s, (const struct sockaddr*)&dst, slen), ::close(s) );
    DEBUG(2, "Connected to " << inet_ntoa(dst.sin_addr) << ":" << ntohs(dst.sin_port) << endl);

    return s;
}


// Get a socket for incoming connections.
// The returned filedescriptor is in blocking mode.
//
// You *must* specify the port/protocol. Optionally specify
// a local interface to bind to. If left empty (which is
// default) bind to all interfaces.
int getsok(unsigned short port, const string& proto, const string& local = "") {
    int                s;
    int                fmode;
    int                soktiep( SOCK_STREAM );
    int                reuseaddr;
    unsigned int       optlen( sizeof(reuseaddr) );
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent*   pptr;
    struct sockaddr_in src;

    DEBUG(2, "port=" << port << ", proto=" << proto << ", local=" << local << endl);
    // If it's UDP, we change soktiep [type of the socket] from
    // SOCK_STREAM => SOCK_DGRAM. Otherwise leave it at SOCK_STREAM.
    if( proto=="udp" )
        soktiep = SOCK_DGRAM;

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(proto.c_str())), SCINFO(" - proto: '" << proto << "'") );
    DEBUG(3, "Got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=::socket(PF_INET, soktiep, pptr->p_proto) );
    DEBUG(3, "Got socket " << s << endl);

    // Set in blocking mode
    fmode = fcntl(s, F_GETFL);
    fmode &= ~O_NONBLOCK;
    ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );

    // Before we actually do the bind, set 'SO_REUSEADDR' to 1
    reuseaddr = 1;
    ASSERT2_ZERO( ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, optlen),
                  ::close(s) );

    // Bind to local
    src.sin_family      = AF_INET;
    src.sin_port        = htons( port );
    src.sin_addr.s_addr = INADDR_ANY;

    // if 'local' not empty, attempt to bind to that address
    // First try the simple conversion, otherwise we need to do
    // a lookup
    if( local.size() ) {
        if( inet_aton(local.c_str(), &src.sin_addr)==0 ) {
            struct hostent*  hptr;

            DEBUG(2, "Attempt to lookup " << local << endl);
            ASSERT2_NZERO( (hptr=::gethostbyname(local.c_str())),
                           ::close(s); lclSvar_0a << " - " << hstrerror(h_errno) << " '" << local << "'"; );
            memcpy(&src.sin_addr.s_addr, hptr->h_addr, sizeof(src.sin_addr.s_addr));
            DEBUG(2, "Found it: " << hptr->h_name << " [" << inet_ntoa(src.sin_addr) << "]" << endl);
        }
    }

    ASSERT2_ZERO( ::bind(s, (const struct sockaddr*)&src, slen), ::close(s) );

    // Ok. It's bound.
    // Now do the listen()
    if( proto=="tcp" ) {
        DEBUG(3, "Start to listen on interface " << local << endl);
        ASSERT2_ZERO( ::listen(s, 5), ::close(s) );
    }

    return s;
}



// thread function which reads data from disc into memory
void* disk2mem( void* argptr ) {
    runtime*            rte = (runtime*)argptr;
    S_READDESC          readdesc;
    playpointer         cur_pp( 0 );
    unsigned int        idx;
    unsigned int        nb;
    unsigned int        n_ull_p_block;
    unsigned long long* ullptr;

    try { 
        // We can't get cancelled. Signal us to stop via
        // rte->stop==true [and use pthread_condbroadcast()]
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );

        // indicate we're doing disk2mem
        rte->tomem_dev = dev_disk;

        // Can fill in parts of the readdesc that will not change
        // across invocations
        readdesc.XferLength = rte->buffer.blocksize();
        nb                  = rte->buffer.nblock();
        ullptr              = rte->buffer.buffer();
        n_ull_p_block       = readdesc.XferLength/sizeof(unsigned long long);
        // reset the current playpointer in the runtime to '0'
        rte->pp_current     = 0;

        // Now, enter main thread loop.
        idx    = 0;
        while( true ) {
            bool         stop, run, emptyblock;
            unsigned int nread;

            // States:
            // *initial*      run: false
            //                stop: false
            //                n_empty: >0 [==0 is valid but not usefull
            //                             unless someone else makes it
            //                             >0 at some point]
            //
            // From this initial state we have nothing to do;
            // all we do is transfer data from disk to memory, subject
            // to the boundary conditions that (1) we're in run mode
            // (run==true) and (2) there are free block(s) in memory where to
            // put the data.
            // If the stopflag is set, we should terminate.
            //
            // Changes in the initial are signalled by a pthread_cond_broadcast()
            // by other thread(s) when something has changed, eg:
            //
            // * user switched on the datatransfer signalled by a change
            //   in 'run' from false -> true
            // * user cancels the action, data may or may not be flowing,
            //   signalled by 'stop' going from false -> true
            // * the mem2* thread has successfully transferred a filled
            //   block to somewhere and signals that "number of free blocks
            //   available" has grown by 1:  n_empty -> n_empty+1
            //
            // So, we should only go into a blocking wait if we:
            // (1) don't know what to do (both run/stop are false)
            // (2) do know what to do (either run and/or stop are true)
            // (3) or we are supposed to run but we can't because there are
            //     no free blocks

            // grab the mutex
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );

            // and only go into pthread_cond_wait() if we RLY have to
            // Note: the while loop is to cactch spurious wakeups as well
            while( !rte->stop && (!rte->run || (rte->run && rte->n_empty==0)) ) {
                PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
            }
            // copy the thread-args so we can release the mutex and *then*
            // at ease evaluate what to do
            run        = rte->run;
            stop       = rte->stop;

            // and figure out where to start
            // [cur_pp starts at 0 so if it's > pp_start we've already
            //  incremented it, if it's <pp_start we haven't initialized it yet
            //  or, if it's equal, it doesn't matter anyway!]
            if( !stop && run && cur_pp<rte->pp_start )
                cur_pp = rte->pp_start;
            // if blocks available, grab one, but only if we're supposed to run
            if( !stop && (emptyblock = (run && rte->n_empty>0))==true )
                rte->n_empty--;
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

            // if stopflag is set, honour it
            if( stop )
                break;
            // if we're not in the running state
            // OR we're running but no blocks are free, we retry
            // (emptyblock already has "&& run" in it)
            if( !run || !emptyblock )
                continue;

            // Now let's read data, or decide to quit
            nread = 0;
            if( cur_pp<rte->pp_end ) {
                // haven't reached end-of-playable range yet, so read a block
                readdesc.AddrHi     = cur_pp.AddrHi;
                readdesc.AddrLo     = cur_pp.AddrLo;
                readdesc.BufferAddr = (long unsigned int*)(ullptr + idx * n_ull_p_block);

                XLRCALL( ::XLRRead(rte->xlrdev.sshandle(), &readdesc) );

                // Update read-state variables: current block and playpointer
                idx          = (idx+1)%nb;
                cur_pp.Addr += readdesc.XferLength;
                // indicate we have read a block
                nread = 1;
            }

            // test if we have reached end of playable range
            stop = (cur_pp>=rte->pp_end);
            // maybe... but was repeat set?
            if( stop && rte->repeat ) {
                stop   = false;
                cur_pp = rte->pp_start;
            }

            // grab the mutex
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            // update shared state variables
            rte->n_full += nread; // if nothing read this doesn't change the state
            // update the current playpointer in the rte
            // Do not use that one for our own purposes
            // as it might get clobbered due to other threads
            // doing gefingerpoken in the runtime
            rte->pp_current    = cur_pp;
            rte->nbyte_to_mem += nread * readdesc.XferLength;

            // broadcast that we've filled another block (possibly)
            PTHREAD_CALL( ::pthread_cond_broadcast(rte->condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
            // if we decided to stop, break from the loop!
            if( stop )
                break;
        }
    }
    catch( const exception& e ) {
        cerr << "disk2mem caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "disk2mem caught unknown exception?!" << endl;
    }
    if( rte )
        rte->tomem_dev = dev_none;
    return (void*)0;
}

// thread function which reads data from the FIFO into memory
void* fifo2mem( void* argptr ) {
    runtime*            rte = (runtime*)argptr;
    SSHANDLE            sshandle;
    DWORDLONG           blocksize;
    unsigned int        nb;
    unsigned int        idx;
    unsigned int        n_ull_p_block;
    unsigned long long* start;
    unsigned long long* buffer;

    try { 
        // not cancellable. stop us via:
        // rte->stop==true [and use pthread_condbroadcast()]
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );

        // indicate we're doing fifo2mem
        rte->tomem_dev = dev_fifo;

        // take over values from the runtime
        nb                 = rte->buffer.nblock();
        buffer             = rte->buffer.buffer();
        blocksize          = rte->buffer.blocksize();
        n_ull_p_block      = rte->buffer.blocksize()/sizeof(unsigned long long);
        sshandle           = rte->xlrdev.sshandle();
        rte->pp_current    = 0;

        // enter thread mainloop
        idx = 0;
        while( true ) {
            bool   run, stop, canread;

            // grab the mutex
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );

            // and only go into pthread_cond_wait() if we RLY have to
            // Note: the while loop is to cactch spurious wakeups as well
            while( !rte->stop && (!rte->run || (rte->run && rte->n_empty==0)) ) {
                PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
            }
            // copy the thread-args so we can release the mutex and *then*
            // at ease evaluate what to do
            run        = rte->run;
            stop       = rte->stop;

            // if blocks available, grab one, but only if we're supposed to run AND
            // there is enough data in the FIFO to grab. Otherwise we do NOT grab a block
            // and we'll retry again next time...
            if( !stop &&
                (canread = (run && rte->n_empty>0 && ::XLRGetFIFOLength(sshandle)>blocksize))==true  )
                rte->n_empty--;
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
#if 0
            cerr << "fifo2mem: stop=" << stop << ", canread=" << canread
                 << ", FIFOLen=" << ::XLRGetFIFOLength(sshandle)
                 << endl;
#endif
            // inspect what to do
            if( stop )
                break;
            // if we don't know what to do or we cannot do what we're supposed to do,
            // retry! (note: the 'canread' has already an implicit
            // "&& run && block_available && fifodata_available" in it...
            if( !run || !canread ) {
                usleep( 10000 );
                continue;
            }

            // get pointer to start
            start = buffer + idx * n_ull_p_block;

            // Now read a block from the fifo -> memory
            XLRCALL( ::XLRReadFifo(sshandle, (long unsigned int*)start, blocksize, 0) );

            // Update loop variable(s)
            idx        = (idx+1)%nb;

            // Ok, we've filled up another block,
            // tell the other that
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            rte->n_full++;
            rte->pp_current   += blocksize;
            rte->nbyte_to_mem += blocksize;
            PTHREAD_CALL( ::pthread_cond_broadcast(rte->condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
        }
        DEBUG(2, "fifo2mem: ending main loop" << endl);
    }
    catch( const exception& e ) {
        cerr << "fifo2mem caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "fifo2mem caught unknown exception?!" << endl;
    }
    if( rte )
        rte->tomem_dev = dev_none;
    return (void*)0;
}

// thread function which transfers data from mem'ry to FIFO
void* mem2streamstor( void* argptr ) {
    runtime*            rte = (runtime*)argptr;
    SSHANDLE            sshandle;
    DWORDLONG           blocksize;
    unsigned int        nb;
    unsigned int        idx;
    unsigned int        n_ull_p_block;
    unsigned long long* start;
    unsigned long long* buffer;

    try { 
        // not cancellable. stop us via:
        // rte->stop==true [and use pthread_condbroadcast()]
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );

        // indicate we're writing to the 'FIFO'
        rte->frommem_dev = dev_fifo;

        // take over values from the runtime
        nb                 = rte->buffer.nblock();
        buffer             = rte->buffer.buffer();
        blocksize          = rte->buffer.blocksize();
        n_ull_p_block      = rte->buffer.blocksize()/sizeof(unsigned long long);
        sshandle           = rte->xlrdev.sshandle();

        // enter thread mainloop
        idx = 0;
        while( true ) {
            bool   run, stop, fullblock;

            // grab the mutex
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );

            // and only go into pthread_cond_wait() if we RLY have to
            // Note: the while loop is to cactch spurious wakeups as well
            while( !rte->stop && (!rte->run || (rte->run && rte->n_full==0)) ) {
                PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
            }
            // copy the thread-args so we can release the mutex and *then*
            // at ease evaluate what to do
            run        = rte->run;
            stop       = rte->stop;

            // if blocks available, grab one, but only if we're supposed to run
            // otherwise we'll retry again next time...
            if( !stop &&
                (fullblock = (run && rte->n_full>0))==true  )
                rte->n_full--;
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

            // inspect what to do
            if( stop )
                break;
            // if we don't know what to do or we cannot do what we're supposed to do,
            // retry! (note: the 'fullblock' has already an implicit
            // "&& run && block_available" in it...
            if( !run || !fullblock ) {
                usleep( 10000 );
                continue;
            }

            // get pointer to start
            start = buffer + idx * n_ull_p_block;

            // Now write the current block to the StreamStor
            XLRCALL( ::XLRWriteData(sshandle, (void*)start, blocksize) );

            // Update loop variable(s)
            idx        = (idx+1)%nb;

            // Ok, we've written another block,
            // tell the other that
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            rte->n_empty++;
            rte->nbyte_from_mem += blocksize;
            PTHREAD_CALL( ::pthread_cond_broadcast(rte->condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
        }
        DEBUG(2, "mem2streamstor: ending main loop" << endl);
    }
    catch( const exception& e ) {
        cerr << "mem2streamstor caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "mem2streamstor caught unknown exception?!" << endl;
    }
    if( rte )
        rte->frommem_dev = dev_none;
    return (void*)0;
}

// Go from memory to net
void* mem2net( void* argptr ) {
    ssize_t             ntosend;
    runtime*            rte = (runtime*)argptr;
    unsigned int        idx;
    unsigned int        nb;
    unsigned int        datagramsize;
    unsigned int        n_ull_p_block;
    unsigned int        n_ull_p_dg;
    unsigned int        n_dg_p_block;
    struct iovec        iovect[2];
    struct msghdr       msg;
    unsigned long long  seqnr = 0;
    unsigned long long* start;
    unsigned long long* buffer;

    try { 
        // we should not be cancellable
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );
        ASSERT2_POS( rte->fd, SCINFO("No socket given (must be >=0)") );

        // Indicate we're doing mem2net
        rte->frommem_dev = dev_network;

        // take over values from the runtime
        nb                 = rte->buffer.nblock();
        buffer             = rte->buffer.buffer();
        datagramsize       = rte->buffer.datagramsize();
        n_ull_p_block      = rte->buffer.blocksize()/sizeof(unsigned long long);
        n_ull_p_dg         = datagramsize/sizeof(unsigned long long);
        n_dg_p_block       = rte->buffer.blocksize()/datagramsize;

        // Initialize stuff that will not change (sizes, some adresses etc)
        msg.msg_name       = 0;
        msg.msg_namelen    = 0;
        msg.msg_iov        = &iovect[0];
        msg.msg_iovlen     = sizeof(iovect)/sizeof(struct iovec);
        msg.msg_control    = 0;
        msg.msg_controllen = 0;
        msg.msg_flags      = 0;

        // part of the iovec can also be filled in [ie: the header]
        iovect[0].iov_base = &seqnr;
        iovect[0].iov_len  = sizeof(seqnr);
        // we always send datagrams of size datagramsize
        iovect[1].iov_len  = datagramsize;

        // Can precompute how many bytes should be sent in a sendmsg call
        ntosend = iovect[0].iov_len + iovect[1].iov_len;

        // enter thread main loop
        idx = 0;
        while( true ) {
            bool         stop, run, fullblock;

            // Wait for an event to happen
            // but only do that if we realy have to
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );

            // Only go into blocking wait for an event if we rilly have to
            while( !rte->stop && (!rte->run || (rte->run && rte->n_full==0)) ) {
                PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
            }

            // copy state variables whilst we have the mutex so we can
            // first release it and then evaluate what to do
            run       = rte->run;
            stop      = rte->stop;
            // if fullblocks available, grab one, but only if we're supposed to run
            if( !stop && (fullblock = (run && rte->n_full>0))==true )
                rte->n_full--;
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

            // if stopflag is set, honour it
            if( stop )
                break;
            // if we're not in the running state
            // OR we're running but no blocks are free, we retry
            // (emptyblock already has "&& run" in it)
            if( !run || !fullblock )
                continue;

            // Send the block out in datagrams, each of size
            // ... 'datagramsize' (you'd have never guessed eh)

            // get pointer to start
            start = buffer + idx * n_ull_p_block;

            for( unsigned int dg=0; dg<n_dg_p_block; dg++ ) {
                // Update message structure
                iovect[1].iov_base = start;

                // attempt send
                ASSERT_COND( ::sendmsg(rte->fd, &msg, MSG_EOR)==ntosend );

                // we did send out another datagram, 
                // Update loopvariables
                seqnr++;
                start += n_ull_p_dg;
            }
            // done sending block. Move counter to next block
            // and signal the other thread that another block has
            // become free
            idx    = (idx+1) % nb;

            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            // signal that we've emptied another block
            rte->n_empty++;
            rte->nbyte_from_mem += n_ull_p_block *sizeof(unsigned long long int);
            PTHREAD_CALL( ::pthread_cond_broadcast(rte->condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
        }
    }
    catch( const exception& e ) {
        cerr << "mem2net caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "mem2net caught unknown exception?!" << endl;
    }
    if( rte )
        rte->frommem_dev = dev_none;
    return (void*)0;
}

// thread function which reads data from the net into memory
// This is the "straight-through" implementation meant for
// reliable links: everything that is sent is expected to be
// received AND in the same order it was sent.
// Note:
//     the first thing this fn will do is try to
//     accept an incoming connection. Only *then*
//     it will try to read data
void* net2mem_reliablelink( void* argptr ) {
    bool                readingblock;
    bool                first;
    size_t              ntoread;
    size_t              blocksize;
    runtime*            rte = (runtime*)argptr;
    unsigned int        nb;
    unsigned int        idx;
    unsigned int        n_ull_p_block;
    struct pollfd       pfd;
    unsigned long long* start;
    unsigned long long* buffer;

    try { 
        // not cancellable. stop us via:
        // rte->stop==true [and use pthread_condbroadcast()]
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );

        // indicate we're transferring to memory
        rte->tomem_dev = dev_network;

        // take over values from the runtime
        nb                 = rte->buffer.nblock();
        buffer             = rte->buffer.buffer();
        // eeeuw.. maybe have type-conflict here...
        // I *think* we're safe for blocksizes up to 2GB ...
        blocksize          = (size_t)rte->buffer.blocksize();
        n_ull_p_block      = rte->buffer.blocksize()/sizeof(unsigned long long);

        // enter thread mainloop
        idx          = 0;
        first        = true; // for setting fd mode to non-blocking
        ntoread      = blocksize;
        readingblock = false;
        while( true ) {
            int     pres;
            bool    run, stop, grab;
            ssize_t nread;

            // grab the mutex
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );

            // and only go into pthread_cond_wait() if we RLY have to
            // Note: the while loop is to cactch spurious wakeups as well
            // "the system" will switch on 'run' as soon as it's decided
            // it's safe to start reading from 'rte->fd'
            // If we've already grabbed a block (readingblock==true)
            // we don't go into blocking wait; we need to try to 
            // read data from the socket.
            // if readingblock==false, we're not reading a block yet,
            // but if we're supposed to run and we're not reading a block
            // yet and there's not one available (n_empty==0) then it's
            // time to go in a blocking wait until someone decides to
            // release a block.
            // OTOH, if we're supposed to run and readingblock==true,
            // we can just go on and try and read from the socket
            while( !rte->stop &&
                   (!rte->run || (rte->run && (!readingblock && rte->n_empty==0))) ) {
                PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
            }
            // copy the thread-args so we can release the mutex and *then*
            // at ease evaluate what to do
            run        = rte->run;
            stop       = rte->stop;

            // if blocks available, grab one, but only if we're supposed to run 
            // AND this is the first time through the major-loop for the current block
            grab = run && !readingblock;
            if( !stop && grab && rte->n_empty>0  ) {
                rte->n_empty--;
                readingblock = true;
            }
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

            // inspect what to do
            if( stop )
                break;
            // if we don't know what to do or we cannot do what we're supposed to do,
            // retry! 
            if( !(run && readingblock) ) {
                usleep( 10 );
                continue;
            }

            // Only end up here if run && readingblock

            // Check if we need to initialize
            if( first ) {
                int          rbuf( rte->netparms.rcvbufsize );  
                unsigned int olen( sizeof(rte->netparms.rcvbufsize) );

                // Make sure we have a filedescriptor!
                ASSERT2_POS( rte->fd, SCINFO("No socket/filedescriptor!") );

                // do non-blocking I/O
                setfdblockingmode(rte->fd, false);

                // set receive-buf-size
                ASSERT_ZERO( ::setsockopt(rte->fd, SOL_SOCKET, SO_RCVBUF, &rbuf, olen) );

                // and set up polling
                pfd.fd     = rte->fd;
                pfd.events = POLLIN|POLLERR|POLLHUP;
                first = false;
            }

            // If this is the first call to read()
            // initialize "start"
            // Note: this statement could be executed multiple
            // times - as long as no data has come in for the
            // new block. But then again, it wouldn't make a
            // difference since start is still supposed to 
            // be at the start of the current block...
            if( ntoread==blocksize )
                start = buffer + idx * n_ull_p_block;

            // Now (attempt to) read a block from the socket into memry...
            // we only stay here for a while; we must regularly 
            // check the "stop" flag
            ASSERT_POS( (pres=::poll(&pfd, 1, 1)) );

            // if pres==0 => timeout, no data to read
            if( pres==0 )
                continue;

            // if there are HUPs or ERRORs on the datafd, terminate
            // this loop
            if( pfd.revents&POLLERR || pfd.revents&POLLHUP ) {
                DEBUG(0, "datasocket was hung up/had errorflag set. Closing and terminating reading."<<endl);
                ::close(rte->fd);
                rte->fd = -1;
                break;
            }
            // if nothing to read... 
            if( !(pfd.revents&POLLIN) )
                continue;

            // weehee! read sum data!
            ASSERT_POS( (nread=::read(rte->fd, start, ntoread)) );

            // Ok nread is >= 0
            ntoread -= nread;

            // if ntoread still >0, we need to read more to fill up the current block
            if( ntoread>0 )
                continue;

            // Great. Finally we've filled up a whole block.
            // Tell the other thread and re-initialize ourselves
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            rte->n_full++;
            rte->nbyte_to_mem += blocksize;
            PTHREAD_CALL( ::pthread_cond_broadcast(rte->condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

            // Update loop variable(s):
            // move on to nxt block, reset number of bytes to read
            // and indicate that we'd like to start reading another
            // block
            idx          = (idx+1)%nb;
            ntoread      = blocksize;
            readingblock = false;
        }
        DEBUG(2, "net2mem_reliablelink: ending main loop" << endl);
    }
    catch( const exception& e ) {
        cerr << "net2mem_reliablelink caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "net2mem_reliablelink caught unknown exception?!" << endl;
    }
    if( rte )
        rte->tomem_dev = dev_none;
    return (void*)0;
}




// map commands to functions.
// The functions must have the following signature.
//
// The functions take a (bool, const vector<string>&, runtime&),
// the bool indicating query or not, the vector<string>
// the arguments to the function and the 'runtime' environment upon
// which the command may execute. Obviously it's non-const... otherwise
// you'd have a rough time changing it eh!
//
// Note: fn's that do not need to access the environment (rarely...)
// may list it as an unnamed argument...
// Return a reply-string. Be sure to fully format the reply
// (including semi-colon and all)
//
// NOTE: the first entry in the vector<string> is the command-keyword
// itself, w/o '?' or '=' (cf. main() where argv[0] is the
// programname itself)
typedef string (*mk5cmd)(bool, const vector<string>&, runtime& rte);


// this is our "dictionary"
typedef map<string, mk5cmd>  mk5commandmap_type;


// The mark5 commands that we know of
string disk2net_fn( bool qry, const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    // store the socket in here.
    // if we create it but cannot start the threads (or something else
    // goes wrong) then the cleanup code at the end will close the socket.
    // *if* the threads are succesfully started make sure you
    // reset this value to <0 to prevent your socket from being
    // closed 
    int              s = -1;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing disk2net - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=disk2net ) {
        reply << " 1 : _something_ is happening and its NOT disk2net!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.lasthost << " : "
                  << rte.transfersubmode
                  << " : " << rte.pp_current;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        if( args[1]=="connect" ) {
            recognized = true;
            // if transfermode is already disk2net, we ARE already connected
            // (only disk2net::disconnect clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                int          sbuf( rte.netparms.sndbufsize );  
                unsigned int olen( sizeof(rte.netparms.sndbufsize) );

                // good. pick up optional hostname/ip to connect to
                if( args.size()>2 )
                    rte.lasthost = args[2];

                // create socket and connect 
                s = getsok(rte.lasthost, 2630, rte.netparms.protocol);

                // Set sendbufsize
                ASSERT_ZERO( ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sbuf, olen) );

                // before kicking off the threads, transfer some important variables
                // across. The playpointers will be done later on
                rte.fd     = s;
                rte.buffer = buffer_type( rte.netparms );

                // goodie. now start the threads disk2mem and mem2net!
                // start_threads() will throw up if something's fishy
                rte.start_threads(disk2mem, mem2net);

                // Make sure the local temporaries get reset to 0 (or -1)
                // to prevent deleting/foreclosure
                s             = -1;

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = disk2net;
                rte.transfersubmode.clr_all();
                // we are connected and waiting for go
                rte.transfersubmode |= connected_flag;
                rte.transfersubmode |= wait_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }

        // <on> : turn on dataflow
        if( args[1]=="on" ) {
            recognized = true;
            // only allow if transfermode==disk2net && submode hasn't got the running flag
            // set
            if( rte.transfermode==disk2net && (rte.transfersubmode&run_flag)==false ) {
                bool               repeat = false;
                playpointer        pp_s;
                playpointer        pp_e;
                unsigned long long nbyte;

                // Pick up optional extra arguments:
                // note: we do not support "scan_set" yet so
                //       the part in the doc where it sais
                //       that, when omitted, they refer to
                //       current scan start/end.. that no werk

                // start-byte #
                if( args.size()>2 && !args[2].empty() ) {
                    unsigned long long v;

                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    // if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[2].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("start-byte# is out-of-range");
                    pp_s.Addr = v;
                }
                // end-byte #
                // if prefixed by "+" this means: "end = start + <this value>"
                // rather than "end = <this value>"
                if( args.size()>3 && !args[3].empty() ) {
                    unsigned long long v;
                   
                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    //if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[3].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("end-byte# is out-of-range");
                    if( args[3][0]=='+' )
                        pp_e.Addr = pp_s.Addr + v;
                    else
                        pp_e.Addr = v;
                }
                // repeat
                if( args.size()>4 && !args[4].empty() ) {
                    long int    v = ::strtol(args[4].c_str(), 0, 0);

                    if( (v==LONG_MIN || v==LONG_MAX) && errno==ERANGE )
                        throw xlrexception("value for repeat is out-of-range");
                    repeat = (v!=0);
                }
                // now compute "real" start and end, if any
                // so the threads, when kicked off, don't have to
                // think but can just *go*!
                if( pp_e.Addr<=pp_s.Addr ) {
                    S_DIR       currec;
                    playpointer curlength;
                    // end <= start => either end not specified or
                    // neither start,end specified. Find length of recording
                    // and play *that*, starting at startbyte#
                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &currec) );
                    curlength = currec.Length;

                    // check validity of start,end
                    if( pp_s>=curlength ||  pp_e>=curlength ) {
                        ostringstream  err;
                        err << "start and/or end byte# out-of-range, curlength=" << curlength;
                        throw xlrexception( err.str() );
                    }
                    // if no end given: set it to the end of the current recording
                    if( pp_e==playpointer(0) )
                        pp_e = curlength;
                }
                // make sure the amount to play is an integral multiple of
                // blocksize
                nbyte = pp_e.Addr - pp_s.Addr;
                DEBUG(1,"start/end [nbyte]=" << pp_s << "/" << pp_e << " [" << nbyte << "]" << endl);
                nbyte = nbyte/rte.netparms.blocksize * rte.netparms.blocksize;
                if( nbyte<rte.netparms.blocksize )
                    throw xlrexception("less than <blocksize> bytes selected to play. no can do");
                pp_e = pp_s.Addr + nbyte;
                DEBUG(1,"Made it: start/end [nbyte]=" << pp_s << "/" << pp_e << " [" << nbyte << "]" << endl);
                
                // After we've acquired the mutex, we may set the "real"
                // variable (start) to true, then broadcast the condition.
                PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );
                
                // ok, now transfer options to the threadargs structure
                rte.pp_end   = pp_e;
                rte.repeat   = repeat;
                rte.pp_start = pp_s;
                // do this last such that a spurious wakeup will not
                // make a thread start whilst we're still filling in
                // the arguments!
                rte.run      = true;
                // now broadcast the startcondition
                PTHREAD2_CALL( ::pthread_cond_broadcast(rte.condition),
                               ::pthread_mutex_unlock(rte.mutex) );

                // And we're done, we may release the mutex
                PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
                // indicate running state
                rte.transfersubmode.clr( wait_flag ).set( run_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or disk2net, nothing else
                if( rte.transfermode==disk2net )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }

        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing disk2net.
            // Don't care if we were running or not
            if( rte.transfermode==disk2net ) {
                // let the runtime stop the threads
                rte.stop_threads();

                // destroy allocated resources
                if( rte.fd>=0 )
                    ::close( rte.fd );
                // reset global transfermode variables 
                rte.fd           = -1;
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing disk2net ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    // If any of the temporaries is non-default, clean up
    // whatever it was
    if( s>=0 )
        ::close(s);
    return reply.str();
}

// set up net2out
string net2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    ostringstream    reply;

    // store the socket in here.
    // if we create it but cannot start the threads (or something else
    // goes wrong) then the cleanup code at the end will close the socket.
    // *if* the threads are succesfully started make sure you
    // reset this value to <0 to prevent your socket from being
    // closed 
    int              s = -1;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=net2out ) {
        reply << " 1 : _something_ is happening and its NOT net2out!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <open>
        if( args[1]=="open" ) {
            recognized = true;
            // if transfermode is already net2out, we ARE already doing this
            // (only net2out::close clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                SSHANDLE      ss( rte.xlrdev.sshandle() );
                const string& proto( rte.netparms.protocol );

                // for now, only accept tcp or udp
                ASSERT_COND( ((proto=="udp")||(proto=="tcp")) );

                // create socket and start listening.
                // If netparms.proto==tcp we put socket into the
                // rte's acceptfd field so it will do the waiting
                // for connect for us (and kick off the threads
                // as soon as somebody make a conn.)
                s = getsok(2630, proto);

                // switch off recordclock
                rte.ioboard[ mk5areg::notClock ] = 1;

                // now program the streamstor to record from PCI -> FPDP
                XLRCALL( ::XLRSetMode(ss, SS_MODE_PASSTHRU) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_FRONT) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_FRONT) );
                XLRCALL( ::XLRSetDBMode(ss, SS_FPDP_XMITMASTER, 0) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMITMASTER, 0) );
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );

                // before kicking off the threads, transfer some important variables
                // across.
                if( proto=="udp" ) {
                    rte.fd       = s;
                    rte.acceptfd = -1;
                }
                else  {
                    rte.fd       = -1;
                    rte.acceptfd = s;
                }
                rte.buffer = buffer_type( rte.netparms );

                // goodie. now start the threads net2mem and mem2streamstor!
                // start_threads() will throw up if something's fishy
                // NOTE: we should change the net2mem policy based on
                // reliable or lossy transport.
                rte.start_threads(net2mem_reliablelink, mem2streamstor);

                // Note: the 'start_threads' routine sets the 'run' flag
                // to false unconditionally. However, if we're doing UDP
                // we can go immediately!
                if( proto=="udp" ) {
                    PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );
                    rte.run = true;
                    PTHREAD_CALL( ::pthread_cond_broadcast(rte.condition) );
                    PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
                }

                // Make sure the local temporaries get reset to 0 (or -1)
                // to prevent deleting/foreclosure
                s             = -1;

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = net2out;
                rte.transfersubmode.clr_all();
                // depending on the protocol...
                if( rte.netparms.protocol=="udp" ) {
                    rte.transfersubmode |= connected_flag;
                    rte.transfersubmode |= run_flag;
                } else if( rte.netparms.protocol=="tcp" )
                    rte.transfersubmode |= wait_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        // <close>
        if( args[1]=="close" ) {
            recognized = true;

            // only accept this command if we're doing
            // net2out
            if( rte.transfermode==net2out ) {
                // Ok. stop the threads
                rte.stop_threads();
                // close the socket(s)
                if( rte.fd )
                    ::close( rte.fd );
                if( rte.acceptfd )
                    ::close( rte.acceptfd );
                // reset the 'state' variables
                rte.fd       = -1;
                rte.acceptfd = -1;
                // And tell the streamstor to stop recording
                // Note: since we call XLRecord() we MUST call
                //       XLRStop() twice, once to stop recording
                //       and once to, well, generically stop
                //       whatever it's doing
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;

                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing net2out yet ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    // If any of the temporaries is non-default, clean up
    // whatever it was
    if( s>=0 )
        ::close(s);
    return reply.str();
}

string in2net_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    ostringstream    reply;

    // store the socket in here.
    // if we create it but cannot start the threads (or something else
    // goes wrong) then the cleanup code at the end will close the socket.
    // *if* the threads are succesfully started make sure you
    // reset this value to <0 to prevent your socket from being
    // closed 
    int              s = -1;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing disk2net - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=in2net ) {
        reply << " 1 : _something_ is happening and its NOT in2net!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.lasthost << " : " << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        if( args[1]=="connect" ) {
            recognized = true;
            // if transfermode is already in2net, we ARE already connected
            // (only in2net::disconnect clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                int          sbuf( rte.netparms.sndbufsize );  
                SSHANDLE     ss( rte.xlrdev.sshandle() );
                unsigned int olen( sizeof(rte.netparms.sndbufsize) );

                // good. pick up optional hostname/ip to connect to
                if( args.size()>2 )
                    rte.lasthost = args[2];

                // create socket and connect 
                s = getsok(rte.lasthost, 2630, rte.netparms.protocol);

                // Set sendbufsize
                ASSERT_ZERO( ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sbuf, olen) );

                // switch off clock
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

                DEBUG(1,"connect: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspendf) << endl);
                notclock = 1;
                DEBUG(1,"connect: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspendf) << endl);

                // now program the streamstor to record from FPDP -> PCI
                XLRCALL( ::XLRSetMode(ss, SS_MODE_PASSTHRU) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetDBMode(ss, SS_FPDP_RECVMASTER, SS_DBOPT_FPDPNRASSERT) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_RECVMASTER, SS_OPT_FPDPNRASSERT) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );

                // before kicking off the threads, transfer some important variables
                // across.
                rte.fd     = s;
                rte.buffer = buffer_type( rte.netparms );

                // goodie. now start the threads disk2mem and mem2net!
                // start_threads() will throw up if something's fishy
                rte.start_threads(fifo2mem, mem2net);

                // Make sure the local temporaries get reset to 0 (or -1)
                // to prevent deleting/foreclosure
                s             = -1;

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = in2net;
                rte.transfersubmode.clr_all();
                // we are connected and waiting for go
                rte.transfersubmode |= connected_flag;
                rte.transfersubmode |= wait_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        // <on> : turn on dataflow
        if( args[1]=="on" ) {
            recognized = true;
            // only allow if transfermode==in2net && submode hasn't got the running flag
            // set (could be restriced to only allow if submode has wait or pause)
            if( rte.transfermode==in2net && (rte.transfersubmode&run_flag)==false ) {
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspend  = rte.ioboard[ mk5areg::SF ];

                // After we've acquired the mutex, we may set the 
                // variable (start) to true, then broadcast the condition.
                PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );

                // now switch on clock
                DEBUG(1,"on: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);
                notclock = 0;
                suspend  = 0;
                DEBUG(1,"on: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);
                rte.run      = true;
                // now broadcast the startcondition
                PTHREAD2_CALL( ::pthread_cond_broadcast(rte.condition),
                               ::pthread_mutex_unlock(rte.mutex) );

                // And we're done, we may release the mutex
                PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
                // indicate running state
                rte.transfersubmode.clr( wait_flag ).clr( pause_flag ).set( run_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or disk2net, nothing else
                if( rte.transfermode==in2net )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        if( args[1]=="off" ) {
            recognized = true;
            // only allow if transfermode==in2net && submode has the run flag
            if( rte.transfermode==in2net && (rte.transfersubmode&run_flag)==true ) {
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspend  = rte.ioboard[ mk5areg::SF ];

                // We don't have to get the mutex; we just turn off the 
                // record clock on the inputboard
                DEBUG(1,"off: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);
                notclock = 1;
                DEBUG(1,"off: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);

                // indicate paused state
                rte.transfersubmode.clr( run_flag ).set( pause_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or disk2net, nothing else
                if( rte.transfermode==in2net )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing disk2net.
            // Don't care if we were running or not
            if( rte.transfermode==in2net ) {
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];

                // turn off clock
                DEBUG(1,"disconnect: notclock: " << hex_t(*notclock) << endl);
                notclock = 1;
                DEBUG(1,"disconnect: notclock: " << hex_t(*notclock) << endl);

                // let the runtime stop the threads
                rte.stop_threads();

                // stop the device
                // As per the SS manual need to call 'XLRStop()'
                // twice: once for stopping the recording
                // and once for stopping the device altogether?
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                // destroy allocated resources
                if( rte.fd>=0 )
                    ::close( rte.fd );
                // reset global transfermode variables 
                rte.fd           = -1;
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing disk2net ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    // If any of the temporaries is non-default, clean up
    // whatever it was
    if( s>=0 )
        ::close(s);
    return reply.str();
}

// the tstat function
string tstat_fn(bool, const vector<string>&, runtime& rte ) {
    double                    dt;
    const double              fifosize( 512 * 1024 * 1024 );
    unsigned long             fifolen;
    ostringstream             reply;
    unsigned long long        bytetomem_cur;
    unsigned long long        bytefrommem_cur;
    static struct timeb       time_cur;
    static struct timeb*      time_last( 0 );
    static unsigned long long bytetomem_last;
    static unsigned long long bytefrommem_last;

    // Take a snapshot of the values & get the time
    bytetomem_cur   = rte.nbyte_to_mem;
    bytefrommem_cur = rte.nbyte_from_mem;
    ftime( &time_cur );
    fifolen = ::XLRGetFIFOLength(rte.xlrdev.sshandle());

    if( !time_last ) {
        time_last  = new struct timeb;
        *time_last = time_cur;
    }

    // Compute 'dt'. If it's too small, do not even try to compute rates
    dt = (time_cur.time + time_cur.millitm/1000.0) - 
         (time_last->time + time_last->millitm/1000.0);

    if( dt>0.1 ) {
        double tomem_rate   = (((double)(bytetomem_cur-bytetomem_last))/(dt*1.0E6))*8.0;
        double frommem_rate = (((double)(bytefrommem_cur-bytefrommem_last))/(dt*1.0E6))*8.0;
        double fifolevel    = ((double)fifolen/fifosize) * 100.0;

        reply << "!tstat = 0 : "
              // dt in seconds
              << format("%6.2lfs", dt) << " "
              // device -> memory rate in 10^6 bits per second
              << rte.tomem_dev << ">M " << format("%8.4lfMb/s", tomem_rate) << " "
              // memory -> device rate in 10^6 bits per second
              << "M>" << rte.frommem_dev << " " << format("%8.4lfMb/s", frommem_rate) << " "
              // and the FIFO-fill-level
              << "F" << format("%4.1lf%%", fifolevel)
              << " ;";
    } else {
        reply << "!tstat = 1 : Retry - we're initialized now ;";
    }

    // Update statics
    *time_last           = time_cur;
    bytetomem_last       = bytetomem_cur;
    bytefrommem_last     = bytefrommem_cur;
    return reply.str();
}

string reset_fn(bool, const vector<string>&, runtime& rte ) {
    rte.reset_ioboard();
    return "!reset = 0 ;";
}

// Expect:
// mode=<markn|tvg>:<ntrack>
string mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;


    // query can always be done
    if( qry ) {
        const inputmode_type& ipm( rte.inputMode() );

        reply << "!" << args[0] << "? 0 : "
              << ipm.mode << " : " << ipm.ntracks << " ;";
        return reply.str();
    }

    // Command only allowed if doing nothing
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "!" << args[0] << "= 3 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    inputmode_type  ipm( inputmode_type::empty );
    outputmode_type opm( outputmode_type::empty );

    reply.str( string() );

    // first argument. the actual 'mode'
    if( args.size()>=2 && args[1].size() ) {
        opm.mode = ipm.mode = args[1];
    }

    // 2nd: number of tracks
    if( args.size()>=3 && args[2].size() ) {
        unsigned long v = ::strtoul(args[2].c_str(), 0, 0);

        if( v<=0 || v>64 )
            reply << "!" << args[0] << "= 8 : ntrack out-of-range (" << args[2] << ") usefull range <0, 64] ;";
        else
            ipm.ntracks = opm.ntracks = (int)v;
    }

    try {
        // set mode to h/w
        rte.inputMode( ipm );
        rte.outputMode( opm );
    }
    catch( const exception& e ) {
        reply << "!" << args[0] << "= 8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << "!" << args[0] << "= 8 : Caught unknown exception! ;";
    }

    // no reply yet indicates "ok"
    if( reply.str().empty() )
        reply << "!" << args[0] << "= 0 ;";
    return reply.str();
}

string playrate_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    if( !qry ) {
        if( args.size()>=3 && args[2].size() ) {
            outputmode_type   opm( outputmode_type::empty );

            opm.freq = ::strtod(args[2].c_str(), 0);
            DEBUG(2, "Setting clockfreq to " << opm.freq << endl);
            rte.outputMode( opm );
        }
        reply << "!" << args[0] << "= 0 ;";
    } else {
        const inputmode_type&  ipm( rte.inputMode() );
        const outputmode_type& opm( rte.outputMode() );

        reply << "!" << args[0] << "? 0 : IP[" << ipm << "] OP[" << opm << "] ;";
    }
    return reply.str();
}



// Expect:
// net_protcol=<protocol>[:<socbufsize>[:<blocksize>[:<nblock>]]
// Note: socbufsize will set BOTH send and RECV bufsize
string net_protocol_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream  reply;
    netparms_type& np( rte.netparms );

    if( qry ) {
        reply << "!" << args[0] << "? 0 : "
              << np.protocol << " : " ;
        if( np.rcvbufsize==np.sndbufsize )
            reply << np.rcvbufsize;
        else
            reply << "Rx " << np.rcvbufsize << ", Tx " << np.sndbufsize;
        reply << " : " << np.blocksize
              << " : " << np.nblock 
              << " ;";
        return reply.str();
    }
    // do not allow to change during transfers
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }
    // Not query. Pick up all the values that are given
    // If len(args)<=1 *and* it's not a query, that's a syntax error!
    if( args.size()<=1 )
        return string("!net_protocol = 3 : Empty command (no arguments given, really) ;");

    // Make sure the reply is RLY empty [see before "return" below why]
    reply.str( string() );

    // See which arguments we got
    // #1 : <protocol>
    if( args.size()>=2 && !args[1].empty() )
        np.protocol = args[1];

    // #2 : <socbuf size> [we set both send and receivebufsizes to this value]
    if( args.size()>=3 && !args[2].empty() ) {
        long int   v = ::strtol(args[2].c_str(), 0, 0);

        // Check if it's a sensible "int" value for size, ie >0 and <=INT_MAX
        if( v<=0 || v>INT_MAX ) {
            reply << "!" << args[0] << " = 8 : <socbuf size> out of range <=0 or >= INT_MAX ; ";
        } else {
            np.rcvbufsize = np.sndbufsize = (int)v;
        }
    }

    // #3 : <workbuf> [the size of the blocks used by the threads]
    //      Value will be adjusted to accomodate an integral number of
    //      datagrams.
    if( args.size()>=4 && !args[3].empty() ) {
        unsigned long int   v = ::strtoul(args[3].c_str(), 0, 0);

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v<=UINT_MAX ) {
            np.blocksize = (unsigned int)v;
        } else {
            reply << "!" << args[0] << " = 8 : <workbufsize> out of range (too large) ;";
        }
    }

    // #4 : <nbuf>
    if( args.size()>=5 && !args[4].empty() ) {
        unsigned long int   v = ::strtoul(args[4].c_str(), 0, 0);

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v>0 && v<=UINT_MAX )
            np.nblock = (unsigned int)v;
        else
            reply << "!" << args[0] << " = 8 : <nbuf> out of range - 0 or too large ;";
    }
    if( args.size()>5 )
        DEBUG(1,"Extra arguments (>5) ignored" << endl);

    // If reply is still empty, the command was executed succesfully - indicate so
    if( reply.str().empty() )
        reply << "!" << args[0] << " = 0 ;";
    return reply.str();
}


// status? [only supports query. Can't be bothered to complain
// if someone calls it as a command]
string status_fn(bool, const vector<string>&, runtime& rte) {
    // flag definitions for readability and consistency
    const unsigned int record_flag   = 0x1<<6; 
    const unsigned int playback_flag = 0x1<<8; 
    // automatic variables
    unsigned int       st;
    ostringstream      reply;

    // compile the hex status word
    st = 1; // 0x1 == ready
    switch( rte.transfermode ) {
        case disk2net:
            st |= playback_flag;
            st |= (0x1<<14); // bit 14: disk2net active
            break;
        case in2net:
            st |= record_flag;
            st |= (0x1<<16); // bit 16: in2net active/waiting
            break;
        case net2out:
            st |= playback_flag;
            st |= (0x1<<17); // bit 17: net2out active
            break;
        default:
            // d'oh
            break;
    }
    reply << "!status? 0 : " << hex_t(st) << " ;";
    return reply.str();
}

string debug_fn( bool , const vector<string>& args, runtime& rte ) {
    rte.ioboard.dbg();
    return string("!")+args[0]+"= 0 ;";
}

const mk5commandmap_type& make_mk5commandmap( void ) {
    static mk5commandmap_type mk5commands = mk5commandmap_type();

    if( mk5commands.size() )
        return mk5commands;

    // Fill the map!
    pair<mk5commandmap_type::iterator, bool>  insres;

    // disk2net
    insres = mk5commands.insert( make_pair("disk2net", disk2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command disk2net into commandmap");

    // in2net
    insres = mk5commands.insert( make_pair("in2net", in2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command in2net into commandmap");

    // net2out
    insres = mk5commands.insert( make_pair("net2out", net2out_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command in2net into commandmap");

    // net_protocol
    insres = mk5commands.insert( make_pair("net_protocol", net_protocol_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command disk2net into commandmap");

    // mode
    insres = mk5commands.insert( make_pair("mode", mode_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command mode into commandmap");

    // play_rate
    insres = mk5commands.insert( make_pair("play_rate", playrate_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command playrate into commandmap");

    // status
    insres = mk5commands.insert( make_pair("status", status_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command status into commandmap");

    // Not official mk5 commands but handy sometimes anyway :)
    insres = mk5commands.insert( make_pair("dbg", debug_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dbg into commandmap");

    insres = mk5commands.insert( make_pair("reset", reset_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command reset into commandmap");

    insres = mk5commands.insert( make_pair("tstat", tstat_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command tstat into commandmap");
    return mk5commands;
}


// Tying properties to a filedescriptor
// For now just the remote host:port adress
typedef map<int, string>   fdprops_type;


// Perform an accept on the given filedescriptor
// and return a fdprops_type::value_type
// which is a pair<filedescriptor,remote-adress>
// with the 'filedescriptor' the newly accepted connection
// and the 'remote-adress' the ... stringified remote
// address.
//
// if anything wonky/fails an exception will be thrown
// so you do not have to check the returnvalue... if you
// get it, it's ok!
//
// Note: caller should make sure that the passed in socket
//       indeed has an incoming connection waiting!
fdprops_type::value_type do_accept_incoming( int fd ) {
    int                afd;
    int                fmode;
    unsigned int       slen( sizeof(struct sockaddr_in) );
    ostringstream      strm;
    struct sockaddr_in remote;
    // somebody knockin' on the door!
    // Do the accept but do not let the system throw - it would
    // kill all other running stuff... 
    ASSERT_POS( (afd=::accept(fd, (struct sockaddr*)&remote, &slen)) );

    // Put the socket in blockig mode
    fmode  = ::fcntl(afd, F_GETFL);
    fmode &= ~O_NONBLOCK;
    ASSERT_ZERO( ::fcntl(afd, F_SETFL, fmode) );

    // Get the remote adress/port
    strm << inet_ntoa(remote.sin_addr) << ":" << ntohs(remote.sin_port);

    return make_pair(afd, strm.str());
}


void Usage( const char* name ) {
    cout << "Usage: " << name << " [-h] [-m <messagelevel>] [-c <cardnumber>]" << endl
         << "   defaults are: messagelevel " << dbglev_fn() << " and card #1" << endl;
    return;
}


#define KEES(a,b) \
    case b: a << #b; break;
ostream& operator<<( ostream& os, const S_BANKSTATUS& bs ) {
    os << "BANK[" << bs.Label << "]: ";

    // bank status
    switch( bs.State ) {
        KEES(os, STATE_READY);
        KEES(os, STATE_NOT_READY);
        KEES(os, STATE_TRANSITION);
        default:
            os << "<Invalid state #" << bs.State << ">";
            break;
    }
    os << ", " << ((bs.Selected)?(""):("not ")) << "selected";
    os << ", " << ((bs.WriteProtected)?("ReadOnly "):("R/W "));
    os << ", ";
    // Deal with mediastatus
    switch( bs.MediaStatus ) {
        KEES(os, MEDIASTATUS_EMPTY);
        KEES(os, MEDIASTATUS_NOT_EMPTY);
        KEES(os, MEDIASTATUS_FULL);
        KEES(os, MEDIASTATUS_FAULTED);
        default:
            os << "<Invalid media status #" << bs.MediaStatus << ">";
    }
    return os;
}

// main!
int main(int argc, char** argv) {
    char            option;
    UINT            devnum( 1 );
    unsigned int    numcards;

    // Check commandline
    while( (option=::getopt(argc, argv, "hm:c:"))>=0 ) {
        switch( option ) {
            case 'h':
                Usage( argv[0] );
                return -1;
            case 'm': {
                    long int v = ::strtol(optarg, 0, 0);
                    // check if it's too big for int
                    if( v<INT_MIN || v>INT_MAX ) {
                        cerr << "Value for messagelevel out-of-range.\n"
                             << "Usefull range is: [" << INT_MIN << ", " << INT_MAX << "]" << endl;
                        return -1;
                    }
                    dbglev_fn((int)v);
                }
                break;
            case 'd': {
                    long int v = ::strtol(optarg, 0, 0);
                    // check if it's out-of-range for UINT
                    if( v<0 || v>INT_MAX ) {
                        cerr << "Value for devicenumber out-of-range.\n"
                             << "Usefull range is: [0, " << INT_MAX << "]" << endl;
                        return -1;
                    }
                    devnum = ((UINT)v);
                }
                break;
            default:
                cerr << "Unknown option '" << option << "'" << endl;
                return -1;
        }
    }

    // Start looking for cards
    if( (numcards=::XLRDeviceFind())==0 ) {
        cout << "No XLR Cards found? Is the driver loaded?" << endl;
        return 1;
    }
    cout << "Found " << numcards << " StreamStorCard" << ((numcards>1)?("s"):("")) << endl;

    try {
        int           listensok;
        runtime       environment;
        fdprops_type  acceptedfds;

        environment.xlrdev = xlrdevice( devnum );
        cout << environment.xlrdev << endl;

        // Ignore sigpipe
        ASSERT_COND( signal(SIGPIPE, SIG_IGN)!=SIG_ERR );
        // Catch SIGINT
        ASSERT_COND( signal(SIGINT, dostop)!=SIG_ERR );

        // can use returnvalue blindly as the getsok() will
        // throw if no socket can be created
        listensok = getsok( 2620, "tcp" );
        DEBUG(2, "Start main loop, waiting for incoming connections" << endl);
        // Accept incoming connections and handle commands
        done = false;
        while( !done ) {
            // Always poll one more (namely the listening socket)
            // than the number of accepted sockets.
            // If the "acceptfd" datamember of the runtime is >=0
            // it is assumed that it is waiting for an incoming
            // (data) connection. Add it to the list of
            // of fd's to poll as well.
            // 'cmdsockoffs' is the offset into the "struct pollfd fds[]"
            // variable at which the oridnary command connections start.
            // It's either 1 or 2 [depending on whether or not the
            // acceptfd datamember was >= 0].
            const bool                   doaccept( environment.acceptfd>=0 );
            const unsigned int           nrfds( 1 + (doaccept?1:0) + acceptedfds.size() );
            const unsigned int           cmdsockoffs( (doaccept?2:1) );
            // non-const stuff
            short                        events;
            unsigned int                 idx;
            struct pollfd                fds[ nrfds ];
            fdprops_type::const_iterator curfd;

            // Position '0' is always used for the listeningsocket
            fds[0].fd     = listensok;
            fds[0].events = POLLIN|POLLPRI|POLLERR|POLLHUP;

            // Position '1' is reserved for incoming (data)connection, if any
            if( doaccept ) {
                fds[1].fd     = environment.acceptfd;
                fds[1].events = POLLIN|POLLPRI|POLLERR|POLLHUP;
            }

            // Loop over the accepted connections
            for(idx=cmdsockoffs, curfd=acceptedfds.begin();
                curfd!=acceptedfds.end(); idx++, curfd++ ) {
                fds[idx].fd     = curfd->first;
                fds[idx].events = POLLIN|POLLPRI|POLLERR|POLLHUP;
            }
            DEBUG(4, "Polling for events...." << endl);

            // Wait forever for something to happen - this is
            // rilly the most efficient way of doing things
            int   pres = ::poll(&fds[0], nrfds, -1);
            // interrupted systemcall is not a real error, we 
            // want to see if we need to terminate. we treat it
            // as logically equivalent to a timeout
            if( pres<0 && errno==EINTR )
                pres = 0;
            if( pres<0 ) {
                lastsyserror_type   lse;
                ostringstream       oss;
                oss << lse;
                throw syscallexception(oss.str());
            }
            // if timeout, try again
            if( pres==0 )
                continue;

            // check who's requested something!
            // #0 is alwasy special..
            if( (events=fds[0].revents)!=0 ) {
                // Ok revents not zero: something happened!
                DEBUG(3, "listensok got " << eventor(events) << endl);

                // If the socket's been hung up (can that happen?)
                // we just stop processing commands
                if( events&POLLHUP || events&POLLERR ) {
                    cerr << "Detected hangup of listensocket. Closing down." << endl;
                    done = true;
                    continue;
                }
                if( events&POLLIN ) {
                    // do_accept_incoming may throw but we don't want to shut
                    // down the whole app upon failure of *that*
                    try {
                        fdprops_type::value_type          v( do_accept_incoming(fds[0].fd) );
                        pair<fdprops_type::iterator,bool> insres;

                        // And add it to the vector of filedescriptors to watch
                        insres = acceptedfds.insert( v );
                        if( !insres.second ) {
                            cerr << "Failed to insert entry into acceptedfds for connection from "
                                << v.second << "!?";
                            ::close( v.first );
                        } else {
                            DEBUG(2, "Incoming on fd#" << v.first << " " << v.second << endl);
                        }
                    }
                    catch( const exception& e ) {
                        cerr << "Failed to accept incoming connection: " << e.what() << endl;
                    }
                    catch( ... ) {
                        cerr << "Unknown exception whilst trying to accept incoming connection?!" << endl;
                    }
                }
                // Done dealing with the listening socket
            }

            // #1 *may* be special
            if( doaccept && ((events=fds[1].revents)!=0) ) {
                DEBUG(2, "data-listen-socket (" << fds[1].fd << ") got " << eventor(events) << endl);
                if( events&POLLHUP || events&POLLERR ) {
                    DEBUG(0,"Detected hangup of data-listen-socket?!?!" << endl);
                    ::close( environment.acceptfd );
                    environment.acceptfd = -1;
                } else if( events&POLLIN ) {
                    // do_accept_incoming may throw but we don't want to shut
                    // down the whole app upon failure of *that*
                    try {
                        fdprops_type::value_type    v( do_accept_incoming(fds[1].fd) );

                        // Ok. that went fine.
                        // Now close the listening socket, transfer the accepted fd
                        // to the runtime environment and tell any waiting thread(z)
                        // that it's ok to go!
                        ::close( environment.acceptfd );
                        environment.acceptfd = -1;

                        PTHREAD_CALL( ::pthread_mutex_lock(environment.mutex) );
                        // update transfersubmode state change:
                        // from WAIT -> (RUN, CONNECTED)
                        environment.transfersubmode.clr( wait_flag ).set( run_flag ).set( connected_flag );
                        environment.fd       = v.first;
                        environment.run      = true;
                        PTHREAD_CALL( ::pthread_cond_broadcast(environment.condition) );
                        PTHREAD_CALL( ::pthread_mutex_unlock(environment.mutex) );
                        DEBUG(1, "Incoming data connection on fd#" << v.first << " " << v.second << endl);
                    }
                    catch( const exception& e ) {
                        cerr << "Failed to accept incoming data connection: " << e.what() << endl;
                    }
                    catch( ... ) {
                        cerr << "Unknown exception whilst trying to accept incoming data connection?!" << endl;
                    }
                }
            }

            // On all other sockets, loox0r for commands!
            const mk5commandmap_type&   mk5cmds = make_mk5commandmap();

            for( idx=cmdsockoffs; idx<nrfds; idx++ ) {
                int                    fd( fds[idx].fd );
                fdprops_type::iterator fdptr;

                // If no events, nothing to do!
                if( (events=fds[idx].revents)==0 )
                    continue;

                DEBUG(3, "fd#" << fd << " got " << eventor(events) << endl);

                // Find it in the list
                if(  (fdptr=acceptedfds.find(fd))==acceptedfds.end() ) {
                    // no?!
                    cerr << "Internal error. FD#" << fd
                        << " is in pollfds but not in acceptedfds?!" << endl;
                    continue;
                }

                // if error occurred or hung up: close fd and remove from
                // list of fd's to monitor
                if( events&POLLHUP || events&POLLERR ) {
                    DEBUG(2, "Detected HUP/ERR on fd#" << fd << " [" << fdptr->second << "]" << endl);
                    ::close( fd );

                    acceptedfds.erase( fdptr );
                    // Move on to checking next FD
                    continue;
                }

                // if stuff may be read, see what we can make of it
                if( events&POLLIN ) {
                    char                           linebuf[4096];
                    char*                          sptr;
                    char*                          eptr;
                    string                         reply;
                    ssize_t                        nread;
                    vector<string>                 commands;
                    vector<string>::const_iterator curcmd;

                    // attempt to read a line
                    nread = ::read(fd, linebuf, sizeof(linebuf));

                    // if <=0, socket was closed, remove it from the list
                    if( nread<=0 ) {
                        if( nread<0 ) {
                            lastsyserror_type lse;
                            DEBUG(0, "Error on fd#" << fdptr->first << " ["
                                 << fdptr->second << "] - " << lse << endl);
                        }
                        ::close( fdptr->first );
                        acceptedfds.erase( fdptr );
                        continue;
                    }
                    // make sure line is null-byte terminated
                    linebuf[ nread ] = '\0';

                    // strip all whitespace and \r and \n's.
                    // As per VSI/S, embedded ws is 'illegal'
                    // and leading/trailing is insignificant...
                    sptr = eptr = linebuf;
                    while( *sptr ) {
                        if( ::strchr(" \r\n", *sptr)==0 )
                            *eptr++ = *sptr;
                        sptr++;
                    }
                    // eptr is the new end-of-string
                    *eptr = '\0';

                    commands = ::split(string(linebuf), ';');
                    if( commands.size()==0 )
                        continue;

                    // process all commands
                    // Even if we did receive only whitespace, we still need to
                    // send back *something*. A single ';' for an empty command should
                    // be just fine
                    for( curcmd=commands.begin(); curcmd!=commands.end(); curcmd++ ) {
                        bool                               qry;
                        string                             keyword;
                        const string&                      cmd( *curcmd );
                        vector<string>                     args;
                        string::size_type                  posn;
                        mk5commandmap_type::const_iterator cmdptr;

                        if( cmd.empty() ) {
                            reply += ";";
                            continue;
                        }
                        // find out if it was a query or not
                        if( (posn=cmd.find_first_of("?="))==string::npos ) {
                            reply += ("!syntax = 7 : Not a command or query;");
                            continue;
                        }
                        qry     = (cmd[posn]=='?');
                        keyword = ::tolower( cmd.substr(0, posn) );
                        if( keyword.empty() ) {
                            reply += "!syntax = 7 : No keyword given ;";
                            continue;
                        }

                        // see if we know about this specific command
                        if( (cmdptr=mk5cmds.find(keyword))==mk5cmds.end() ) {
                            reply += (string("!")+keyword+((qry)?('?'):('='))+" 7 : ENOSYS - not implemented ;");
                            continue;
                        }

                        // now get the arguments, if any
                        // (split everything after '?' or '=' at ':'s and each
                        // element is an argument)
                        args = ::split(cmd.substr(posn+1), ':');
                        // stick the keyword in at the first position
                        args.insert(args.begin(), keyword);

                        try {
                            reply += cmdptr->second(qry, args, environment);
                        }
                        catch( const exception& e ) {
                            reply += string("!")+keyword+" = 4 : " + e.what() + ";";
                        }
                    }
                    // processed all commands in the string
                    // send a reply
                    DEBUG(2,"Reply: " << reply << endl);
                    // do *not* forget the \r\n ...!
                    reply += "\r\n";
                    ASSERT_COND( ::write(fd, reply.c_str(), reply.size())==(ssize_t)reply.size() );
                }
                // done with this fd
            }
            // done all fds
        }
        DEBUG(1, "Closing listening socket (ending program)" << endl);
        ::close( listensok );
        // the destructor of the runtime will take care of stopping threads if they are
        // runninge...
    }
    catch( const exception& e ) {
        cout << "!!!! " << e.what() << endl;
    }
    catch( ... ) {
        cout << "caught unknown exception?!" << endl;
    }

    return 0;
}
#if 0
                    char                           linebuf[ 2048 ];
                    string                         reply;
                    ssize_t                        nread;
                    vector<string>                 commands;
                    vector<string>::const_iterator curcmd;;


                    // before going on, strip all whitespace and \n's and \r's
                    // this time we strip the embedded ones (trailing have already
                    // been removed)
                    // Reason: as per VSI/S, embedded whitespaces are basically illegal
                    // anyway and leading/trailing is not significant..
                    startptr = endptr = linebuf;
                    while( (c=*startptr)!=0 ) {
                        if( !strchr(" \t\n\r", c) )
                            *endptr++ = c;
                        startptr++;
                    }
                    *endptr   = '\0';
                    if( !::strlen(linebuf) )
                        continue;
                    DEBUG(2, fdptr->second << " said '" << linebuf << "'" << endl);

                    // Find all commands (separated by ';' s)
                    commands = ::split(string(linebuf), ';');
                    for( curcmd=command.begin(); curcmd!=commands.end(); curcmd++ ) {
                        if( curcmd->empty() )
                            continue;
                        bool                               qry;
                        string::size_type                  posn;
#if 0
                        char*                              last;
                        char*                              ptr;
#endif
                        string                             keyword;
                        vector<string>                     parts;
                        mk5commandmap_type::const_iterator cmdptr;

                        // endptr points at the semicolon. Replace it with a '\0'
                        *endptr = '\0';

                        DEBUG(3,"*** CMD: '" << startptr << "'" << endl)

                        // Check if there's a command at all
                        if( !strlen(startptr) ) {
                            // apparently not, try if something's following
                            startptr = endptr + 1;
                            continue;
                        }
                        // we expect:
                        // <keyword>[=?](:<value>)*

                        // Before we use strtok we must remember if it was
                        // '?' or '=' (strtok will overwrite the separator char
                        // with '\0' ...)
                        qry = (::strchr(startptr, '?')!=0);
                        // !qry is fine, as long as there is an equalsign.
                        // if !qry and no equalsign found => syntaxerror!
                        if( !qry && ::strchr(startptr,'=')==0 ) {
                            reply += "!syntax = 1 : No '?' or '=' ;";
                            startptr = endptr + 1;
                            continue;
                        }
                        // extract the keyword first! No need to check returnvalue;
                        // we have ascertained that there is at least a '?' or '='
                        // in the string...
                        keyword = string( strtok_r(startptr, "?=", &last) );

                        // Now that we know the keyword, we may as well check if
                        // we recognize it at all! If we don't we don't even bother
                        // parsing the rest of the command 
                        if( (cmdptr=mk5cmds.find(keyword))==mk5cmds.end() ) {
                            reply += (string("!")+keyword+((qry)?('?'):('='))+" 7 : ENOSYS - not implemented ;");
                            startptr = endptr + 1;
                            continue;
                        }

                        // Now find all the arguments, they're separated by ':' s
                        parts.push_back( keyword );
                        while( (ptr=::strtok_r(0, ":", &last))!=0 )
                            parts.push_back( string(ptr) );

                        // And call the commandfunction!
                        reply += cmdptr->second(qry, parts, environment);

                        // And do *not* forget to update loop parameters D'OH!
                        startptr = endptr+1;
                    }
                    DEBUG(2, "Reply: " << reply << endl);
                    // Done all commands. Terminate the reply with a newline and send it off
                    if( reply.size() ) {
                        reply += '\n';
                        ::write( fds[idx].fd, reply.c_str(), reply.size() );
                    }
#endif
