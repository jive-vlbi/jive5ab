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
#include <data_check.h>  // for data_reader_type and descendants
#include <ezexcept.h>
#include <threadfns.h>    // for all the processing steps + argument structs
#include <blockpool.h>
#include <notimplemented.h>
#include <runtime.h>
#include <memory>
#include <map>

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
struct etd_streamstor_fd:
    public etdc::etdc_fd {

        // the first two parameters are the same as to open(2) 
        // and have no meaning for the streamstor reader
        etd_streamstor_fd(SSHANDLE h, const playpointer& start, const playpointer& end);

        virtual ~etd_streamstor_fd();

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
            etd_streamstor_fd() = delete;
            etd_streamstor_fd const& operator=(etd_streamstor_fd const&) = delete;
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
        etd_vbs_fd(std::string const& scan, open_vbs_rv const& openrec );
        etd_vbs_fd(std::string const& scan, mountpointlist_type const& mps);
        // the first two parameters are similar to open(2)
        etd_vbs_fd(std::string const& scan, int, mountpointlist_type const& mps);
        virtual ~etd_vbs_fd();

        static etdc::sockname_type getsockname(int);

    private:
        std::string const                   __m_scanName;
        //std::unique_ptr<vbs_reader_base>    __m_vbsReader;

        etd_vbs_fd() = delete;
        etd_vbs_fd const& operator=(etd_vbs_fd const&) = delete;
};

// Support for the disk2etransfer{_vbs} functions:
// support structures, cleanup function and thread function

using unique_result = std::unique_ptr<etdc::result_type>;

// all e-transfers share this state
struct etransfer_state {
    runtime*                        rteptr;
    std::string                     scanName;
    unique_result                   dstResult;
    volatile off_t                  fpStart, fpEnd, fpCur;
    etdc::etdc_fdptr                src_fd, dst_fd;
    etdc::etd_server_ptr            remote_proxy;
    std::unique_ptr<pthread_t>      sender;
    std::unique_ptr<blockpool_type> pool;

    // initializes the step-id's to invalid stepid; the other members
    // have good default c'tors. Asserts that p!=NULL
    etransfer_state(runtime* p);

    private:
        etransfer_state();
};

using etransfer_state_ptr  = std::shared_ptr<etransfer_state>;
using etransfer_state_type = std::map<runtime*, etransfer_state_ptr>;


// The thread functions
void etransfer_fd_read(outq_type<block>*, sync_type<etransfer_state_ptr>*);
void etransfer_fd_write(inq_type<block>*, sync_type<etransfer_state_ptr>*);

// and the cancellation function
void cancel_etransfer(etransfer_state_ptr*);

// and the cleanup function
void etransfer_cleanup(etransfer_state_type::iterator);

#endif // ETRANSFER

#endif // EVLBI5A_ETRANSFER_H
