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


DEFINE_EZEXCEPT(etransfer_exception)

etdc::etd_state etdState{};


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
streamstor_reader_ptr  etd_streamstor_reader::__m_ssReader;
int64_t                etd_streamstor_reader::__m_start  = 0;
int64_t                etd_streamstor_reader::__m_offset = 0;
int64_t                etd_streamstor_reader::__m_end    = 0;

// the first two arguments are from open(2) "path" and "open mode" and both
// have no meaning for this one
etd_streamstor_reader::etd_streamstor_reader(std::string const&, int, SSHANDLE h, const playpointer& start, const playpointer& end):
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

    // Update our read/seek functions
    etdc::update_fd(*this, etdc::read_fn(&etd_streamstor_reader::read),
          etdc::close_fn(&etd_streamstor_reader::close),
          etdc::getsockname_fn(&etd_streamstor_reader::getsockname), // peer, sock name == same for this one
          etdc::getpeername_fn(&etd_streamstor_reader::getsockname),
          etdc::lseek_fn(&etd_streamstor_reader::lseek));
}

etd_streamstor_reader::~etd_streamstor_reader()
{ __m_ssReader.reset(); }


/// Here should be static methods
ssize_t etd_streamstor_reader::read(int, void* buf, size_t n) {
    // Signal end-of-file by returning 0
    if( __m_start + __m_offset >= __m_end )
        return 0;
    // Make sure we're reading multiples of 8
    uint64_t  r = __m_ssReader->read_into(static_cast<unsigned char*>(buf), __m_offset, n);
    __m_offset += r;
    return (ssize_t)r;
}

int etd_streamstor_reader::close(int) {
    return 0;
}

off_t etd_streamstor_reader::lseek(int, off_t offset, int whence) {
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
    if( newOffset < __m_start ) {
        errno = EINVAL;
        return (off_t)-1;
    }
    __m_offset = newOffset;
    return  __m_offset;
}

etdc::sockname_type etd_streamstor_reader::getsockname(int) {
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

// the first two arguments are from open(2) "path" and "open mode", the
// latter which has no meaning for this one
etd_vbs_fd::etd_vbs_fd(std::string const& scan, int, mountpointlist_type const& mps): etd_vbs_fd(scan, mps)
{}

etd_vbs_fd::etd_vbs_fd(std::string const& scan, mountpointlist_type const& mps): etdc_fd(),
    __m_scanName( scan )//,
    //__m_vbsReader(new vbs_reader_base(__m_scanName, mpl, 0, 0, vbs_reader_base::try_both))
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


///////////////////////////////////////////////////////////////////////////////////
//
// We override the ETDServer class to have the 
//     virtual requestFileRead() 
// return UUIDs that map to etdc_fd-derived instances of the streamstor or vbs reader
//
///////////////////////////////////////////////////////////////////////////////////


ETD5abServer::~ETD5abServer() {}

etdc::result_type ETD5abServer::requestFileRead(std::string const& s, off_t) {
    THROW_EZEXCEPT(etransfer_exception, "requestFileRead(" << s << ") - Not supposed to be called on ETD5abServer!");
}
#if 0
etdc::filelist_type ETD5abServer::listPath(std::string const&, bool) const {
   return etdc::filelist_type{};
}

etdc::result_type ETD5abServer::requestFileWrite(std::string const& s, etdc::openmode_type) {
    THROW_EZEXCEPT(etransfer_exception, "requestFileWrite(" << s << ") - Not supposed to be called on ETD5abServer!");
}

etdc::result_type ETD5abServer::requestFileRead(std::string const& s, off_t) {
    THROW_EZEXCEPT(etransfer_exception, "requestFileRead(" << s << ") - Not supposed to be called on ETD5abServer!");
}

etdc::dataaddrlist_type ETD5abServer::dataChannelAddr( void ) const {
    return etdc::dataaddrlist_type{};
}
        virtual etdc::xfer_result   getFile (etdc::uuid_type const& /*srcUUID*/, etdc::uuid_type const& /*dstUUID*/,
                                             off_t /*todo*/, etdc::dataaddrlist_type const& /*remote*/);

#endif
etdc::xfer_result ETD5abServer::sendFile(etdc::uuid_type const& /*srcUUID*/, etdc::uuid_type const& /*dstUUID*/,
        off_t /*todo*/, etdc::dataaddrlist_type const& /*remote*/) {
    return etdc::xfer_result{false, 0, "Implementation waiting", std::chrono::seconds(0)};
}

bool ETD5abServer::removeUUID(etdc::uuid_type const& uuid) {
    ETDCASSERT(uuid==__m_uuid, "Cannot remove someone else's UUID!");

    // We need to do some thinking about locking sequence because we need
    // a lock on the shared state *and* a lock on the transfer
    // before we can attempt to remove it.
    // To prevent deadlock we may have to relinquish the locks and start again.
    // What that means is that if we fail to lock both atomically, we must start over:
    //  lock shared state and (attempt to) find the transfer
    // because after we've released the shared state lock, someone else may have snuck in
    // and deleted or done something bad with the transfer i.e. we cannot do a ".find(uuid)" once 
    // and assume the iterator will remain valid after releasing the lock on shared_state
    etdc::etd_state&                          shared_state( __m_shared_state.get() );
    std::unique_ptr<etdc::transferprops_type> removed;
    while( true ) {
        // 1. lock shared state
        std::unique_lock<std::mutex>     lk( shared_state.lock );
        // 2. find if there is an entry in the map for us
        etdc::transfermap_type::iterator ptr = shared_state.transfers.find(__m_uuid);
        
        // No? OK then we're done
        if( ptr==shared_state.transfers.end() )
            return false;

        // Now we must do try_lock on the transfer - if that fails we sleep and start from the beginning
        //std::unique_lock<std::mutex>     sh( *ptr->second.lockPtr, std::try_to_lock );
        std::unique_lock<std::mutex>     sh( ptr->second->lock, std::try_to_lock );
        if( !sh.owns_lock() ) {
            // we must release the lock on shared state before sleeping
            // for a bit or else no-one can change anything [because we
            // hold the lock to shared state ...]
            lk.unlock();
            // *now* we sleep for a bit and then try again
            std::this_thread::sleep_for( std::chrono::microseconds(42) );
            //std::this_thread::sleep_for( std::chrono::seconds(1) );
            continue;
        }
        // Right, we now hold both locks!
        etdc::transferprops_type&  transfer( *ptr->second );
        transfer.fd->close(transfer.fd->__m_fd);
        // We cannot erase the transfer immediately: we hold the lock that is contained in it
        // so what we do is transfer the lock out of the transfer and /then/ erase the entry.
        // And when we finally return, then the lock will be unlocked and the unique pointer
        // deleted
        //transfer_lock = std::move(transfer.lockPtr);
        // move the data out of the transfermap
        //std::swap(removed, ptr->second);
        removed.swap( ptr->second );
        // OK lock is now moved out of the transfer, so now it's safe to erase the entry
        // OK the uniqueptr to the transfer is now moved out of the transfermap, so now it's safe to erase the entry
        shared_state.transfers.erase( ptr );
        break;
    }
    return true;
}


#endif // ETRANSFER
