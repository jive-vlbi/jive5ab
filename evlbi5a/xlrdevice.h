// implement a [threadsafe] wrapper around the streamstor device
// Copyright (C) 2007-2008 Harro Verkouter
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
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
//
// class: xlrdevice_type
//      for dealing with the H/W
//
// 
//        NOTE NOTE NOTE IMPORTANT NOTE NOTE NOTE
//
//        The StreamStor library uses threads but
//        is not threadsafe. Therefore access to 
//        the device must be serialized.
//
//        Both XLRCALL-macros below have that built in.
//        However! Some API functions do not return
//        XLR_SUCCESS but some other value (eg XLRGetFIFOLength())
//        and for such instances you MUST serialize these calls
//        by first doing "do_xlr_lock()", then the API call and then
//        "do_xlr_unlock()" yourself.
//        The XLRCALL macros do exactly this for you.
//
// This file also defines macros for easy calling of XLR API functions.
// They can be used for all XLR API calls that return XLR_SUCCESS on ...
// succesfull completion. If the result of the API call is NOT XLR_SUCCESS
// the macros will throw an exception.
//
// It comes in two flavours:
//
// XLRCALL( <apicall> )
//      call the API function and if it does NOT return XLR_SUCCESS
//      throw an xlrexception with where and why
// XLRCALL2( <apicall>, <cleanupcode> )
//      same as above, only executes <cleanupcode> immediately before
//      throwing the exception. If you want multiple functions called
//      in your cleanupcode use the 'comma' operator.
//      Note: the cleanupcode is called with the xlr-lock held so
//      it's safe to put plain API-calls in there.
//
// Example:
//
// If you want to add extra information to the exception (eg extra variable
// values being printed) do it like this:
//
// XLRCALL2( ::XLROpen(devnum), ::XLRClose(devnum), XLRINFO(" devnum:" << devnum) );
//
// If the system fails to open XLR device #devnum it will now both
// close the device (must, as per XLR API documentation) and add the
// text " devnum:<devnum>" to the exceptiontext.
//
// Or:
//
// XLRCALL2( ::XLRPlayback(<....>),
//           XLRINFO(" scan:" << sname), cleanup_fn1(), cleanup_fn2(),
//           ::close(fd) );
//
#ifndef EVLBI5A_XLRDEVICE_H
#define EVLBI5A_XLRDEVICE_H

// streamstor api
#include <xlrtypes.h>
#include <xlrapi.h>

// c++ 
#include <string>
#include <iostream>
#include <sstream>
#include <exception>

// own stuff
#include <countedpointer.h>

// channel definitions
#define CHANNEL_PCI         0
#define CHANNEL_FPDP_TOP   30
#define CHANNEL_FPDP_FRONT 31

// On Mark5C's there a newer API than on Mark5A/B
// In the new streamstor API there's no room for
// the type UINT (which was the basic interface
// type on Mark5A/B) but seems to have been replaced
// by UINT32.
// Our code has UINT internally so let's do this 
// and hope the compiler will whine if something
// don't fit.
#ifdef MARK5C
typedef UINT32 UINT;
#endif

// Also the type of the datapointer passed to
// XLRRead* API functions (XLRReadFifo, XLRRead, etc)
// has changed from PULONG (old) to PUINT32
// Code in jive5a[b] uses (as of Dec 2011)
// XLRRead(.., READTYPE* , ...)
#ifdef MARK5C
// new type
typedef UINT32 READTYPE;
#else
// old type
typedef ULONG READTYPE;
#endif

// Start with the support stuff:


// If you create an instance of this object it
// will automagically get the last XLRError +
// the accompanying message for you
struct lastxlrerror_type {
    lastxlrerror_type();

    ULONG        xlr_errno;
    std::string  xlr_errormessage;
};
// format it as "<message> (<errno>)" when this thing
// is inserted into a stream
std::ostream& operator<<( std::ostream& os, const lastxlrerror_type& xlre );

// An exception specific to the streamstor
struct xlrexception :
    public std::exception
{
    xlrexception( const std::string& s );

    virtual const char* what() const throw();
    virtual ~xlrexception() throw();

    const std::string msg;
};


// By wrapping the "Device" in a class we can (automatically)
// trigger calling "XLRClose()" even when the device fails to open
// (as per Conduants documentation for "XLROpen()")
// This implements it as a "pointer-to-implementation" with
// reference counting so we can copy the "interface" at will
// *and*, as an added bonus, guarantee proper shutdown of the device :)
class xlrdevice {
    public:
        // magic value to signal 'no device' ..
        static const UINT     noDevice = ~((UINT)0);

        // constructs an empty/invalid device - you
        // can assign a valid device later on tho.
        xlrdevice();

        // Attemt to open device #d
        explicit xlrdevice( UINT d );

        // return the device number.
        // xlrdevice::noDevice for empty/default object..!
        UINT       devnum( void ) const;

        // Get the handle to the device.
        // May be 'INVALID_SSHANDLE'...
        SSHANDLE   sshandle( void ) const;

        // Access derived info
        bool       isAmazon( void ) const;

        // release resources
        ~xlrdevice();

        // When inserted into a stream, print out some of the device's characteristics
        // in HRF. Define this one as friend so we don't have to expose
        // everything to the outside world
        friend std::ostream& operator<<( std::ostream& os, const xlrdevice& d );

    private:
        // This struct holds the actual properties
        // we just reference an instance of this thing
        struct xlrdevice_type {
            // constructs with invalid devicenumber and invalid SSHANDLE
            xlrdevice_type();

            // attemtps to open device #d
            xlrdevice_type( UINT d );


            // close down the device
            ~xlrdevice_type();

            UINT        devnum;
            SSHANDLE    sshandle;
            S_DEVINFO   devinfo;
            //S_DBINFO    dbinfo;
            S_DEVSTATUS devstatus;

            private:
                // Make sure this thing ain't copyable nor assignable
                xlrdevice_type( const xlrdevice_type& );
                const xlrdevice_type& operator=( const xlrdevice_type& );
        };


        // Our only datamember: a counted pointer to the implementation
        countedpointer<xlrdevice_type>   mydevice;
};



//
// Define the macros that make calling and checking api calls ez
//

// define global functions for lock/unlock
// such we can serialize access to the streamstor
void do_xlr_lock( void );
void do_xlr_unlock( void );

#ifdef __GNUC__
#define XLR_FUNC "in [" << __PRETTY_FUNCTION__ << "]"
#else
#define XLR_FUNC ""
#endif


#define XLR_LOCATION \
    std::string  xLr_fn_( __FILE__); int xLr_ln_(__LINE__);

#define XLR_STUFF(fubarvar) \
    lastxlrerror_type  xLr_1Se; std::ostringstream xlr_Svar_0a;\
    xlr_Svar_0a << xLr_fn_ << ":" << xLr_ln_ << " " << XLR_FUNC << " " << fubarvar << " fails " << xLr_1Se;

// can use this as (one of the) arguments in a XLRCALL2() macro to
// add extra info to the error string
#define XLRINFO(a) \
    xlr_Svar_0a << a;

// Do call an XLR-API method and check returncode.
// If it's not XLR_SUCCESS an xlrexception is thrown
// Perform the actual API call whilst the mutex is held
#define XLRCALL(a) \
    do {\
        XLR_LOCATION;\
        XLR_RETURN_CODE xrv0lcl1;\
        do_xlr_lock();\
        xrv0lcl1 = a;\
        do_xlr_unlock();\
        if( xrv0lcl1!=XLR_SUCCESS ) { \
            XLR_STUFF(#a);\
            throw xlrexception( xlr_Svar_0a.str() ); \
        } \
    } while( 0 );

// the cleanupcode in "b" is also called with
// the lock held so it's safe to put plain
// ::XLR* API calls in there.
#define XLRCALL2(a, b) \
    do {\
        XLR_LOCATION;\
        XLR_RETURN_CODE xrv1lcl2;\
        do_xlr_lock();\
        xrv1lcl2 = a;\
        do_xlr_unlock();\
        if( xrv1lcl2!=XLR_SUCCESS ) { \
            XLR_STUFF(#a);\
            do_xlr_lock();\
            b;\
            do_xlr_unlock();\
            throw xlrexception( xlr_Svar_0a.str() ); \
        } \
    } while( 0 );


#endif
