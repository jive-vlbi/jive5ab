// implement a wrapper around the streamstor device
//
// class: xlrdevice_type
//      for dealing with the H/W
// also defines macros for easy calling of XLR API functions.
// Two flavours:
//
// XLRCALL( <apicall> )
//      call the API function and if it does NOT return XLR_SUCCESS
//      throw an xlrexception with where and why
// XLRCALL2( <apicall>, <cleanupcode> )
//      same as above, only executes <cleanupcode> immediately before
//      trowing the exception
#ifndef EVLBI5A_XLRDEVICE_H
#define EVLBI5A_XLRDEVICE_H

// streamstor api
#include <xlrapi.h>
#include <xlrtypes.h>

// c++ 
#include <string>
#include <iostream>
#include <exception>

// own stuff
#include <countedpointer.h>


// channel definitions
#define CHANNEL_PCI         0
#define CHANNEL_FPDP_TOP   30
#define CHANNEL_FPDP_FRONT 31

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

            UINT      devnum;
            SSHANDLE  sshandle;
            S_DEVINFO devinfo;

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


#ifdef __GNUC__
#define XLR_FUNC "in [" << __PRETTY_FUNCTION__ << "]"
#else
#define XLR_FUNC ""
#endif


#define XLR_LOCATION \
    string  fn_( __FILE__); int ln_(__LINE__);

#define XLR_STUFF(fubarvar) \
    lastxlrerror_type  le; ostringstream xlr_Svar_0a;\
    xlr_Svar_0a << fn_ << ":" << ln_ << " " << XLR_FUNC << " " << fubarvar << " fails " << le;

// can use this as (one of the) arguments in a XLRCALL2() macro to
// add extra info to the error string
#define XLRINFO(a) \
    xlr_Svar_0a << a;

// Do call an XLR-API method and check returncode.
// If it's not XLR_SUCCESS an xlrexception is thrown
#define XLRCALL(a) \
    do {\
        XLR_LOCATION;\
        if( a!=XLR_SUCCESS ) { \
            XLR_STUFF(#a);\
            throw xlrexception( xlr_Svar_0a.str() ); \
        } \
    } while( 0 );

#define XLRCALL2(a, b) \
    do {\
        XLR_LOCATION;\
        if( a!=XLR_SUCCESS ) { \
            XLR_STUFF(#a);\
            b;\
            throw xlrexception( xlr_Svar_0a.str() ); \
        } \
    } while( 0 );


#endif
