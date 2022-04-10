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

// c++ 
#include <string>
#include <iostream>
#include <sstream>
#include <exception>
#include <vector>
#include <set>
#include <map>

// own stuff
#include <xlrdefines.h>
#include <countedpointer.h>
#include <userdir.h>
#include <playpointer.h>
#include <registerstuff.h>
#include <ezexcept.h>
#include <errorqueue.h>

// channel definitions
#define CHANNEL_PCI         (CHANNELTYPE)0
#define CHANNEL_10GIGE      (CHANNELTYPE)28
#define CHANNEL_FPDP_TOP    (CHANNELTYPE)30
#define CHANNEL_FPDP_FRONT  (CHANNELTYPE)31

// Not all SDKs have support for the 10GigE daughterboard
// I think it's safe to assume that if we see one of
// the 10GigE daughter board register names being defined
// that the accompanying XLRReadDBReg32/XLRWriteDBReg32
// are also there
#ifdef SS_10GIGE_REG_MAC_FLTR_CTRL
    #define HAVE_10GIGE 1
#else
    #define HAVE_10GIGE 0
#endif


// Define the macros that make calling and checking api calls ez

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
    xlr_Svar_0a << xLr_fn_ << "@" << xLr_ln_ << " " << XLR_FUNC << " " << fubarvar << " fails " << xLr_1Se;

// can use this as (one of the) arguments in a XLRCALL2() macro to
// add extra info to the error string
#define XLRINFO(a) \
    xlr_Svar_0a << a;

// Do call an XLR-API method and check returncode.
// If it's not XLR_SUCCESS an xlrexception is thrown
// Perform the actual API call whilst the mutex is held
#ifdef NOSSAPI
    #define XLRCALL(a)  do { XLR_LOCATION; \
                             XLR_STUFF(#a);\
                              xlr_Svar_0a << " - compiled w/o SSAPI support"; \
                              throw xlrexception(xlr_Svar_0a.str()); \
                        } while( 0 );
    #define XLRCODE(a)
    #define NOXLRCALL(a) do { a } while (0);
#else
    #define XLRCALL(a) \
        do {\
            XLR_RETURN_CODE xrv0lcl1;\
            do_xlr_lock();\
            xrv0lcl1 = a;\
            do_xlr_unlock();\
            if( xrv0lcl1!=XLR_SUCCESS ) { \
                XLR_LOCATION;\
                XLR_STUFF(#a);\
                throw xlrexception( xlr_Svar_0a.str() ); \
            } \
        } while( 0 );
    #define XLRCODE(a) a
    #define NOXLRCALL(a)
#endif

// the cleanupcode in "b" is also called with
// the lock held so it's safe to put plain
// ::XLR* API calls in there.
#ifdef NOSSAPI
    #define XLRCALL2(a, b)  do { XLR_LOCATION; XLR_STUFF(#a); throw xlrexception(xlr_Svar_0a.str()); } while( 0 );
#else
    #define XLRCALL2(a, b) \
        do {\
            XLR_RETURN_CODE xrv1lcl2;\
            do_xlr_lock();\
            xrv1lcl2 = a;\
            do_xlr_unlock();\
            if( xrv1lcl2!=XLR_SUCCESS ) { \
                XLR_LOCATION;\
                XLR_STUFF(#a);\
                do_xlr_lock();\
                b;\
                do_xlr_unlock();\
                throw xlrexception( xlr_Svar_0a.str() ); \
            } \
        } while( 0 );
#endif


// Start with the support stuff:
const UINT     noDevice = ~((UINT)0);

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

// disk states as appended to the vsn
struct disk_states {
    static const std::set<std::string> all_set;
    static std::pair<std::string, std::string> split_vsn_state(std::string label);
};

// Version numbers in XLR are effing _strings_
// And strings don't compare well numerically *sigh*
struct swversion_type {
    swversion_type(unsigned int ma, unsigned int mi);
    swversion_type(char const* const ma_s, char const*const mi_s);

    // data members 'major' and 'minor' are, apparently,
    // GNU defined macros into 'gnu_dev_major' and 'gnu_dev_minor'
    // Interesting ....
    const unsigned int  major_v, minor_v;

    private:
        // no default c'tor
        swversion_type();
};
bool  operator<(const swversion_type& l, const swversion_type& r);
bool  operator>(const swversion_type& l, const swversion_type& r);
bool operator==(const swversion_type& l, const swversion_type& r);
bool operator<=(const swversion_type& l, const swversion_type& r);
bool operator>=(const swversion_type& l, const swversion_type& r);
std::ostream& operator<<(std::ostream& os, const swversion_type& sw);


// The 10GigE daughterboard registers.
struct xlrreg {
    // The size of the registers on the daughterboard.
    // There's 0x32 4-byte words.
    enum teng_register {
        TENG_BYTE_LENGTH_CHECK_ENABLE, TENG_DISABLE_MAC_FILTER,
        TENG_DPOFST, TENG_DFOFST, TENG_PSN_MODE1, TENG_PSN_MODE2, 
        TENG_PSN_MODES, // this is a pseudo register - read/write both PSN MODE bits in one go
        TENG_PSNOFST, TENG_BYTE_LENGTH,
        TENG_PROMISCUOUS, TENG_CRC_CHECK_DISABLE, TENG_DISABLE_ETH_FILTER,
        TENG_PACKET_LENGTH_CHECK_ENABLE, TENG_PACKET_LENGTH,
        TENG_FILL_PATTERN,
        // The registers for MAC filter 0xF
        TENG_MAC_F_LO, TENG_MAC_F_HI, TENG_MAC_F_EN
    };

    // Note: should be type UINT32 in SDK9 but SDK8 doesn't have UINT32.
    //       i.e. we insert our own 32-bit unsigned type and for the moment
    //       assume they're compatible. The compilert be complainin' if they ain't
    typedef regdesc_type<uint32_t>            regtype;

    typedef std::map<teng_register, regtype>  teng_registermap;
};

// Create a pointer to an xlr daughterboard register.
// Because we haven't defined the xlrdevice class yet we
// can't use that as device and we stick to the SSHANDLE
// for accessing the device.
// (Note: this is only a problem because we want to use
//  this class in the interface of xlrdevice itself -
//  thereby creating the circular dependency)

DECLARE_EZEXCEPT(xlrreg_exception)

struct xlrreg_pointer {
    public:
        xlrreg_pointer();

        xlrreg_pointer(const xlrreg::regtype reg, SSHANDLE dev);

        // assignment to this object will write into the h/w.
        // (possibly) truncate value and shift to correct position,
        // do it in a read-modify-write cycle; it's the only thing we
        // can sensibly do, right?
        //
        // By letting ppl assign arbitrary types, we have at least
        // the possibility to specialize for 'bool'.
        // If you assign a 32bit value to a 16bit hardware register ...
        // well ... too bad!
#if HAVE_10GIGE
        template <typename U>
        const xlrreg_pointer& operator=( const U& u ) {
            UINT32     w;

            EZASSERT(devHandle!=::noDevice, xlrreg_exception);
            XLRCALL( ::XLRReadDBReg32(devHandle, wordnr, &w) );
            w = (w&(~fieldmask))|((((UINT32)u)&valuemask)<<startbit);
            XLRCALL( ::XLRWriteDBReg32(devHandle, wordnr, w) );
            return *this;
        }
#else
        template <typename U>
        const xlrreg_pointer& operator=( const U& ) {
            THROW_EZEXCEPT(xlrreg_exception, "Compiled under SDK without 10GigE daughterboard support")
            return *this;
        }
#endif

        // Have a specialized fn for assigning bool: make sure that
        // bit 0 is set if b==true ...
        const xlrreg_pointer& operator=( const bool& b );

        // and also implement the dereference operator
        // Note: this used to be UINT32 but SDK's below SDK9 don't _have_ UINT32
        //       so to stay compilable under many SDKs we insert our own 32-bit
        //       unsigned type and for now assume they're compatible.
        uint32_t operator*( void ) const;

        friend std::ostream& operator<<(std::ostream& os, const xlrreg_pointer& rp );

    private:
        SSHANDLE    devHandle;
        // Note: these used to be UINT32s but SDK's below SDK9 don't _have_ UINT32
        //       so to stay compilable under many SDKs we insert our own 32-bit
        //       unsigned type and for now assume they're compatible.
        uint32_t    wordnr;
        uint32_t    startbit;
        uint32_t    valuemask;
        uint32_t    fieldmask;
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
        //static const UINT     noDevice = ~((UINT)0);
        static const UINT     noDevice = ::noDevice;

        // constructs an empty/invalid device - you
        // can assign a valid device later on tho.
        xlrdevice();

        // Attemt to open device #d
        explicit xlrdevice( UINT d );

        // Allow cast-to-bool:
        // if( xlrdev ) {
        //     ... do Stuff ...
        // }
        // Is actually shorthand for "xlrdev.sshandle()!=noDevice"
        operator bool() const;

        // return the device number.
        // xlrdevice::noDevice for empty/default object..!
        UINT              devnum( void ) const;

        // Get the handle to the device.
        // May be 'INVALID_SSHANDLE'...
        SSHANDLE          sshandle( void ) const;

        // Read-only access to the dev-info 
        // and versions. Only to be trusted to
        // contain usefull info if
        //     sshandle()!=noDevice
        const S_DBINFO&   dbInfo( void ) const;
        const S_XLRSWREV& swRev( void ) const;
        // the devInfo struct is written into by the poller thread,
        // so we need to copy the cached data into a user supplied struct
        void              copyDevInfo( S_DEVINFO& into ) const;

        // Bankmode stuff.
        // The setBankMode either just works (tm) 
        // or throws up
        void              setBankMode( S_BANKMODE newmode );
        S_BANKMODE        bankMode( void ) const;

        // Access derived info
        bool              isAmazon( void ) const;

        // Return the generation of the StreamStor board:
        //   0 => #ERROR or no device
        //   3 => XF2/V100/VXF2
        //   4 => Amazon-*
        //   5 => Amazon/Express
        unsigned int      boardGeneration( void ) const;

        // return the maximum bps this StreamStor can sustain while in forking mode/absolute device limit
        unsigned long int maxForkDataRate( void ) const;
        unsigned long int maxRecordDataRate( void ) const;

        // Read/Write daughterboard
        xlrreg_pointer    operator[](xlrreg::teng_register reg);

        // access function to/from the user directory
        ROScanPointer         getScan( unsigned int index );
        ScanPointer           startScan( std::string name );
        void                  finishScan( ScanPointer& scan );
        std::string           userDirLayoutName( void ) const;
        // call stopRecordingFailure in case of problems with the streamstor
        // prevent properly ending a scan
        void                  stopRecordingFailure();
        unsigned int          nScans( void );
        bool                  isScanRecording( void );

        // read/write the VSN/disk state to streamstor, also update the user directory
        // write companion/write vsn may throw if the userdirectory does not
        // support storing that info
        void         write_vsn( std::string vsn );
        void         write_state( std::string state );
        std::string  read_label( void );

        std::string  get_vsn( void );
        std::string  get_companion( void );

        // erase the whole disk, forcing a new layout
        void erase( std::string layoutName, const SS_OWMODE=SS_OVERWRITE_NONE );
        // erase last scan only
        void erase_last_scan( void );
        // erase the disk and gather statistics, by doing a write/read cycle
        void start_condition( void );

        enum mount_point_type { NoBank, BankA, BankB, NonBankMode };
        struct mount_status_type {
            mount_point_type    mount_point;
            std::string         vsn;

            mount_status_type():
                mount_point( NoBank )
            {}
            mount_status_type(mount_point_type mp, std::string const& v):
                mount_point( mp ), vsn( v )
            {}

            bool operator==(mount_status_type const& other) {
                return (other.mount_point==mount_point && other.vsn==vsn);
            }
            bool operator!=(mount_status_type const& other) {
                return !(*this==other);
            }
        };

        // checks mount status and reload user dir if changed
        // HV: 30-May-2016 Changed to return the current mount_status_type.
        //                 This allows caller(s) to do something useful 
        //                 if they detect a change
        mount_status_type        update_mount_status();
        
        // returns the drive info stored in the user directory
        // only available if user direcyory layout is version one or two
        std::vector<S_DRIVEINFO> getStoredDriveInfo( void );

        // the streamstor has a method XLRRecoverData, which can be used
        // to recover data after various failure modes
        // recover will execute this method and try to restore the ScanDir
        void recover( UINT mode );
        
        // drive statistics (access times) can be gathered by the streamstor
        // this function sets the bins of the statistics
        // calling set with an empty vector will use the previously set bins
        // otherwise the size of the vector should be 7 (drive_stats_length)
        void set_drive_stats( std::vector<ULONG> settings );
        std::vector< ULONG > get_drive_stats( );
        static const ULONG drive_stats_default_values[];
        static const size_t drive_stats_length;
        
        // release resources
        ~xlrdevice();

        // When inserted into a stream, print out some of the device's characteristics
        // in HRF. Define this one as friend so we don't have to expose
        // everything to the outside world
        friend std::ostream& operator<<( std::ostream& os, const xlrdevice& d );

    private:
        // All xlr device instances share the same daughter board registers
        static xlrreg::teng_registermap   xlrdbregs;

        // assumes user_dir_lock is already locked and then does the same as the public version
        void locked_set_drive_stats( std::vector<ULONG> settings );

        // write the label only, assumes the user_dir_lock is already locked
        void write_label( std::string vsn );
        

        // This struct holds the actual properties
        // we just reference an instance of this thing
        struct xlrdevice_type {
            // constructs with invalid devicenumber and invalid SSHANDLE
            xlrdevice_type();

            // attemtps to open device #d
            xlrdevice_type( UINT d );

            void        setBankMode( S_BANKMODE newmode );
            std::string read_label( void );

            // close down the device
            ~xlrdevice_type();

            UINT           devnum;
            SSHANDLE       sshandle;
            S_DBINFO       dbinfo;
            S_DEVINFO      devinfo;
            S_BANKMODE     bankMode;
            S_DEVSTATUS    devstatus;
            S_XLRSWREV     swrev;

            UserDirectory      user_dir;
            mount_status_type  mount_status;
            bool               recording_scan;
            std::vector<ULONG> drive_stats_settings;

            // lock access to above members
            mutable pthread_mutex_t user_dir_lock;
            
            private:
                

                // Make sure this thing ain't copyable nor assignable
                xlrdevice_type( const xlrdevice_type& );
                const xlrdevice_type& operator=( const xlrdevice_type& );
        };

        // Our only datamember: a counted pointer to the implementation
        countedpointer<xlrdevice_type>   mydevice;
};


#endif
