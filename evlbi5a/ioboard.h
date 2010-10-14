// access the mark5 ioboard
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
#ifndef EVLBI5A_IOBOARD_H
#define EVLBI5A_IOBOARD_H

#include <string>
#include <map>
#include <iostream>
#include <sstream>
#include <vector>

#include <hex.h>
#include <ezexcept.h>
#include <registerstuff.h>
#include <flagstuff.h>
#include <dosyscall.h>
#include <evlbidebug.h>

DECLARE_EZEXCEPT(ioboardexception);


// the mark5a I/O board registers
struct mk5areg {
    // size of region to map
    // cf IOBoard.c Mk5A ioboard needs to map 128 bytes into memory
    static const size_t   mmapregionsize = 128;

    // The FQ_UD bit is wired to the AD9850 'FQ_UD' pin, the 'W_CLK' to
    //  the AD9850's W_CLK [see .pdf], the 'W' 8-bit value is connected
    //  to the 8-bit parallel load register "W[0:4]" for loading the
    //  control/data word into the AD9850
    enum ipb_regname {
        invalid_ipmk5a, notClock, W_CLK, U, FQ_UD=U, mode, vlba, errorbits, R, W
        // aliases for full words
        // words 3 and 5 only contain ReadOnly fields so we don't bother
        // doing them
        ,ip_word0, ip_word1, ip_word2 /*, ip_word3*/, ip_word4 /*, ip_word5*/
    };
    // the register names for the outputboard
    // their locations etc will be keyed to this
    // 'V' => VLBA  'I' => Internal clock
    // 'AP' => 'run in A+ mode' (playback of Mk5B data on a Mk5A+)
    // 'AP1' and 'AP2' are used to select which Mk5B => VLBA built-in
    // trackmap to use: (0,0)=>0, (1,0)=>1, (0,1)=>2, (1,1)=Undef
    enum opb_regname {
       invalid_opmk5a, ChBSelect, ChASelect, F, I,  AP, AP1, AP2,
       V, SF, CODE, C, Q, S, NumberOfReSyncs, DIMRev, FillPatMSBs, FillPatLSBs
       // aliases for full words
       //,op_word0, op_word1, op_word2, op_word3, op_word4
    };

    // the mk5a ioboard uses 16bit registers
    typedef regdesc_type<unsigned short>   regtype;
    // now define a mapping between the symbolic names and where the regs
    // actually are located
    typedef std::map<ipb_regname, regtype> ipb_registermap;
    typedef std::map<opb_regname, regtype> opb_registermap;

    // get read-only access to the registerfile
    static const ipb_registermap&  ipb_registers( void );
    static const opb_registermap&  opb_registers( void );
};

// print out the registernames in hrf:
std::ostream& operator<<(std::ostream& os, mk5areg::ipb_regname r );
std::ostream& operator<<(std::ostream& os, mk5areg::opb_regname r );



// mark5b registers and I/O ports
struct mk5breg {
    // size of region to map
    // cf IOBoard.c Mk5B ioboard needs to map 0x20000 bytes into memory
    static const size_t   mmapregionsize = 0x20000;

    // Technically, this is not a registertype ... it's 
    // the I/O port-type ... it's almost, but not quite,
    // entirely unlike a register ... so it'll fit in here
    // perfectly ....
    // We do Mk5B port I/O in units of 32bits, ie unsigned int
    // (this holds on ILP32 as well as LP64 systems).
    typedef unsigned int ioport_type;

    // bitvalues that represent a specific LED colour
    enum led_colour {
        led_off=0x0, led_red=0x1, led_green=0x2, led_blue=0x3
    };

    // Mk5B DOM Registers
    enum dom_register {
        DOM_LEDENABLE, // enable LEDs ...
        DOM_LED0, DOM_LED1, // DOM s/w controllable leds
        DOM_ICLK, // DOM InternalClock control 
    };

    // and DIM Registers
    enum dim_register {
        // Software-controlled LEDs are #6 and #7
        DIM_LED0, DIM_LED1, DIM_SW1=DIM_LED0, DIM_SW2=DIM_LED1, 
        DIM_REVBYTE, // Current DIM revision
        // fclock = 2^(K+1), 0<=k<=5, decimation = 2^J, 0<=j<=4 AND j<=k
        DIM_K, DIM_J, 
        // which pulse-per-second-source to sync on:
        // 0=>None, 1=>ALTA1PPS (?), 2=>ALTB1PPS, 3=>VSI1PPS
        DIM_SELPP, 
        DIM_SELCGCLK, // clock: 0=>VSI clock, 1=>clockgenerator
        // 32-bit-stream-mask, which bitstreams are active. #bits set must be ^2!,
        // divided over two 16-bit values
        DIM_BSM_H, DIM_BSM_L, 
        DIM_USERWORD, // ...
        // 32-bit header words #2 and #3, apparently ...
        // spread over two 16-bit values each
        DIM_HDR2_H, DIM_HDR2_L, DIM_HDR3_H, DIM_HDR3_L,
        // 32bit T(est)-V(ector)-R(ecorder) mask, in two 16-bit chunks
        DIM_TVRMASK_H, DIM_TVRMASK_L,
        // GOCOM? WTF is that?
        DIM_GOCOM,
        // Request Use of FPDP_II ['II' will be set to '1' or '0'
        // depending on wether or not the h/w can support it]
        DIM_REQ_II, DIM_II,
        DIM_SETUP, DIM_RESET, DIM_STARTSTOP, DIM_PAUSE,
        DIM_SELDOT, DIM_SELDIM,
        DIM_ERF, DIM_TVGSEL,
        DIM_ICLK, // DIM Internal Clock config
        DIM_SYNCPPS, DIM_SUNKPPS, DIM_CLRPPSFLAGS, DIM_RESETPPS, // PPS stuff
        DIM_APERTURE_SYNC, DIM_EXACT_SYNC, // even more PPS stuff
        DIM_STARTTIME_H, DIM_STARTTIME_L,
    };

    // the mk5a ioboard uses 16bit registers
    typedef regdesc_type<unsigned short>   regtype;
    // now define a mapping between the symbolic names and where the regs
    // actually are located
    typedef std::map<dom_register, regtype> dom_registermap;
    typedef std::map<dim_register, regtype> dim_registermap;

    // get read-only access to the registerfile
    static const dom_registermap&  dom_registers( void );
    static const dim_registermap&  dim_registers( void );
};

// Display stuff in HRF
std::ostream& operator<<(std::ostream& os, mk5breg::dim_register r );
std::ostream& operator<<(std::ostream& os, mk5breg::dom_register r );
std::ostream& operator<<(std::ostream& os, mk5breg::led_colour l);

// transform a string into a led_colour allows "off", "red", "green", "blue"
// as well as their numerical values 0, 1, 2, 3
mk5breg::led_colour text2colour(const std::string& s);

// and now, without further ado... *drumroll* ... the ioboard interface!
// You can create as many of these as you like; it's implemented as a singleton
// which will attempt to initialize at first access. If something's fishy,
// an exception will be thrown
class ioboard_type {
    public:

        // In order to keep track of what kind of hardware is
        // present in the machine, we set flags. Ssometimes we
        // have subtypes or combations of hardware that are
        // known under some alias.
        // Eg. the mark5b comes in two flavours, dim and dom.
        // For some of the code it doesn't matter which flavour,
        // as long as we know if its a mark5b or not. So
        // you'll see the dim/dom flag actually being two bits:
        // dim = (mark5bflag|dimflag), dom=(mark5bflag|domflag) 
        enum iob_flags {
            mk5a_flag, mk5b_flag, dim_flag,
            dom_flag, fpdp_II_flag, amazon_flag
        };

        // Ok, keep the flags in a "flagset". For now we can get away with
        // unsigned char [<= 8 bitflags], and we disallow multiple initialization
        typedef flagset_type<iob_flags, unsigned char, false> iobflags_type;
        // Create typedefs of the dependant flagdescr and flagmap so we always have
        // the correct types of both at hand
        typedef iobflags_type::flag_descr_type                iobflagdescr_type;
        typedef iobflags_type::flag_map_type                  iobflagmap_type;

        // only feature default c'tor. Will go and look which 
        // kind of board can be found. Throws if nothing can be
        // found
        ioboard_type();

        // Get the input/outpdesign revisions
        unsigned short        idr( void ) const;
        unsigned short        odr( void ) const;

        // RO-access to what h/w was found on this machine
        const iobflags_type&  hardware( void ) const;

        // access mk5a registers
        typedef reg_pointer<mk5areg::regtype::base_type> mk5aregpointer;

        mk5aregpointer operator[]( mk5areg::ipb_regname rname ) const;
        mk5aregpointer operator[]( mk5areg::opb_regname rname ) const;

        // access mk5b registers
        typedef reg_pointer<mk5breg::regtype::base_type> mk5bregpointer;

        mk5bregpointer operator[]( mk5breg::dim_register rname ) const;
        mk5bregpointer operator[]( mk5breg::dom_register rname ) const;

        // Program the Mk5B clockchip. This function works on
        // both Mk5B/DIM and Mk5B/DOM.
        // The (implicit) unit of 'f' is MHz (ie: use 32.0 for
        // 32MHz ...)
        // Note: When called on a not-Mark5B ... you *know* you're getting
        // an exception!
        void setMk5BClock( double f ) const;

        void dbg( void ) const;

        // Access to I/O ports

        // Read sizeof(T) from port 'port'
        // It cannot find the template by argument-deduction
        // so you'll have to call it via
        // ioboard_type  iob;
        // iob.inb<unsigned int>()
        template <typename T>
        T inb( off_t port ) {
            T     rv;
            off_t base;
            // check if the 'ports' device is available
            ASSERT2_POS( portsfd, SCINFO(" - The device (" << hardware_found
                                         << ") does not have I/O ports") );
            // Get current base
            ASSERT_COND( (base=::lseek(portsfd, 0, SEEK_CUR))!=(off_t)-1 );
            // Seek to the requested port [which is done relative to the last position]
            ASSERT_COND( ::lseek(portsfd, port, SEEK_CUR)!=(off_t)-1 );
            // read a value
            ASSERT_COND( ::read(portsfd, &rv, sizeof(T))==sizeof(T) );
            DEBUG(3, "Read " << hex_t(rv) << " from port " << hex_t(port)
                     << " (base=" << hex_t(base) << ")" << std::endl);
            // put the filepointer back to where it was
            ASSERT_COND( ::lseek(portsfd, base, SEEK_SET)!=(off_t)-1 );
            return rv;
        }

        // and write a value to I/O port 'port'
        template <typename T>
        void oub( off_t port, const T& t ) {
            off_t base;
            // check if the 'ports' device is available
            ASSERT2_POS( portsfd, SCINFO(" - The device (" << hardware_found
                                         << ") does not have I/O ports") );
            // Figure out where the pointer is at, currently
            ASSERT_COND( (base=::lseek(portsfd, 0, SEEK_CUR))!=(off_t)-1 );
            // Seek to the requested port
            ASSERT_COND( ::lseek(portsfd, port, SEEK_CUR)!=(off_t)-1 );
            // write the value
            DEBUG(3, "Writing " << hex_t(t) << " to port " << hex_t(port)
                     << " (base=" << hex_t(base) << ")" << std::endl);
            ASSERT_COND( ::write(portsfd, &t, sizeof(T))==sizeof(T) );
            // put the filepointer back to where it was
            ASSERT_COND( ::lseek(portsfd, base, SEEK_SET)!=(off_t)-1 );
            return;
        }

        // doesn't quite do anything...
        ~ioboard_type();

    private:
        // How many references to the ioboard?
        // if the destructor detects the refcount going
        // to zero the state will be returned to "uninitialized"
        // (thus having the ability to do a proper cleanup)
        static unsigned long long int   refcount;
        // the hardware that's found
        static iobflags_type            hardware_found;

        // pointers to the boardregions [input and output]
        // Eithter both are zero [ie the ioboard subsystem not
        // initialized OR they are both nonzero so testing
        // just one for existance is good enough for initializationness
        // testing]
        static unsigned short*          inputdesignrevptr;
        static unsigned short*          outputdesignrevptr;

        // These be memorymapped MEM regions.
        static volatile unsigned char*  ipboard;
        static volatile unsigned char*  opboard;

        // I/O Ports should be accessed via filedescriptor
        // (seek + read/write). If the filedescriptor>=0
        // they're assumed to be opened
        static volatile int             portsfd;

        // Do the initialization. Note: will be done unconditionally.
        // It's up the the caller to decide wether or not to call this
        // function. Will throw up in case of fishyness.
        void                            do_initialize( void );

        typedef std::vector<unsigned long> pciparms_type;
        // board-specific initializers. they're supposed to set up the boards
        // and fill in the pointers to the registers. They also work
        // unconditionally. It is the callers responsibility to (only) call them
        // when appropriate
        void                            do_init_mark5a( const pciparms_type& pci );
        // generic init fn for mark5b. Should do basic init and possibly
        // fork out to specific mk5b/{dim|dom} fns -> as long as the
        // boardtype gets changed from mk5b_generic to the appropriate type
        void                            do_init_mark5b( const pciparms_type& pci );
        void                            do_init_mark5b_dim( const pciparms_type& pci );

        // and the cleanup routines. Will be called automagically by
        // the destructor iff nobody references the ioboard anymore
        // and by looking at the boardtype
        void                            do_cleanup_mark5a( void );
        void                            do_cleanup_mark5b( void );

        // Misc fn's


        // Prohibit these because it is sematically nonsense,
        // even though technically it would not be a problem
        // to allow copy/assignment.
        ioboard_type( const ioboard_type& );
        const ioboard_type& operator=( const ioboard_type& );

};

#endif // includeguard
