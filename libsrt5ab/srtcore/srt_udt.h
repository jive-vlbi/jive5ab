/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/18/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

/* WARNING!!!
 * Since now this file is a "C and C++ header".
 * It should be then able to be interpreted by C compiler, so
 * all C++-oriented things must be ifdef'd-out by __cplusplus.
 *
 * Mind also comments - to prevent any portability problems,
 * B/C++ comments (// -> EOL) should not be used unless the
 * area is under __cplusplus condition already.
 *
 * NOTE: this file contains _STRUCTURES_ that are common to C and C++,
 * plus some functions and other functionalities ONLY FOR C++. This
 * file doesn't contain _FUNCTIONS_ predicted to be used in C - see udtc.h
 */

#ifndef INC_SRT_UDT_H
#define INC_SRT_UDT_H

#include <set>
#include <vector>
#include "srt.h"

/*
* SRT_ENABLE_THREADCHECK IS SET IN MAKEFILE, NOT HERE
*/
#if defined(SRT_ENABLE_THREADCHECK)
#include "threadcheck.h"
#else
#define THREAD_STATE_INIT(name)
#define THREAD_EXIT()
#define THREAD_PAUSED()
#define THREAD_RESUMED()
#define INCREMENT_THREAD_ITERATIONS()
#endif

// This is a log configuration used inside SRT.
// Applications using SRT, if they want to use the logging mechanism
// are free to create their own logger configuration objects for their
// own logger FA objects, or create their own. The object of this type
// is required to initialize the logger FA object.
namespace srt_logging { struct LogConfig; }
SRT_API extern srt_logging::LogConfig srt_logger_config;

// Enclose stuff in srt:: 
namespace srt {

    // in common.{h,cpp}
    class CUDTException;

    // This is a C++ SRT API extension. This is not a part of legacy UDT API.
    SRT_API void setloglevel(srt_logging::LogLevel::type ll);
    SRT_API void addlogfa(srt_logging::LogFA fa);
    SRT_API void dellogfa(srt_logging::LogFA fa);
    SRT_API void resetlogfa(std::set<srt_logging::LogFA> fas);
    SRT_API void resetlogfa(const int* fara, size_t fara_size);
    SRT_API void setlogstream(std::ostream& stream);
    SRT_API void setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
    SRT_API void setlogflags(int flags);

    SRT_API bool setstreamid(SRTSOCKET u, const std::string& sid);
    SRT_API std::string getstreamid(SRTSOCKET u);

    // Namespace alias
    namespace logging {
        using namespace srt_logging;
    }
    
    // Here is where srt::UDT starts
    namespace UDT {
        // Explicitly refer to the one in the srt:: namespace
        typedef srt::CUDTException ERRORINFO;

        // This facility is used only for select() function.
        // This is considered obsolete and the epoll() functionality rather should be used.
        typedef std::set<SRTSOCKET> UDSET;
#if 0
#define SRT_UD_CLR(u, uset) ((uset)->erase(u))
#define SRT_UD_ISSET(u, uset) ((uset)->find(u) != (uset)->end())
#define SRT_UD_SET(u, uset) ((uset)->insert(u))
#define SRT_UD_ZERO(uset) ((uset)->clear())
#endif
        SRT_API extern const SRTSOCKET INVALID_SOCK;

        SRT_API int startup();
        SRT_API int cleanup();
        SRT_API SRTSOCKET socket();
        SRT_API SRTSOCKET socket(int , int , int );
        SRT_API int bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
        SRT_API int bind2(SRTSOCKET u, UDPSOCKET udpsock);
        SRT_API int listen(SRTSOCKET u, int backlog);
        SRT_API SRTSOCKET accept(SRTSOCKET u, struct sockaddr* addr, int* addrlen);
        SRT_API int connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
        SRT_API int close(SRTSOCKET u);
        SRT_API int getpeername(SRTSOCKET u, struct sockaddr* name, int* namelen);
        SRT_API int getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen);
        SRT_API int getsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, void* optval, int* optlen);
        SRT_API int setsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, const void* optval, int optlen);
        SRT_API int send(SRTSOCKET u, const char* buf, int len, int flags);
        SRT_API int recv(SRTSOCKET u, char* buf, int len, int flags);

        SRT_API int sendmsg(SRTSOCKET u, const char* buf, int len, int ttl = -1, bool inorder = false, int64_t srctime = 0);
        SRT_API int recvmsg(SRTSOCKET u, char* buf, int len, uint64_t& srctime);
        SRT_API int recvmsg(SRTSOCKET u, char* buf, int len);

        SRT_API int64_t sendfile(SRTSOCKET u, std::fstream& ifs, int64_t& offset, int64_t size, int block = 364000);
        SRT_API int64_t recvfile(SRTSOCKET u, std::fstream& ofs, int64_t& offset, int64_t size, int block = 7280000);
        SRT_API int64_t sendfile2(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block = 364000);
        SRT_API int64_t recvfile2(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block = 7280000);

        // select and selectEX are DEPRECATED; please use epoll. 
        SRT_API int select(int nfds, UDSET* readfds, UDSET* writefds, UDSET* exceptfds, const struct timeval* timeout);
        SRT_API int selectEx(const std::vector<SRTSOCKET>& fds, std::vector<SRTSOCKET>* readfds,
                std::vector<SRTSOCKET>* writefds, std::vector<SRTSOCKET>* exceptfds, int64_t msTimeOut);

        SRT_API int epoll_create();
        SRT_API int epoll_add_usock(int eid, SRTSOCKET u, const int* events = NULL);
        SRT_API int epoll_add_ssock(int eid, SYSSOCKET s, const int* events = NULL);
        SRT_API int epoll_remove_usock(int eid, SRTSOCKET u);
        SRT_API int epoll_remove_ssock(int eid, SYSSOCKET s);
        SRT_API int epoll_update_usock(int eid, SRTSOCKET u, const int* events = NULL);
        SRT_API int epoll_update_ssock(int eid, SYSSOCKET s, const int* events = NULL);
        SRT_API int epoll_wait(int eid, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, int64_t msTimeOut,
                std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
        SRT_API int epoll_wait2(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                SYSSOCKET* lrfds = NULL, int* lrnum = NULL, SYSSOCKET* lwfds = NULL, int* lwnum = NULL);
        SRT_API int epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
        SRT_API int epoll_release(int eid);
        SRT_API ERRORINFO& getlasterror();
        SRT_API int getlasterror_code();
        SRT_API const char* getlasterror_desc();
        SRT_API int bstats(SRTSOCKET u, SRT_TRACEBSTATS* perf, bool clear = true);
        SRT_API SRT_SOCKSTATUS getsockstate(SRTSOCKET u);

    } // namespace UDT
} // namespace srt

#endif
