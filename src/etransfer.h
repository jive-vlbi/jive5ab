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
#ifndef EVLBI5A_ETRANSFER_H
#define EVLBI5A_ETRANSFER_H

// Only if e-transfer support compiled in
#ifdef ETRANSFER

#include <etdc_fd.h>
#include <etdc_etdserver.h>
#include <memory>
#include <data_check.h>  // for data_reader_type and descendants
#include <ezexcept.h>
#include <notimplemented.h>

DECLARE_EZEXCEPT(etransfer_exception)


using streamstor_reader_ptr = std::shared_ptr<streamstor_reader_type>;

///////////////////////////////////////////////////////
//
// This struct adapts access to the StreamStor card 
// into an etdc_fd file descriptor entity.
// There is no actual file descriptor but it can mimick
// the reads & seeks.
//
////////////////////////////////////////////////////////
struct etd_streamstor_reader:
    public etdc::etdc_fd {


        etd_streamstor_reader(SSHANDLE h, const playpointer& start, const playpointer& end);

        virtual ~etd_streamstor_reader();

        /// Here should be static methods
        static int                 close(int);
        static off_t               lseek(int, off_t offset, int whence);
        static ssize_t             read(int, void* buf, size_t n);
        static etdc::sockname_type getsockname(int);

        // We keep a static streamstor_reader_type()
        // Only one runtime can have the disk2etransfer function.
        // The upper level code makes sure no etd_streamstor_reader() can be
        // constructed before another one is finished.
        // We rely on other code to make sure this is only
        // used as singleton.
        static streamstor_reader_ptr  __m_ssReader;
        static int64_t                __m_start, __m_offset, __m_end;

        private:
            // If we're compiling ETRANSFER, we're in C++11 happyland!
            etd_streamstor_reader() = delete;
            etd_streamstor_reader const& operator=(etd_streamstor_reader const&) = delete;
};


/////////////////////////////////////////////////////////////////////////
//
//  Adapt the vbs reader into an etdc_fd file descriptor
//  The libvs gives us a 'file descriptor' and has read/seek
//  defined on these file descriptors so mapping should be easy
//
/////////////////////////////////////////////////////////////////////////
struct etd_vbs_fd:
    public etdc::etdc_fd
{
        etd_vbs_fd(std::string const& scan, mountpointlist_type const& mps);
        virtual ~etd_vbs_fd();

        static etdc::sockname_type getsockname(int);

    private:
        std::string const                   __m_scanName;
        //std::unique_ptr<vbs_reader_base>    __m_vbsReader;

        etd_vbs_fd() = delete;
        etd_vbs_fd const& operator=(etd_vbs_fd const&) = delete;
};


///////////////////////////////////////////////////////////////////////////////////
//
// We override the ETDServer class to have the 
//     virtual requestFileRead() 
// return UUIDs that map to etdc_fd-derived instances of the streamstor or vbs reader
//
///////////////////////////////////////////////////////////////////////////////////
#if 0
class ETDStreamstorServer :
    etdc::ETDServerInterface
{
    ETDStreamstorServer();

    // Implement requestFileRead
    virtual etdc::result_type requestFileRead(std::string const& /*file name*/, off_t offset /*alreadyhave*/);

    virtual ~ETDStreamstorServer();
};
class ETDFlexBuffServer :
    etdc::ETDServerInterface
{
    ETDFlexBuffServer();

    virtual etdc::result_type requestFileRead(std::string const& scan/*file name*/, off_t offset /*alreadyhave*/);

    virtual ~ETDStreamstorServer();
};
#endif
extern etdc::etd_state  etdState;

class ETD5abServer: public etdc::ETDServerInterface {
    public:
        explicit ETD5abServer(etdc::etd_state& shared_state):
            __m_uuid( etdc::uuid_type::mk() ), __m_shared_state( shared_state )
        { DEBUG(3, "ETD5abServer starting, my uuid=" << __m_uuid << std::endl); }

        virtual etdc::filelist_type     listPath(std::string const& /*path*/, bool /*allow tilde expansion*/) const NOTIMPLEMENTED;

        virtual etdc::result_type       requestFileWrite(std::string const&, etdc::openmode_type) NOTIMPLEMENTED;
        virtual etdc::result_type       requestFileRead(std::string const&,  off_t) NOTIMPLEMENTED;
        virtual etdc::dataaddrlist_type dataChannelAddr( void ) const NOTIMPLEMENTED;

        // Canned sequence?
        virtual etdc::xfer_result   sendFile(etdc::uuid_type const& /*srcUUID*/, etdc::uuid_type const& /*dstUUID*/,
                                             off_t /*todo*/, etdc::dataaddrlist_type const& /*remote*/);
        virtual etdc::xfer_result   getFile (etdc::uuid_type const& /*srcUUID*/, etdc::uuid_type const& /*dstUUID*/,
                                             off_t /*todo*/, etdc::dataaddrlist_type const& /*remote*/) NOTIMPLEMENTED;

        virtual bool          removeUUID(etdc::uuid_type const&);
        virtual std::string   status( void ) const NOTIMPLEMENTED;

        template <typename actual_fd>
        etdc::result_type     requestFileReadT(std::string const& nPath, off_t alreadyhave) {
            // We must check-and-insert-if-ok into shared state.
            // This has to be atomic, so we'll grab the lock
            // until we're completely done.
            auto&                       shared_state( __m_shared_state.get() );
            std::lock_guard<std::mutex> lk( shared_state.lock );
            auto&                       transfers( shared_state.transfers );

            // Check if we're not already busy
            ETDCASSERT(transfers.find(__m_uuid)==transfers.end(), "requestFileReadT: this server is already busy");

            // Before doing anything - see if this server already has an entry for this (normalized) path -
            // we can only honour this request if it's opened for reading [multiple readers = ok]
            //const std::string nPath( detail::normalize_path(path) );
            const auto  pathPtr = std::find_if(std::begin(transfers), std::end(transfers),
                                               std::bind([&](std::string const& p) { return p==nPath; },
                                                         std::bind(std::mem_fn(&etdc::transferprops_type::path), std::bind(etdc::snd_type(), std::placeholders::_1))));
            ETDCASSERT(pathPtr==std::end(transfers) || pathPtr->second->openMode==etdc::openmode_type::Read,
                       "requestFileReadT(" << nPath << ") - the path is already in use");

            // Transform to int argument to open(2) + append some flag(s) if necessary/available
            int  omode = static_cast<int>(etdc::openmode_type::Read);

#if O_LARGEFILE
            // set large file if the current system has it
            omode |= O_LARGEFILE;
#endif

            // Note: etdc_file(...) c'tor will create the whole directory tree if necessary.
            // Because openmode is read, then we don't have to pass the file permissions; either it's there or it isn't
            //etdc_fdptr      fd( new etdc_file(nPath, omode) );
            etdc::etdc_fdptr fd( std::regex_match(nPath, etdc::rxDevZero) ? mk_fd<etdc::devzeronull>(nPath, omode) : mk_fd<actual_fd>(nPath, omode) );
            const off_t      sz{ fd->lseek(fd->__m_fd, 0, SEEK_END) };
            //const uuid_type uuid{ uuid_type::mk() };

            // Assert that we can seek to the requested position
            ETDCASSERT(fd->lseek(fd->__m_fd, alreadyhave, SEEK_SET)!=static_cast<off_t>(-1),
                       "Cannot seek to position " << alreadyhave << " in file " << nPath << " - " << etdc::strerror(errno));

            auto insres = transfers.emplace(__m_uuid, std::unique_ptr<etdc::transferprops_type>( new etdc::transferprops_type(fd, nPath, etdc::openmode_type::Read)));
            ETDCASSERT(insres.second, "Failed to insert new entry, request file read '" << nPath << "'");
            return etdc::result_type(__m_uuid, sz-alreadyhave);
        }

        virtual ~ETD5abServer();

    private:
        // We operate on shared state
        const etdc::uuid_type                   __m_uuid;
        std::reference_wrapper<etdc::etd_state> __m_shared_state;
};


template <typename Which = etdc::ETDProxy, typename... Args>
etdc::etd_server_ptr mk_proxy(Args&&... args) {
    return std::make_shared<Which>( mk_client(std::forward<Args>(args)...) );
}


// all e-transfers share this state
struct etransfer_state {
    etdc::etd_server_ptr    src, dst;  // pointers to ETDProxy* instances
};



#endif // ETRANSFER

#endif // EVLBI5A_ETRANSFER_H
