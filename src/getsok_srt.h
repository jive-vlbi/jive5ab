// methods that create/configure SRT sockets as per what we think are sensible defaults
// Copyright (C) 2007-2024 Marjolein Verkouter
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
// Author:  Marjolein Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI as an ERIC
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef JIVE5A_GETSOKSRT_H
#define JIVE5A_GETSOKSRT_H

#include <string>
#include <getsok.h>        // for ::resolve_host() and fdprops_type
#include <ezexcept.h>
#include <srtcore/common.h>           // for the congestion control base class
#include <srtcore/congctl.h>           // for the congestion control base class

DECLARE_EZEXCEPT(srtexception)

// Open an UDT connection to <host>:<port>
// Returns the filedescriptor for this open connection.
// It will be in blocking mode.
// Throws if something fails.
int getsok_srt( const std::string& host, unsigned short port, const std::string& proto, const unsigned int mtu);

// Get a socket for incoming connections.
// The returned filedescriptor is in blocking mode.
//
// You *must* specify the port/protocol. Optionally specify
// a local interface to bind to. If left empty (which is
// default) bind to all interfaces.
int getsok_srt(unsigned short port, const std::string& proto, const unsigned int mtu, const std::string& local = "");

// perform an accept on 'fd' and return something that is
// suitable for insertion in a 'fdprops_type' typed variable.
fdprops_type::value_type do_accept_incoming_srt( int fd );

// SRT does not allow congestioncontrol like UDT does
#if 0
// All our UDT sockets will be constructed with a modified congestion
// control algorithm - the default CUDTCC (seems to work nicely) but we
// add the possibility of adjusting the sending rate on the fly
// by modifiying the ipd; much like we do with all our other UDP based
// transfers
class IPDBasedCC:
    public CUDTCC
{
    public:
        // Default c'tor - initial ipd will be 0
        // i.e. no rate control
        IPDBasedCC();

        // We only overload the onACK because that's
        // where the rate limiting occurs
        virtual void onACK(int32_t seqno);

        // We support one method - setting the ipd in ns.
        void         set_ipd(unsigned int ipd_in_ns);
        unsigned int get_ipd( void ) const;

        virtual ~IPDBasedCC();

    private:
        unsigned int  _ipd_in_ns;
};
#endif

// We need to handle calls to the srt::* functions a little bit different
#ifdef __GNUC__
#define SRT_FUNC "in [" << __PRETTY_FUNCTION__ << "]"
#else
#define SRT_FUNC ""
#endif

#define SRT_LOCATION \
    std::string  srt_fn_( __FILE__); int srt_ln_(__LINE__);

//    std::ostringstream udt_Svar_0a;
//    << " " << fubarvar << " fails ";
#define SRTSTUFF(fubarvar) \
    srt_Svar_0a << srt_fn_ << "@" << srt_ln_ << " " << SRT_FUNC << " " << fubarvar << " "

// can use this as (one of the) arguments in a XLRCALL2() macro to
// add extra info to the error string
#define SRTINFO(a) \
    srt_Svar_0a << a;

/////////////////////////////////////////////////////////////////////////
// Generic assertion 
// throw udtexception if !(a), executing b before throwing.
/////////////////////////////////////////////////////////////////////////
#define SRTASSERT2(a, b) \
    do { \
        SRT_LOCATION; bool srtFa1l( false );  std::ostringstream srt_Svar_0a;\
        try { if( (srtFa1l = !(a))==true ) SRTSTUFF("assertion " << #a << " fails"); } \
        catch( srt::CUDTException& srT3xcept ) { \
            srtFa1l = true; SRTSTUFF("SRTException{code=" << srT3xcept.getErrorCode() << ", msg=" << srT3xcept.getErrorMessage() << "} during execution of " << #a);\
        } \
        if( srtFa1l ) { b;  throw srtexception( srt_Svar_0a.str() ); }\
    } while( 0 );

// id. without cleanup
#define SRTASSERT(a) \
    SRTASSERT2(a, ;)

/////////////////////////////////////////////////////////////////////////
// assert that (a)==0, with cleanup b executed before throwing
/////////////////////////////////////////////////////////////////////////
#define SRTASSERT2_ZERO(a, b) \
    do { \
        SRT_LOCATION; bool srtFa1l( false );  std::ostringstream srt_Svar_0a;\
        try { if( (srtFa1l = !((a)==0))==true ) SRTSTUFF("assertion " << #a << " fails"); } \
        catch( srt::CUDTException& srT3xcept ) { \
            srtFa1l = true; SRTSTUFF("SRTException{code=" << srT3xcept.getErrorCode() << ", msg=" << srT3xcept.getErrorMessage() << "} during execution of " << #a);\
        } \
        if( srtFa1l ) { b;  throw srtexception( srt_Svar_0a.str() ); }\
    } while( 0 );

// id. without cleanup
#define SRTASSERT_ZERO(a) \
    SRTASSERT2_ZERO(a, ;)

/////////////////////////////////////////////////////////////////////////
// assert that (a)!=0, with cleanup b executed before throwing
/////////////////////////////////////////////////////////////////////////
#define SRTASSERT2_NZERO(a, b) \
    do { \
        SRT_LOCATION; bool srtFa1l( false );  std::ostringstream srt_Svar_0a;\
        try { if( (srtFa1l = !((a)!=0))==true ) SRTSTUFF("assertion " << #a << " fails"); } \
        catch( srt::CUDTException& srt3xcept ) { \
            srtFa1l = true; SRTSTUFF("SRTException{code=" << srt3xcept.getErrorCode() << ", msg=" << srt3xcept.getErrorMessage() << "} during execution of " << #a);\
        } \
        if( srtFa1l ) { b;  throw srtexception( srt_Svar_0a.str() ); }\
    } while( 0 );

// id. without cleanup
#define SRTASSERT_NZERO(a) \
    SRTASSERT2_NZERO(a, ;)

/////////////////////////////////////////////////////////////////////////
// assert that (a)>=0 (non-negative), with cleanup b executed before throwing
/////////////////////////////////////////////////////////////////////////
#define SRTASSERT2_POS(a, b) \
    do { \
        SRT_LOCATION; bool srtFa1l( false );  std::ostringstream srt_Svar_0a;\
        try { if( (srtFa1l = !((a)>=0))==true ) SRTSTUFF("assertion " << #a << " fails"); } \
        catch( srt::CUDTException& srt3xcept ) { \
            srtFa1l = true; SRTSTUFF("SRTException{code=" << srt3xcept.getErrorCode() << ", msg=" << srt3xcept.getErrorMessage() << "} during execution of " << #a);\
        } \
        if( srtFa1l ) { b;  throw srtexception( srt_Svar_0a.str() ); }\
    } while( 0 );

// id. without cleanup
#define SRTASSERT_POS(a) \
    SRTASSERT2_POS(a, ;)

#endif
