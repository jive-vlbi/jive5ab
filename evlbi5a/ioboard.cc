// implementation
#include <ioboard.h>
#include <sstream>
#include <vector>

// C-stuff
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> // for mmap(2)
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>   // for feof(3)


// our own stuff
#include <dosyscall.h>
#include <evlbidebug.h>
#include <hex.h>

using namespace std;

// the static datamembers of ioboard have been declared, now define them
// so the compiler will actually reserve space for them
unsigned long long int      ioboard_type::refcount           = 0ULL;
ioboard_type::iobflags_type ioboard_type::hardware_found     = ioboard_type::iobflags_type();
unsigned short*             ioboard_type::inputdesignrevptr  = 0;
unsigned short*             ioboard_type::outputdesignrevptr = 0;
volatile unsigned char*     ioboard_type::ipboard            = 0;
volatile unsigned char*     ioboard_type::opboard            = 0;
volatile int                ioboard_type::portsfd            = -1;


// the exception
DEFINE_EZEXCEPT(ioboardexception);

// Failure to insert entry in map
DECLARE_EZEXCEPT(failed_insert_of_flag_in_iobflags_map);
DEFINE_EZEXCEPT(failed_insert_of_flag_in_iobflags_map);


//
//            Mark5A  i/o board register definitions
//
const mk5areg::ipb_registermap& mk5areg::ipb_registers( void ) {
    static ipb_registermap       __map = mk5areg::ipb_registermap();

    // only fill it once.
    if( __map.empty() ) {
        DEBUG(3, "Start building Mark5A inputboard registermap" << endl);


        // notclock is bit 0 in word 4
        __map.insert( make_pair(mk5areg::notClock, regtype(1, 0, 4)) );
        // w_clk seems to be bit 0 in word 2
        __map.insert( make_pair(mk5areg::W_CLK, regtype(1, 0, 2)) );
        // U is 'FQ_UD' is bit 9 in word 0
        __map.insert( make_pair(mk5areg::FQ_UD, regtype(1, 9, 0)) );
        // The 'W' register is bits 0:7 in word 0
        __map.insert( make_pair(mk5areg::W, regtype(8, 0, 0)) );

        // mode (seems to be) an 8bit field in word 1, starting at bit0
        __map.insert( make_pair(mk5areg::mode, regtype(8, 0, 1)) );

        // the vlba field (seems to be) a 4bit field in word1, starting
        // at bit 9
        __map.insert( make_pair(mk5areg::vlba, regtype(4, 9, 1)) );

        // The 'R'(eset?) bit is bit 9 in word 0
        __map.insert( make_pair(mk5areg::R, regtype(1, 9, 0)) );


        // the inputboard errorbits are in word3
        __map.insert( make_pair(mk5areg::errorbits, regtype(3)) );

        __map.insert( make_pair(mk5areg::ip_word0, regtype(0)) );
        __map.insert( make_pair(mk5areg::ip_word1, regtype(1)) );
        __map.insert( make_pair(mk5areg::ip_word2, regtype(2)) );
//        __map.insert( make_pair(mk5areg::ip_word3, regtype(3)) );
        __map.insert( make_pair(mk5areg::ip_word4, regtype(4)) );
//        __map.insert( make_pair(mk5areg::ip_word4, regtype(5)) );

        DEBUG(3, "Finished building Mark5A inputboard registermap - it has " << __map.size() << " entries" << endl);
    }
    return __map;
}

const mk5areg::opb_registermap& mk5areg::opb_registers( void ) {
    static opb_registermap       __map = mk5areg::opb_registermap();

    // only fill it once.
    if( __map.empty() ) {
        DEBUG(3, "Start building Mark5A outputboard registermap" << endl);
        // Word 0:
        // Ch A: 6bits, starting from bit 0
        // Ch b: 6bits, starting from bit 8
        __map.insert( make_pair(mk5areg::ChASelect, regtype(6, 0, 0)) );
        __map.insert( make_pair(mk5areg::ChBSelect, regtype(6, 8, 0)) );

        // Word 1: flags
        __map.insert( make_pair(mk5areg::F, regtype(1, 15, 1)) );
        __map.insert( make_pair(mk5areg::I, regtype(1, 12, 1)) );
        __map.insert( make_pair(mk5areg::AP, regtype(1, 11, 1)) );
        __map.insert( make_pair(mk5areg::AP1, regtype(1, 10, 1)) );
        __map.insert( make_pair(mk5areg::AP2, regtype(1, 9, 1)) );
        __map.insert( make_pair(mk5areg::V, regtype(1, 8, 1)) );
        __map.insert( make_pair(mk5areg::SF, regtype(1, 7, 1)) );
        // still word 1: the code field
        __map.insert( make_pair(mk5areg::CODE, regtype(4, 0, 1)) );

        // Word 2: only two flags?
        __map.insert( make_pair(mk5areg::C, regtype(1, 1, 2)) );
        __map.insert( make_pair(mk5areg::Q, regtype(1, 0, 2)) );

        // Word 3: only a single flag defined
        __map.insert( make_pair(mk5areg::S, regtype(1, 0, 3)) );

        // Word 4: number of syncs
        __map.insert( make_pair(mk5areg::NumberOfReSyncs, regtype(4)) );

        // Word 5: DIM revision
        __map.insert( make_pair(mk5areg::DIMRev, regtype(8, 0, 5)) );

        // 32bit fillpattern spread over 2 16bit words: word6 contains the MSBs
        __map.insert( make_pair(mk5areg::FillPatMSBs, regtype(6)) );
        __map.insert( make_pair(mk5areg::FillPatLSBs, regtype(7)) );
#if 0
        // aliases
        __map.insert( make_pair(mk5areg::op_word0, regtype(0)) );
        __map.insert( make_pair(mk5areg::op_word1, regtype(1)) );
        __map.insert( make_pair(mk5areg::op_word2, regtype(2)) );
        __map.insert( make_pair(mk5areg::op_word3, regtype(3)) );
        __map.insert( make_pair(mk5areg::op_word4, regtype(4)) );
#endif
        DEBUG(3, "Finished building Mark5A outputboard registermap - it has " << __map.size() << " entries" << endl);
    }
    return __map;
}


#define KEES(o,k) \
    case mk5areg::k: o << #k; break;

ostream& operator<<(ostream& os, mk5areg::ipb_regname r ) {
    switch( r ) {
        KEES(os, notClock);
        KEES(os, W_CLK);
        KEES(os, FQ_UD);
        KEES(os, mode);
        KEES(os, vlba);
        KEES(os, R);
        KEES(os, W);
#if 0
        KEES(os, ip_word0);
        KEES(os, ip_word1);
#endif
        default:
            os << "<invalid Mk5A inputboard register #" << (unsigned int)r;
            break;
    }
    return os;
}

ostream& operator<<(ostream& os, mk5areg::opb_regname r ) {
    switch( r ) {
        KEES(os, ChBSelect)
        KEES(os, ChASelect)
        KEES(os, F)
        KEES(os, I)
        KEES(os, AP)
        KEES(os, AP1)
        KEES(os, AP2)
        KEES(os, V)
        KEES(os, SF)
        KEES(os, CODE)
        KEES(os, C)
        KEES(os, Q)
        KEES(os, S)
        KEES(os, NumberOfReSyncs)
        KEES(os, DIMRev)
        KEES(os, FillPatMSBs)
        KEES(os, FillPatLSBs)
#if 0
        KEES(os, op_word1)
#endif
        default:
            os << "<invalid Mk5A outputboard register #" << (unsigned int)r;
            break;
    }
    return os;
}

// Mk5B stuff

// Build the DIM registermap
const mk5breg::dim_registermap& mk5breg::dim_registers( void ) {
    static dim_registermap       __map = mk5breg::dim_registermap();

    if( __map.size() )
        return __map;

    // not filled yet, fill in the map
    __map.insert( make_pair(DIM_LED1,  regtype(2, 14, 1)) );
    __map.insert( make_pair(DIM_LED0,  regtype(2, 12, 1)) );

    __map.insert( make_pair(DIM_REVBYTE, regtype(8, 0, 0xe)) );

    __map.insert( make_pair(DIM_K, regtype(3, 6, 0)) );
    __map.insert( make_pair(DIM_J, regtype(3, 3, 0)) );
    __map.insert( make_pair(DIM_SELPP, regtype(2, 1, 0)) );
    __map.insert( make_pair(DIM_SELCGCLK, regtype(1, 0, 0)) );

    __map.insert( make_pair(DIM_BSM_H, regtype(6)) );
    __map.insert( make_pair(DIM_BSM_L, regtype(5)) );

    __map.insert( make_pair(DIM_USERWORD, regtype(7)) );

    __map.insert( make_pair(DIM_HDR2_H, regtype(0x10)) );
    __map.insert( make_pair(DIM_HDR2_L, regtype(0xf)) );
    __map.insert( make_pair(DIM_HDR3_H, regtype(0x12)) );
    __map.insert( make_pair(DIM_HDR3_L, regtype(0x11)) );

    __map.insert( make_pair(DIM_TVRMASK_H, regtype(0xd)) );
    __map.insert( make_pair(DIM_TVRMASK_L, regtype(0xc)) );

    __map.insert( make_pair(DIM_GOCOM, regtype(1, 0, 1)) );

    __map.insert( make_pair(DIM_REQ_II, regtype(1, 13, 0)) );
    __map.insert( make_pair(DIM_II, regtype(1, 13, 0xe)) );

    __map.insert( make_pair(DIM_SETUP, regtype(1, 1, 0xb)) );
    __map.insert( make_pair(DIM_RESET, regtype(1, 0, 0xb)) );
    __map.insert( make_pair(DIM_STARTSTOP, regtype(1, 4, 0xb)) );
    __map.insert( make_pair(DIM_PAUSE, regtype(1, 5, 0xb)) );

    return __map;
}

const mk5breg::dom_registermap& mk5breg::dom_registers( void ) {
    static dom_registermap       __map = mk5breg::dom_registermap();

    if( __map.size() )
        return __map;

    // not filled yet, fill in the map
    __map.insert( make_pair(DOM_LEDENABLE, regtype(1, 15, 0)) );
    __map.insert( make_pair(DOM_LED0, regtype(2, 6, 9)) );
    __map.insert( make_pair(DOM_LED1, regtype(2, 8, 9)) );

    return __map;
}

#define MK5BKEES(o,a) \
    case mk5breg::a: \
        o << #a; break;\

ostream& operator<<(ostream& os, mk5breg::dim_register regname ) {
    switch( regname ) {
        MK5BKEES(os, DIM_LED0);
        MK5BKEES(os, DIM_LED1);
        MK5BKEES(os, DIM_REVBYTE);
        MK5BKEES(os, DIM_K);
        MK5BKEES(os, DIM_J);
        MK5BKEES(os, DIM_SELPP);
        MK5BKEES(os, DIM_SELCGCLK);
        MK5BKEES(os, DIM_BSM_H);
        MK5BKEES(os, DIM_BSM_L);
        MK5BKEES(os, DIM_USERWORD);
        MK5BKEES(os, DIM_HDR2_H);
        MK5BKEES(os, DIM_HDR2_L);
        MK5BKEES(os, DIM_HDR3_H);
        MK5BKEES(os, DIM_HDR3_L);
        MK5BKEES(os, DIM_TVRMASK_H);
        MK5BKEES(os, DIM_TVRMASK_L);
        MK5BKEES(os, DIM_GOCOM);
        MK5BKEES(os, DIM_REQ_II);
        MK5BKEES(os, DIM_II);
        MK5BKEES(os, DIM_SETUP);
        MK5BKEES(os, DIM_RESET);
        MK5BKEES(os, DIM_STARTSTOP);
        MK5BKEES(os, DIM_PAUSE);
        default:
            os << "<Unhandled DIM regname>";
            break;
    }
    return os;
}

ostream& operator<<(ostream& os, mk5breg::dom_register regname ) {
    switch( regname ) {
        MK5BKEES(os, DOM_LEDENABLE);
        MK5BKEES(os, DOM_LED0);
        MK5BKEES(os, DOM_LED1);
        default:
            os << "<Unhandled DOM regname>";
            break;
    }
    return os;
}

ostream& operator<<(ostream& os, mk5breg::led_color l) {
    switch( l ) {
        MK5BKEES(os, led_red);
        MK5BKEES(os, led_green);
        MK5BKEES(os, led_off);
        MK5BKEES(os, led_blue);
        default:
            os << "<Invalid mk5breg::led_color>";
            break;
    }
    return os;
}

// This function defines the actual map for going from
// ioboard_type::iob_flags enum to actual bits
ioboard_type::iobflags_type::flag_map_type make_iobflag_map( void ) {
    ioboard_type::iobflags_type::flag_map_type                       rv;
    pair<ioboard_type::iobflags_type::flag_map_type::iterator, bool> insres;

    // the mk5a bit
    insres = rv.insert( make_pair(ioboard_type::mk5a_flag,
                                  ioboard_type::iobflagdescr_type(0x1,"Mk5A")) );
    if( !insres.second ) {
        THROW_EZEXCEPT(failed_insert_of_flag_in_iobflags_map, "mk5a_flag");
    }

    // mk5b
    insres = rv.insert( make_pair(ioboard_type::mk5b_flag,
                                  ioboard_type::iobflagdescr_type(0x2,"Mk5B")) );
    if( !insres.second )
        THROW_EZEXCEPT(failed_insert_of_flag_in_iobflags_map, "mk5b_flag");

    // The DIM-flag is a combination of Mk5B + DIM
    insres = rv.insert( make_pair(ioboard_type::dim_flag,
                                  ioboard_type::iobflagdescr_type(0x4|0x2, "DIM")) );
    if( !insres.second )
        THROW_EZEXCEPT(failed_insert_of_flag_in_iobflags_map, "dim_flag");

    // DOM is a combination of Mk5B + a DOM flag
    insres = rv.insert( make_pair(ioboard_type::dom_flag,
                                  ioboard_type::iobflagdescr_type(0x8|0x2, "DOM")) );
    if( !insres.second )
        THROW_EZEXCEPT(failed_insert_of_flag_in_iobflags_map, "dom_flag");

    // fpdp_II_flag means: use FPDP2
    // [only in mk5b/amazon? Needs investigation]


    // AMAZON flag is set when the Streamstor has a amazon daughter board
    // should this be here?!

    // Ok map filled!
    return rv;
}


//
// The ioboard thingy
//
ioboard_type::ioboard_type() {
    // see if we need to initialize the flagmap
    if( iobflags_type::get_flag_map().empty() )
        iobflags_type::set_flag_map( make_iobflag_map() );
    // see if we need to find hardware
    if( !refcount )
        do_initialize();
    refcount++;
}

const ioboard_type::iobflags_type& ioboard_type::hardware( void ) const {
    return hardware_found;
}

unsigned short ioboard_type::idr( void ) const {
    ASSERT_NZERO( inputdesignrevptr );
    return *inputdesignrevptr;
}

unsigned short ioboard_type::odr( void ) const {
    ASSERT_NZERO( outputdesignrevptr );
    return *outputdesignrevptr;
}


ioboard_type::mk5aregpointer ioboard_type::operator[]( mk5areg::ipb_regname rname ) const {
    // look for the given name in the mk5aregisterset
    const mk5areg::ipb_registermap&          regmap( mk5areg::ipb_registers() );
    mk5areg::ipb_registermap::const_iterator curreg;

    // assert that the current ioboard is a mark5a board!
    ASSERT_COND( (hardware_found&mk5a_flag)==true );

    // assert we can find the register in the descriptors
    ASSERT2_COND( ((curreg=regmap.find(rname))!=regmap.end()),
                  SCINFO(" registername (rname) = " << rname) );

    return mk5aregpointer(curreg->second, (volatile mk5areg::regtype::register_type*)ipboard);
}


ioboard_type::mk5aregpointer ioboard_type::operator[]( mk5areg::opb_regname rname ) const {
    // look for the given name in the mk5aregisterset
    const mk5areg::opb_registermap&          regmap( mk5areg::opb_registers() );
    mk5areg::opb_registermap::const_iterator curreg;

    // assert that the current ioboard is a mark5a board!
    ASSERT_COND( (hardware_found&mk5a_flag)==true );

    // assert we can find the register in the descriptors
    ASSERT2_COND( ((curreg=regmap.find(rname))!=regmap.end()),
                  SCINFO(" registername (rname) = " << rname) );

    return mk5aregpointer(curreg->second, (volatile mk5areg::regtype::register_type*)opboard);
}

ioboard_type::mk5bregpointer ioboard_type::operator[]( mk5breg::dim_register rname ) const {
    // look for the given name in the mk5aregisterset
    const mk5breg::dim_registermap&          regmap( mk5breg::dim_registers() );
    mk5breg::dim_registermap::const_iterator curreg;

    // assert that the current ioboard is a mark5b/DIM board!
    ASSERT_COND( (hardware_found&dim_flag)==true );

    // assert we can find the register in the descriptors
    ASSERT2_COND( ((curreg=regmap.find(rname))!=regmap.end()),
                  SCINFO(" registername (rname) = " << rname) );

    return mk5bregpointer(curreg->second, (volatile mk5breg::regtype::register_type*)opboard);
}

ioboard_type::mk5bregpointer ioboard_type::operator[]( mk5breg::dom_register rname ) const {
    // look for the given name in the mk5aregisterset
    const mk5breg::dom_registermap&          regmap( mk5breg::dom_registers() );
    mk5breg::dom_registermap::const_iterator curreg;

    // assert that the current ioboard is a mark5b/DOM board!
    ASSERT_COND( (hardware_found&dom_flag)==true );

    // assert we can find the register in the descriptors
    ASSERT2_COND( ((curreg=regmap.find(rname))!=regmap.end()),
                  SCINFO(" registername (rname) = " << rname) );

    return mk5bregpointer(curreg->second, (volatile mk5breg::regtype::register_type*)ipboard);
}

void ioboard_type::dbg( void ) const {
    // depending on which flavour of Mark5 we're executing on, dump
    // different registers
    if( hardware_found&mk5a_flag ) {
        cout << "Dumping regs from " << hardware_found << endl;
        cout << "IP0:2 " << hex_t(ipboard, 3) << endl;
        cout << "IP3:5 " << hex_t(ipboard+3, 3) << endl;
        cout << "OP0:2 " << hex_t(opboard, 3) << endl;
        cout << "OP3:5 " << hex_t(opboard+3, 3) << endl;
        cout << "OP6:8 " << hex_t(opboard+6, 3) << endl;
    } else {
        cout << "dbg() not (yet) supported for the following hardware: " << hardware_found << endl;
    }
    return;
}

ioboard_type::~ioboard_type() {
    if( !(--refcount) ) {
        DEBUG(1, "Closing down ioboard" << endl);
        // check which cleanup fn to call
        // For Mk5B we don't give a crap wether it's dom or dim
        if( hardware_found&mk5a_flag )
            do_cleanup_mark5a();
        else if( hardware_found&mk5b_flag )
            do_cleanup_mark5b();
        else {
            DEBUG(0, "Attempt to cleanup unknown boardtype [this is a no-op]. It's suspicious.");
        }
        // If the ports filedescriptor is open, close it!
        if( portsfd>=0 )
            ::close( portsfd );
        // and invalidate static datamembers
        hardware_found.clr_all();
        ipboard           = opboard            = 0;
        inputdesignrevptr = outputdesignrevptr = 0;
        portsfd            = -1;
    }
}


void ioboard_type::do_initialize( void ) {
    // start loox0ring for mark5a/b board
    const char*        seps = " \t";
    const string       devfile( "/proc/bus/pci/devices" );
    // PCI vendor:subvendor 10b5 == PLX, 3001 = Dan (Smythe?) Mark5A I/O board
    const unsigned int mk5a_tag = 0x10b53001;
    const unsigned int mk5b_tag = 0x10b59030; // id. but then 9030 == Mark5B I/O board

    // nonconst stuff
    char            linebuf[1024];
    FILE*           fp;
    unsigned int    lineno;
    pciparms_type   pciparms;

    DEBUG(1, "Start looking for a Mark5[AB] ioboard" << endl);

    // Before we (re)start, return to initial state
    // Yes, this could cause a prob if we call this w/o
    // having cleaned up first. Let's hope (...) the
    // reference counting + cleanup fn's do their job correct
    hardware_found.clr_all();
    inputdesignrevptr  = 0;
    outputdesignrevptr = 0;
    ipboard            = 0;
    opboard            = 0;
    if( portsfd>=0 )
        ::close( portsfd );
    portsfd            = -1;

    // Open /proc/bus/pci/devices and see if we can find a recognized device
    ASSERT2_NZERO( (fp=::fopen(devfile.c_str(), "r")),
                    SCINFO(" - devfile '" << devfile << "'"); );

    // read each line from the file, as long there are no
    // flags set in 'hardware_found' and there are lines
    // available, obviously
    lineno = 0;
    while( hardware_found.empty() && ::feof(fp)==0 ) {
        char*        sptr;
        char*        eptr;
        char*        entry;

        // Read nxt line
        ASSERT_NZERO( fgets(linebuf, sizeof(linebuf), fp) );

        // break up the line in unsigned longs. Hmmm.. They are
        // printed as hex but w/o leading 0x. Add some magik.
        pciparms.resize( 0 );

        // use strtok to breakup the line
        sptr = linebuf;
        while( (entry=::strtok_r(sptr, seps, &eptr))!=0 ) {
            unsigned long   tmp;

            // Stop scanning at the first failure reading an unsigned long in hex
            if( ::sscanf(entry, "%lx", &tmp)!=1 )
                break;
            // otherwise, stick it at the end of parameters read so far
            pciparms.push_back( tmp );
            // after one iteration this is redundant but at least it's now a neat, single
            // while-loop (otherwise you first have to call strtok() with a non-null first argument
            // and then in a while-loop call it with a NULL first argument so you get to do the
            // errorhandling twice which is yucky
            sptr = 0;
        }
        DEBUG(3, "Read " << pciparms.size() << " entries from " << devfile << ":" << lineno << endl);
        // Assert we have read enough values. Two should be fine here (the PCI-VendorId is in 
        // field #2)
        ASSERT2_COND( pciparms.size()>=2,
                      SCINFO(" - " << devfile << ":" << lineno << " not enough info read") );

        // See what we got. The PCI-ID is in field #1
        if( pciparms[1]==mk5a_tag )
            // it's a Mark5A!
            hardware_found|=mk5a_flag;
        else if( pciparms[1]==mk5b_tag )
            // Ok. We now know itz a mark5b. Later on
            // we will find out if itz a DIM or a DOM
            hardware_found|=mk5b_flag;
        lineno++;
    }
    ::fclose( fp );

    // If still no bits set in 'hardware_found', we didn't actually find anything
    // recognizable
    ASSERT2_COND( hardware_found.empty()==false,
                  SCINFO(" - No recognized hardware found") );

    // Go on with initialization
    if( hardware_found&mk5a_flag )
        do_init_mark5a( pciparms );
    else if( hardware_found&mk5b_flag )
        do_init_mark5b( pciparms );
    else {
        ASSERT2_NZERO( 0,
                       SCINFO(" - internal error: unknown hardware " << hardware_found << " found; "
                           << "part of the system recognized it but at this point "
                           << "it certainly isn't anymore") );
    }
    DEBUG(1, "Found the following hardware: " << hardware_found << " board" << endl);
}


void ioboard_type::do_init_mark5a( const ioboard_type::pciparms_type& pci ) {
    // const values
    const string    memory( "/dev/mem" );
    // local variables
    int             fd;
    unsigned long   reg1_offset; // offset to region 1 of PCI board
    unsigned long   reg2_offset; // offset to region 2 of PCI board
    volatile void*  mmapptr;

    // Over here, we need at least 6 parameters?
    ASSERT2_COND( pci.size()>=6, SCINFO(" not enough parameters from pciparameters for Mark5A ioboard") );

    // This is taken literally (albeit rewritten in my variables) from the original
    // IOBoard.c :
    // /* Here we've found either TAG or TAGB */ 
    // offp = t4; 
    // offp--; /* Why?! */ 
    // offs = t5; 
    reg1_offset = pci[4];
    reg1_offset--;
    reg2_offset = pci[5];

    // Attempt to open system memory. This is why this blasted thang
    // needs to be suid root!
    ASSERT2_POS( (fd=::open(memory.c_str(), O_RDWR)),
                  SCINFO(" memory=" << memory) );

    // And mmap the boardregion into this application
    ASSERT2_COND( (mmapptr=(volatile void*)::mmap(0, mk5areg::mmapregionsize,
                      PROT_READ|PROT_WRITE, MAP_SHARED, fd, reg2_offset))!=MAP_FAILED,
                   ::close(fd) );
    DEBUG(2, " Mk5A: memorymapped " << mk5areg::mmapregionsize << " bytes of memory" << endl);
    // weeheee!
    ipboard = (volatile unsigned char*)mmapptr;
    // outputboard is at offset 32*16bit words wrt inputboard
    opboard = (volatile unsigned char*)(((volatile unsigned short*)ipboard) + 32);

    // The in/out design revisions are in [unsigned short] word #5 of both 
    // boardregions
    inputdesignrevptr  = ((unsigned short*)ipboard) + 5;
    outputdesignrevptr = ((unsigned short*)opboard) + 5;
  
    ::close( fd );

    DEBUG(1,"Mk5A: Found IDR: " << hex_t(*inputdesignrevptr)
            << " ODR: " << hex_t(*outputdesignrevptr) << endl);

    // cf IOBoard.c we need to "pulse" the "R" register (ie 
    // make it go from 0 -> 1 -> 0 [to trigger R(eset) I presume]
    mk5aregpointer  r = (*this)[ mk5areg::R ];

    r = 0;
    usleep( 1 );
    r = 1;
    usleep( 2000 );
    r = 0;

    unsigned int  fill = 0x11223344;

    (*this)[ mk5areg::I ]   = 0;
    (*this)[ mk5areg::AP ]  = 0;
    (*this)[ mk5areg::AP1 ] = 0;
    (*this)[ mk5areg::AP2 ] = 0;
    (*this)[ mk5areg::V ]   = 0;
    (*this)[ mk5areg::SF ]  = 0;
    // enable Fill detection + set default fillpattern
    (*this)[ mk5areg::F ]           = 1;
    (*this)[ mk5areg::FillPatMSBs ] = fill>>16;
    (*this)[ mk5areg::FillPatLSBs ] = fill; // will be automagically masked by the registerdefinition!
    
    return;
}

// no need to invalidate pointers, caller of this method will do
// that
void ioboard_type::do_cleanup_mark5a( void ) {
    DEBUG(2, "Starting to clean-up mark5a ioboard" << endl);
    ASSERT_ZERO( ::munmap((void*)ipboard, mk5areg::mmapregionsize) );

    ipboard           = opboard            = 0;
    inputdesignrevptr = outputdesignrevptr = 0;
    portsfd            = -1;
}


// We should only enter this function when the current ioboard-type 
// is mark5b-generic. We analyze/set up the Mk5B i/o board.
void ioboard_type::do_init_mark5b( const ioboard_type::pciparms_type& pci ) {
    // consts
    const std::string     memory( "/dev/mem" );
    const std::string     ports( "/dev/port" );
    // Static DIM input-designrevision [it's somewhere where
    // we cannot just point at, apparently]
    static unsigned short dim_inputdesignrev;
    // variables
    int                   memfd;
    unsigned long         off0, off1, off2;
    unsigned long         siz0, siz1, siz2;
    volatile void*        mmapptr;

    // We must have at least 12 parameters for the Mk5B board
    ASSERT2_COND( pci.size()>=12,
                  SCINFO(" need at least 12 params from /proc/bus/pci "
                         << "for Mark5B I/O board, got " << pci.size()) );
    // From:
    // <<<< IOBoard.c >>>>
    // found = 2; /* Yes, Mark-5B board */ 
    // off0 = t3;
    // off1 = t4;
    // off1--; /* Why?! */
    // off2 = t5;
    // siz0 = t10;
    // siz1 = t11;
    // siz2 = t12; 
    // << snip >>
    //    /* Here we've found either TAG or TAGB */ 
    // offp = t4; 
    // offp--; /* Why?! */ 
    // offs = t5; 
    // <<<< /IOBoard.c >>>>
    // So it would seem that "off1" and "offp" are synonyms
    // as well as "off2" and "offs"
    off0 = pci[3];
    off1 = pci[4]-1; // *why*?!
    off2 = pci[5];
    siz0 = pci[10];
    siz1 = pci[11];
    siz2 = pci[12];

    DEBUG(3, "Found I/O region (1) @" << hex_t(off1) << ", size " << hex_t(siz1) << endl);
    DEBUG(3, "Found MEM region (2) @" << hex_t(off2) << ", size " << hex_t(siz2) << endl);

    // As per IOBoard.c,
    // we first open the ports and initialize the mk5b board a bit.
    ASSERT2_POS( (portsfd=::open(ports.c_str(), O_RDWR)),
                 SCINFO(" ports=" << ports); this->do_cleanup_mark5b(); );
    // Seek to the start of region 1 [the I/O base-adress of the Mark5B I/O board]
    // The inb() and oub() will make sure they retain the origin.
    ASSERT2_COND( (::lseek(portsfd, (off_t)off1, SEEK_SET)!=(off_t)-1),
                  SCINFO( " Failed to seek to start of I/O region of Mk5B board.");
                  this->do_cleanup_mark5b(); );

    // <IOBoard.c> 
    //   Set 32-bit I/O port 0x54 to 0622222222 (octal) per Will
    //   (We need to do this before we mess with Region 2 per Brian)
    //  ... <snip> ..
    //   Set 32-bit I/O port 0x4c per Brian
    //   add = 0x4c; 
    //   bufx = 0x41; /* ?? Or 0x300041 ??
    //   (If DOM, this might be changed later)
    //   if ((k = SIOlong5B(add, bufx)) < 0) { /* OK? */ 
    // </IOBoard.c>
    // Note: no need to check for return value: fn will throw
    // upon failure ...
    this->oub<mk5breg::ioport_type>( 0x54,  0622222222 );
    DEBUG(2, "Read " << hex_t(this->inb<unsigned int>(0x4c)) << " from port 0x4c" << endl);
    this->oub<mk5breg::ioport_type>( 0x4c,  0x41 );

    // Now it's about time to mmap the registers into memory
    // So: open /dev/memory
    ASSERT2_POS( (memfd=::open(memory.c_str(), O_RDWR)),
                 SCINFO(" memory=" << memory); this->do_cleanup_mark5b(); );

    // and mmap the boardregion into this application [that would be
    // region 2!]
    ASSERT2_COND( (mmapptr=(volatile void*)::mmap(0, mk5breg::mmapregionsize,
                      PROT_READ|PROT_WRITE, MAP_SHARED, memfd, off2))!=MAP_FAILED,
                   ::close(memfd); this->do_cleanup_mark5b(); );
    DEBUG(2, "Mk5B: memorymapped " << mk5breg::mmapregionsize << " bytes of memory" << endl);

    // Now points at the inputboard!
    ipboard = (volatile unsigned char*)mmapptr;

    // If ((unsigned short*)ipboard)[7] & 0xff00 != 0x5b00 ... we're in trouble!
    ASSERT2_COND( (((volatile unsigned short*)ipboard)[7]&0xff00)==0x5b00,
                  SCINFO("Not Mk5B?" << endl << hex_t((volatile unsigned short*)ipboard, 80));
                  this->do_cleanup_mark5b(); );
    // opboard is-at base + 0x3f00 [in units of unsigned short!]
    opboard = (volatile unsigned char*)((volatile unsigned short*)ipboard + 0x3f00);

    // Set input/outputdesignrevistion ptrs [idr may be changed later]
    inputdesignrevptr = outputdesignrevptr = ((unsigned short*)ipboard) + 7;

    // Check if it is a DIM or a DOM
    if( ((volatile unsigned short*)ipboard)[7] & 0x80 ) {
        // This is taken to mean that we have a DIM!
        hardware_found.set(dim_flag);
        // Inputdesignrev can be found in one of the registers
        dim_inputdesignrev = *((*this)[mk5breg::DIM_REVBYTE]);
        inputdesignrevptr  = &dim_inputdesignrev;
    } else {
        // this seems to mean DOM
        hardware_found.set(dom_flag);
    }
    DEBUG(1,"Mk5B: Found IDR: " << hex_t(*inputdesignrevptr)
            << " ODR: " << hex_t(*outputdesignrevptr) << endl);
    return;
}


void ioboard_type::do_cleanup_mark5b( void ) {
    DEBUG(2, "Starting to clean-up mark5b ioboard" << endl);
    // Close the portsfile
    if( portsfd>=0 )
        ::close( portsfd );
    // Release memorymapped region, if any
    ASSERT_ZERO( ::munmap((void*)ipboard, mk5breg::mmapregionsize) );

    ipboard           = opboard            = 0;
    inputdesignrevptr = outputdesignrevptr = 0;
    portsfd            = -1;
    return;
}
