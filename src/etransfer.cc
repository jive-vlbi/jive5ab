// Support for transferring directly to e-transfer daemon https://github.com/jive-vlbi/etransfer
// Copyright (C) 2007-2019 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#if ETRANSFER

#include <etransfer.h>
#include <dosyscall.h>
#include <libvbs.h>
#include <sciprint.h>
#include <pthread.h>
#include <mutex_locker.h>


DEFINE_EZEXCEPT(etransfer_exception)

etransfer_state::etransfer_state():
    rteptr( nullptr )
{}

etransfer_state::etransfer_state(runtime* p) :
    rteptr( p )
{ EZASSERT2(rteptr, etransfer_exception, EZINFO("Attempt to construct etransfer_state with NULL runtime pointer?!!")); }

///////////////////////////////////////////////////////
//
// This struct adapts access to the StreamStor card 
// into an etdc_fd file descriptor entity.
// There is no actual file descriptor but it can mimick
// the reads & seeks.
//
////////////////////////////////////////////////////////

// We keep a static streamstor_reader_type()
// Only one runtime can have the disk2etransfer function
// the upper level code makes sure no etd_streamstor_reader() can be
// constructed before another one is finished
// so we can get awway with that
streamstor_reader_ptr  etd_streamstor_fd::__m_ssReader;
int64_t                etd_streamstor_fd::__m_start  = 0;
int64_t                etd_streamstor_fd::__m_offset = 0;
int64_t                etd_streamstor_fd::__m_end    = 0;
pthread_mutex_t        __m_read_close_mtx            = PTHREAD_MUTEX_INITIALIZER;

// the first two arguments are from open(2) "path" and "open mode" and both
// have no meaning for this one
etd_streamstor_fd::etd_streamstor_fd(SSHANDLE h, const playpointer& start, const playpointer& end):
    etdc::etdc_fd()
{
    constexpr int64_t  maxPlayPointer = std::numeric_limits<int64_t>::max();

    // Make sure the start, end fit within int64_t
    EZASSERT2( start.Addr<maxPlayPointer && end.Addr<maxPlayPointer, streamstor_reader_bounds_except,
               EZINFO("start (" << start.Addr << ") or end (" << end.Addr << ") playpointer >=" << maxPlayPointer) );
    // see below - the higher level code should make sure this
    // one doesn't get constructed more than once
    __m_ssReader = std::make_shared<streamstor_reader_type>(h, start, end);
    __m_start    = start.Addr;
    __m_offset   = 0;
    __m_end      = end.Addr;
    // The 'file descriptor' ...
    __m_fd       = static_cast<int>(h);

    // Update our read/seek functions
    etdc::update_fd(*this, etdc::read_fn(&etd_streamstor_fd::read),
          etdc::close_fn(&etd_streamstor_fd::close),
          etdc::getsockname_fn(&etd_streamstor_fd::getsockname), // peer, sock name == same for this one
          etdc::getpeername_fn(&etd_streamstor_fd::getsockname),
          etdc::lseek_fn(&etd_streamstor_fd::lseek));
}

etd_streamstor_fd::~etd_streamstor_fd()
{ __m_ssReader.reset(); }


/// Here should be static methods
ssize_t etd_streamstor_fd::read(int, void* buf, size_t n) {
    // grab mutex so we know we don't get closed whilst we're executin'
    mutex_locker   lockert( __m_read_close_mtx );
    // Signal end-of-file by returning 0
    if( __m_start + __m_offset >= __m_end )
        return 0;
    __m_ssReader->read_into(static_cast<unsigned char*>(buf), __m_offset, n);
    __m_offset += n;
    return (ssize_t)n;
}

int etd_streamstor_fd::close(int) {
    // wait until the read's finished
    mutex_locker   lockert( __m_read_close_mtx );
    __m_start  = __m_end;
    __m_offset = 0;
    return 0;
}

off_t etd_streamstor_fd::lseek(int, off_t offset, int whence) {
    int64_t   newOffset;
    switch( whence ) {
        case SEEK_SET:
            newOffset = offset;
            break;

        case SEEK_END:
            newOffset = __m_end + offset;
            break;

        case SEEK_CUR:
            newOffset = __m_offset + offset;
            break;

        default:
            errno = EINVAL;
            return (off_t)-1;
    }
    if( newOffset < __m_start || newOffset > __m_end ) {
        errno = EINVAL;
        return (off_t)-1;
    }
    __m_offset = newOffset;
    return  __m_offset;
}

etdc::sockname_type etd_streamstor_fd::getsockname(int) {
    static char hostName[256] = {0,};
    if( hostName[0]==0 )
        ASSERT_ZERO( ::gethostname(hostName, sizeof(hostName)-1) );
    return mk_sockname("streamstor", hostName, etdc::port_type(static_cast<short>(0)));
}


/////////////////////////////////////////////////////////////////////////
//
//  Adapt the vbs reader into an etdc_fd file descriptor
//  The libvs gives us a 'file descriptor' and has read/seek
//  defined on these file descriptors so mapping should be easy
//
/////////////////////////////////////////////////////////

etd_vbs_fd::etd_vbs_fd(std::string const& scan, mountpointlist_type const& mps): etdc_fd(),
    __m_scanName( scan )
{
    // Initialize libvbs
    // To that effect we must transform the mountpoint list into an array of
    // char*. Now that we're in C++11 happy land we can do this:
    std::unique_ptr<char const*[]>      vbsdirs(new char const*[mps.size()+1]);
    mountpointlist_type::const_iterator curmp = mps.begin();

    // Get array of "char*" and add a terminating 0 pointer
    for(unsigned int i=0; i<mps.size(); i++, curmp++)
        vbsdirs[i] = curmp->c_str();
    vbsdirs[ mps.size() ] = 0;

    // Now we can (try to) open the recording and get the length by seeking
    // to the end. Do not forget to put file pointer back at start doofus!
    int        fd1 = ::mk6_open(__m_scanName.c_str(), &vbsdirs[0]);
    int        fd2 = ::vbs_open(__m_scanName.c_str(), &vbsdirs[0]);
    const bool fd1ok( fd1>=0 ), fd2ok( fd2>=0 );

    // Exactly one of those fd's should be non-negative
    if( fd1ok==fd2ok ) {
        std::ostringstream oss;
        // Either neither or both exist, neither of which is a sign of Good
        if( fd1ok ) {
            ::vbs_close( fd1 );
            ::vbs_close( fd2 );
            oss << "'" << __m_scanName << "' exists in both VBS and Mk6 formats";
        } else {
            oss << "'" << __m_scanName << "' does not exist in either VBS nor Mk6 format";
        }
        throw etransfer_exception(oss.str());
    }

    // Pick the file descriptor that succesfully opened
    this->etdc_fd::__m_fd = fd1ok ? fd1 : fd2;

    // We must set out pointers to memberfunctions
    etdc::update_fd(*this, etdc::read_fn(&::vbs_read),
          etdc::lseek_fn(&::vbs_lseek), etdc::close_fn(&::vbs_close),
          // the close is done by the destructor of the unique-ptr so turn
          // into a succesfull no-op
          etdc::getsockname_fn(&etd_vbs_fd::getsockname), // peer, sock name == same for this one
          etdc::getpeername_fn(&etd_vbs_fd::getsockname));
}

etdc::sockname_type etd_vbs_fd::getsockname(int) {
    static char hostName[256] = {0,};
    if( hostName[0]==0 )
        ASSERT_ZERO( ::gethostname(hostName, sizeof(hostName)-1) );
    return mk_sockname("vbs", hostName, etdc::port_type(static_cast<short>(0)));
}

etd_vbs_fd::~etd_vbs_fd(){
}


//////////////////////////////////////////////////////////////////////////////////////
//
//            The thread functions, cancel and cleanup
//
//////////////////////////////////////////////////////////////////////////////////////

void etransfer_fd_read(outq_type<block>* oq, sync_type<etransfer_state_ptr>* data) {
    etransfer_state_ptr  state{ *data->userdata };
    etdc::etdc_fdptr     fd{ state->src_fd };
    netparms_type const& netparms{ state->rteptr->netparms };

    state->pool   = std::unique_ptr<blockpool_type>(new blockpool_type{netparms.get_blocksize(), netparms.nblock});
    state->sender = std::unique_ptr<pthread_t>(new pthread_t{ ::pthread_self() });
    
    install_zig_for_this_thread(SIGUSR1);

    DEBUG(3, "etransfer_fd_read: start reading from fd#" << fd->__m_fd << std::endl);
    while( state->fpCur<state->fpEnd ) {
        block         tmp{ state->pool->get() };
        size_t const  n2Read{ std::min( tmp.iov_len, size_t(state->fpEnd - state->fpCur)) };
        ssize_t const nRead = fd->read(fd->__m_fd, tmp.iov_base, n2Read);

        ETDCASSERT((size_t)nRead==n2Read, "Failed to read " << n2Read << " bytes - " << (nRead<0 ? evlbi5a::strerror(errno) : " closed"));

        if( !oq->push(n2Read==tmp.iov_len ? tmp : tmp.sub(0, n2Read)) )
            break;
        state->fpCur += nRead;
    }
    DEBUG(3, "etransfer_fd_read: done" << std::endl);
}


void etransfer_fd_write(inq_type<block>* iq, sync_type<etransfer_state_ptr>* data) {
    block                b;
    etransfer_state_ptr  state  = *data->userdata;
    etdc::etdc_fdptr     fd     = state->dst_fd;
    runtime*             rteptr = state->rteptr;
    auto const           uuid   = etdc::get_uuid(*(state->dstResult));

    RTEEXEC(*rteptr,
            rteptr->statistics.init(data->stepid, "etransfer_write"));

    counter_type&   counter( rteptr->statistics.counter(data->stepid) );

    DEBUG(3, "etransfer_fd_write: start writing to fd " << fd->__m_fd << " " << fd->getpeername(fd->__m_fd) << std::endl);

    while( iq->pop(b) ) {
        size_t             nWritten{ 0 };
        unsigned char*     buffer{ static_cast<unsigned char*>(b.iov_base) };
        std::ostringstream msg_buf;

        msg_buf << "{ uuid:" << uuid << ", sz:" << b.iov_len << "}";

        const std::string  msg( msg_buf.str() );
        ETDCASSERT(fd->write(fd->__m_fd, msg.data(), msg.size())==static_cast<ssize_t>(msg.size()),
                   "Failed to send the data header to the remote daemon");

        // Keep on writing untill all bytes that were read are actually written
        while( nWritten<b.iov_len ) {
            ssize_t const thisWrite = fd->write(fd->__m_fd, &buffer[nWritten], b.iov_len-nWritten);

            if( thisWrite<=0 )
                break;
            nWritten += thisWrite;
            counter  += thisWrite;
        }
        if( nWritten<b.iov_len )
            break;
        char    ack;
        DEBUG(4, "etransfer_fd_write: waiting for remote ACK ..." << std::endl);
        fd->read(fd->__m_fd, &ack, 1);
        DEBUG(4, "etransfer_fd_write: ... got it" << std::endl);
    }
    DEBUG(3, "etransfer_fd_write: wrote " << counter << " (" << byteprint((double)counter, "byte") << ")" << std::endl);
}

void cancel_etransfer(etransfer_state_ptr* pp) {
    etransfer_state_ptr p( *pp) ;

    if( p->src_fd ) {
        if( p->src_fd->__m_fd!=-1 ) {
            p->src_fd->close( p->src_fd->__m_fd );
            DEBUG(3, "cancel_etransfer: closed fd#" << p->src_fd->__m_fd << std::endl);
        }
        p->src_fd->__m_fd = -1;
    }
    if( p->sender ) {
        auto rv = ::pthread_kill( *p->sender, SIGUSR1 );
        if( rv!=0 && rv!=ESRCH )
            DEBUG(-1, "cancel_etransfer: FAILED to SIGNAL THREAD - " << evlbi5a::strerror(rv) << std::endl);
        p->sender = nullptr;
    }
}

void etransfer_cleanup(etransfer_state_type::iterator p) {
    runtime*            rteptr( p->first );
    etransfer_state_ptr state( p->second );

    DEBUG(2, "Cleaning up etransfer " << p->second->scanName << std::endl);
    try {
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr_all() );

        // close the etransfer file descriptors
        state->src_fd->close(state->src_fd->__m_fd);
        state->dst_fd->close(state->dst_fd->__m_fd);

        // And if a request file write on the remote end was succesfull, we can now remove it
        if( state->dstResult ) {
            // The removeUUID seems to throw easily but we want to do more 
            // cleaning up so transform it into a warning
            try { state->remote_proxy->removeUUID( etdc::get_uuid(*(state->dstResult)) ); }
            catch( ... ) { DEBUG(-1, "etransfer(vbs) cleanup: failed to remove UUID " << etdc::get_uuid(*(state->dstResult)) << std::endl); }
        }

        // Nor do we need the memory &cet anymore
        state->dstResult    = nullptr;
        state->remote_proxy = nullptr;
        state->sender       = nullptr;
        state->pool         = nullptr;
        state->src_fd       = nullptr;
        state->dst_fd       = nullptr;
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "etransfer(vbs) finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "etransfer(vbs) finalization threw an unknown exception" << std::endl );        
    }
    // It is of paramount importance that the runtime's transfermode 
    // gets rest to idle, even in the face of exceptions and lock failures
    // and whatnots
    p->second = nullptr;
    rteptr->transfermode = no_transfer;
    rteptr->transfersubmode.clr( run_flag );
    DEBUG(3, "etransfer(vbs) finalization done." << std::endl);
}



#endif // ETRANSFER
