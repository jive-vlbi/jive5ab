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


// our own stuff
#include <dosyscall.h>
#include <evlbidebug.h>
#include <hex.h>

using namespace std;

// the static datamembers of ioboard have been declared, now define them
// so the compiler will actually reserve space for them
unsigned long long int   ioboard_type::refcount           = 0ULL;
ioboard_type::board_type ioboard_type::boardtype          = ioboard_type::unknown_boardtype;
unsigned short*          ioboard_type::inputdesignrevptr  = 0;
unsigned short*          ioboard_type::outputdesignrevptr = 0;
volatile unsigned short* ioboard_type::ipboard            = 0;
volatile unsigned short* ioboard_type::opboard            = 0;



// the exception
ioboardexception::ioboardexception( const string& m ):
    message( m )
{}

const char* ioboardexception::what( void ) const throw() {
    return message.c_str();
}
ioboardexception::~ioboardexception( void ) throw()
{}



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
        __map.insert( make_pair(mk5areg::errorbits, regtype(16, 0, 3)) );

        __map.insert( make_pair(mk5areg::ip_word0, regtype(16, 0, 0)) );
        __map.insert( make_pair(mk5areg::ip_word1, regtype(16, 0, 1)) );
        __map.insert( make_pair(mk5areg::ip_word2, regtype(16, 0, 2)) );
//        __map.insert( make_pair(mk5areg::ip_word3, regtype(16, 0, 3)) );
        __map.insert( make_pair(mk5areg::ip_word4, regtype(16, 0, 4)) );
//        __map.insert( make_pair(mk5areg::ip_word4, regtype(16, 0, 5)) );

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
        __map.insert( make_pair(mk5areg::NumberOfReSyncs, regtype(16, 0, 4)) );

        // Word 5: DIM revision
        __map.insert( make_pair(mk5areg::DIMRev, regtype(8, 0, 5)) );

        // 32bit fillpattern spread over 2 16bit words: word6 contains the MSBs
        __map.insert( make_pair(mk5areg::FillPatMSBs, regtype(16, 0, 6)) );
        __map.insert( make_pair(mk5areg::FillPatLSBs, regtype(16, 0, 7)) );
#if 0
        // aliases
        __map.insert( make_pair(mk5areg::op_word0, regtype(16, 0, 0)) );
        __map.insert( make_pair(mk5areg::op_word1, regtype(16, 0, 1)) );
        __map.insert( make_pair(mk5areg::op_word2, regtype(16, 0, 2)) );
        __map.insert( make_pair(mk5areg::op_word3, regtype(16, 0, 3)) );
        __map.insert( make_pair(mk5areg::op_word4, regtype(16, 0, 4)) );
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



//
// The ioboard thingy
//
ioboard_type::ioboard_type() {
    if( !refcount )
        do_initialize();
    refcount++;
}
ioboard_type::board_type ioboard_type::boardType( void ) const {
    return boardtype;
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
    ASSERT_COND( boardtype==ioboard_type::mk5a );

    // assert we can find the register in the descriptors
    ASSERT2_COND( ((curreg=regmap.find(rname))!=regmap.end()),
                  SCINFO(" registername (rname) = " << rname) );

    return mk5aregpointer(curreg->second, ipboard);
}


ioboard_type::mk5aregpointer ioboard_type::operator[]( mk5areg::opb_regname rname ) const {
    // look for the given name in the mk5aregisterset
    const mk5areg::opb_registermap&          regmap( mk5areg::opb_registers() );
    mk5areg::opb_registermap::const_iterator curreg;

    // assert that the current ioboard is a mark5a board!
    ASSERT_COND( boardtype==ioboard_type::mk5a );

    // assert we can find the register in the descriptors
    ASSERT2_COND( ((curreg=regmap.find(rname))!=regmap.end()),
                  SCINFO(" registername (rname) = " << rname) );

    return mk5aregpointer(curreg->second, opboard);
}

void ioboard_type::dbg( void ) const {

    cout << "IP0:2 " << hex_t(ipboard, 3) << endl;
    cout << "IP3:5 " << hex_t(ipboard+3, 3) << endl;
    cout << "OP0:2 " << hex_t(opboard, 3) << endl;
    cout << "OP3:5 " << hex_t(opboard+3, 3) << endl;
    cout << "OP6:8 " << hex_t(opboard+6, 3) << endl;
    return;
}

ioboard_type::~ioboard_type() {
    if( !(--refcount) ) {
        DEBUG(1, "Closing down " << boardtype << " ioboard" << endl);
        // check which cleanup fn to call
        switch( boardtype ) {
            case ioboard_type::mk5a:
                do_cleanup_mark5a();
                break;
            case ioboard_type::mk5b:
                do_cleanup_mark5b();
                break;
            case ioboard_type::unknown_boardtype:
                // hmmm?
                DEBUG(0, "Attempt to cleanup unknown boardtype [this is a no-op]. It's suspicious.");
                break;
            default:
                // this is an error i think!
                ASSERT2_NZERO(0, SCINFO(" invalid boardtype " << (unsigned int)boardtype << "; we do "
                                        << "not recognize it whilst cleanup is needed!") ); 
                break;
        }
        // and invalidate static datamembers
        boardtype         = ioboard_type::unknown_boardtype;
        ipboard           = opboard            = 0;
        inputdesignrevptr = outputdesignrevptr = 0;
    }
}


// Show the boardtype as readable format
ostream& operator<<(ostream& os, ioboard_type::board_type bt) {
    switch( bt ) {
        case ioboard_type::unknown_boardtype:
            os << "No/unknown";
            break;
        case ioboard_type::mk5a:
            os << "Mark5A";
            break;
        case ioboard_type::mk5b:
            os << "Mark5B";
            break;
        default:
            os << "<Invalid boardtype #" << (unsigned int)bt << ">";
            break;
    }
    return os;
}
void ioboard_type::do_initialize( void ) {
    // start loox0ring for mark5a/b board
    const char*        seps = " \t";
    const string       devfile( "/proc/bus/pci/devices" );
    const unsigned int mk5a_tag = 0x10b53001; // PCI vendor:subvendor 10b5 == PLX, 3001 = Dan (Smythe?) Mark5A I/O board
    const unsigned int mk5b_tag = 0x10b59030; // id. but then 9030 == Mark5B I/O board

    // nonconst stuff
    char            linebuf[1024];
    FILE*           fp;
    unsigned int    lineno;
    pciparms_type   pciparms;

    DEBUG(1, "Start looking for a Mark5[AB] ioboard" << endl);

    // Before we restart, return to initial state
    boardtype          = unknown_boardtype;
    inputdesignrevptr  = 0;
    outputdesignrevptr = 0;
    ipboard            = 0;
    opboard            = 0;

    // Open /proc/bus/pci/devices and see if we can find a recognized device
    ASSERT2_NZERO( (fp=::fopen(devfile.c_str(), "r")),
                    SCINFO(" - devfile '" << devfile << "'") );

    // read each line from the file
    lineno = 0;
    while( boardtype==unknown_boardtype && ::fgets(linebuf, sizeof(linebuf), fp) ) {
        char*        sptr;
        char*        eptr;
        char*        entry;

        // break up the line in unsigned longs. Hmmm.. They are
        // printed as hex but w/o leading 0x. Add some magik.
        pciparms.resize( 0 );

        // use strtok to breakup the line
        sptr = linebuf;
        while( (entry=::strtok_r(sptr, seps, &eptr))!=0 ) {
            unsigned long   tmp;

            // Stop scanning at the first failure for reading an unsigned long in hex
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
            boardtype = mk5a;
        else if( pciparms[1]==mk5b_tag )
            boardtype = mk5b;
        lineno++;
    }
    ::fclose( fp );

    // If boardtype *still* unknown, we didn't find any!

    // Go on with initialization
    switch( boardtype ) {
        case ioboard_type::mk5a:
            do_init_mark5a( pciparms );
            break;
        case ioboard_type::mk5b:
            do_init_mark5b( pciparms );
            break;
        case ioboard_type::unknown_boardtype:
            ASSERT2_NZERO( 0, SCINFO(" - no recognized i/o boards found") );
            break;
        default:
            // unrecognized boardtype?!
            ASSERT2_NZERO( 0,
                    SCINFO(" - internal error: unknown boardtype " << (unsigned int)boardtype << " found; "
                           << "part of the system recognized it but at this point it certainly isn't anymore") );
            break;
    }
    DEBUG(1, "Detected a " << boardtype << " board" << endl);
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
    ASSERT_POS( (fd=::open(memory.c_str(), O_RDWR)) );

    // And mmap the boardregion into this application
    ASSERT2_COND( (mmapptr=(volatile void*)::mmap(0, mk5areg::mmapregionsize,
                      PROT_READ|PROT_WRITE, MAP_SHARED, fd, reg2_offset))!=MAP_FAILED,
                   ::close(fd) );
    DEBUG(2, " memorymapped " << mk5areg::mmapregionsize << " bytes of memory" << endl);
    // weeheee!
    ipboard = (volatile unsigned short*)mmapptr;
    // outputboard is at offset 32 wrt inputboard
    opboard = (volatile unsigned short*)(ipboard + 32);

    // The in/out design revisions are in [unsigned short] word #5 of both 
    // boardregions
    inputdesignrevptr  = (unsigned short*)(ipboard + 5);
    outputdesignrevptr = (unsigned short*)(opboard + 5);
  
    ::close( fd );

    DEBUG(1,"Found IDR: " << hex_t(*inputdesignrevptr)
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

}

void ioboard_type::do_init_mark5b( const ioboard_type::pciparms_type& ) {
    // N/A
    ASSERT2_NZERO(0, SCINFO("Mark5B ioboards are recognized but not not supported yet") );
    return;
}

void ioboard_type::do_cleanup_mark5b( void ) {
    // N/A
    ASSERT2_NZERO(0, SCINFO("Mark5B ioboards are recognized but not not supported yet") );
    return;
}
