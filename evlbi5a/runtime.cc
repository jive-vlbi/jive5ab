// implementation
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
#include <runtime.h>
#include <dosyscall.h>
#include <pthreadcall.h>
#include <evlbidebug.h>
#include <hex.h>
#include <bin.h>
#include <streamutil.h>

// c++
#include <set>

// C and system includes
#include <signal.h>
#include <math.h>

using namespace std;


// evlbi stats counters
evlbi_stats_type::evlbi_stats_type():
    pkt_total( 0ULL ), pkt_lost( 0ULL ), pkt_ooo( 0ULL ), pkt_rpt( 0ULL )
{}

ostream& operator<<(ostream& os, const evlbi_stats_type& es) {
    double    pct_lst( -1.0 );
    double    pct_ooo( -1.0 );

    if( es.pkt_total ) {
        pct_lst = ((double)es.pkt_lost/(double)es.pkt_total) * 100.0;
        pct_ooo  = ((double)es.pkt_ooo/(double)es.pkt_total) * 100.0;
    }
    os << format("%10llu", es.pkt_total) << " TOT, "
       << format("%10llu", es.pkt_lost) << " LST (" << format("%5.2lf%%", pct_lst) << "), "
       << format("%10llu", es.pkt_ooo) << " OoO (" << format("%5.2lf%%", pct_ooo) << ")";
    if( es.pkt_rpt )
        os << " " << es.pkt_rpt << " RPT!!";
    return os;
}

// shorthand for: (s==mark5adefault)?(d):(e)
// (ie: if setup==mark5adefault use 'd'(efault) otherwise
//  use the 'e'(mpty) value)
#define M5A(s,d,e) \
    (s==mark5adefault)?(d):(e)

// inputboard mode status
inputmode_type::inputmode_type( inputmode_type::setup_type setup ):
    mode( M5A(setup, "st", "") ),
    ntracks( M5A(setup, 32, -1) ),
    notclock( true ),
    errorbits( 0 )
{}
ostream& operator<<(ostream& os, const inputmode_type& ipm) {
    os << ipm.ntracks << "trk " << ipm.mode << " [R:" << !ipm.notclock
       << " E:" << bin_t(ipm.errorbits) << "]";
    return os;
}

//
// Outputboard mode status
//
outputmode_type::outputmode_type( outputmode_type::setup_type setup ):
    mode( M5A(setup, "st", "") ),
    freq( M5A(setup, 8.0, -1.0) ),
    active( false ), synced( false ),
    tracka( M5A(setup, 2, -1) ),
    trackb( M5A(setup, 2, -1) ),
    ntracks( M5A(setup, 32, 0) ),
    numresyncs( -1 ), throttle( false ),
    format( M5A(setup, "mark4", "") )
{}

ostream& operator<<(ostream& os, const outputmode_type& opm ) {
    os << opm.ntracks << "trk " << opm.mode << "(" << opm.format << ") "
       << "@" << format("%7.5lfMs/s/trk", opm.freq) << " "
       << "<A:" << opm.tracka << " B:" << opm.trackb << "> " 
       << " S: "
       << (opm.active?(""):("!")) << "Act "
       << (opm.synced?(""):("!")) << "Sync "
       << (opm.throttle?(""):("!")) << "Susp "
       ;
    return os;
}


// shorthand for: (s==mark5bdefault)?(d):(e)
// (ie: if setup==mark5bdefault use 'd'(efault) otherwise
//  use the 'e'(mpty) value)
#define M5B(s,d,e) \
    (s==mark5bdefault)?(d):(e)

//  Mark5B input
mk5b_inputmode_type::mk5b_inputmode_type( setup_type setup ):
    datasource( M5B(setup, "ext", "") ), // Default
    clockfreq( M5B(setup, 32.0, 0.0) ), // 32MHz
    tvg( M5B(setup, 0, -1) ), // TVG default mode == 0 [goes with 'datasource==ext']
    bitstreammask( M5B(setup, ~((unsigned int)0), 0) ), // all 32tracks
    k( M5B(setup, 4, -1) ), // f_default = 2^(k+1), k=4 => 32MHz default
    j( M5B(setup, 0, -1) ), // no decimation
    selpps( M5B(setup, 3, -1) ), // Sync to VSI1PPS by default
    selcgclk( false ), // no diff between empty/default [default: external clock]
    userword( M5B(setup, 0xbead, 0x0) ), // Verbatim from Mark5A.c
    fpdp2( false ), // do not request it by default
    startstop( false ), // don't cycle it by default
    hdr2( 0 ), hdr3( 0 ), // No difference between empty & default
    tvrmask( M5B(setup, ~((unsigned int)0), 0) ),
    gocom( false ), // GOCOM off by default
    seldim( false ), seldot( false ),
    erf( false ), tvgsel( false )
{}

ostream& operator<<(ostream& os, const mk5b_inputmode_type& ipm ) {
    os << "[" << ipm.datasource << " bsm:" << hex_t(ipm.bitstreammask)
       << " @2^" << ipm.k << "MHz/2^" << ipm.j
       << " PPS:" << ipm.selpps << " CGCLK:" << ipm.selcgclk 
       << "]";
    return os;
}

// The mark5b/dom inputmode 
mk5bdom_inputmode_type::mk5bdom_inputmode_type( setup_type setup ):
    mode( M5B(setup, "ext", "") ), // Default mark5b
    ntrack( M5B(setup, "0xffffffff", "") ) // Default = 32 track BitStreamMask
{}

ostream& operator<<(ostream& os, const mk5bdom_inputmode_type& ipm ) {
    os << "[" << ipm.mode << " ntrack:" << ipm.ntrack << "]";
    return os;
}


// how to show 'devices' on a stream
ostream& operator<<(ostream& os, devtype dt) {
    char   c( '!' );
    switch( dt ) {
        case dev_none:
            c = '*';
            break;
        case dev_network:
            c = 'N';
            break;
        case dev_disk:
            c = 'D';
            break;
        case dev_fifo:
            c = 'F';
            break;
        default:
            break;
    }
    return os << c;
}


//
//
//    The actual runtime 
//
//
runtime::runtime():
    transfermode( no_transfer ), transfersubmode( transfer_submode() ),
    signmagdistance( 0 ),
    tomem_dev( dev_none ), frommem_dev( dev_none ),
    nbyte_to_mem( 0ULL ), nbyte_from_mem( 0ULL ),
    current_taskid( invalid_taskid ),
    mk5a_inputmode( inputmode_type::empty ), mk5a_outputmode( outputmode_type::empty ),
    mk5b_inputmode( mk5b_inputmode_type::empty ),
    /*mk5b_outputmode( mk5b_outputmode_type::empty ),*/
    n_trk( 0 ), trk_bitrate( 0 ), trk_format(fmt_none)
{
    // already set up the mutex and the condition variable
    PTHREAD_CALL( ::pthread_mutex_init(&rte_mutex, 0) );

    // Set a default inputboardmode and outputboardmode,
    // depending on which hardware we find
    if( ioboard.hardware()&ioboard_type::mk5a_flag ) {
        this->set_input( inputmode_type(inputmode_type::mark5adefault) );
        this->set_output( outputmode_type(outputmode_type::mark5adefault) );
    } else if( ioboard.hardware()&ioboard_type::dim_flag ) {
        // DIM: set Mk5B default inputboard mode
        this->set_input( mk5b_inputmode_type(mk5b_inputmode_type::mark5bdefault) );
    } else if( ioboard.hardware()&ioboard_type::dom_flag ) {
        // DOM: set Mk5B default inputboard mode
        this->set_input( mk5bdom_inputmode_type(mk5bdom_inputmode_type::mark5bdefault) );
    } else if( !ioboard.hardware().empty() ){
        DEBUG(0, "Not setting default input/output boardmode because\n"
                 << "  hardware " << ioboard.hardware() << " not supported (yet)" << endl;);
    }
}

void runtime::lock( void ) {
    PTHREAD_CALL( ::pthread_mutex_lock(&rte_mutex) );
}
void runtime::unlock( void ) {
    PTHREAD_CALL( ::pthread_mutex_unlock(&rte_mutex) );
}

// Get current Mark5A Inputmode
void runtime::get_input( inputmode_type& ipm ) const {
    unsigned short               vlba;
    unsigned short               mode;
    codemap_type::const_iterator cme;

    // Do not access/alter the HW state if we're
    // in the "none" mode. Use mode=...
    // to reset it to anything not-none
    if( mk5a_inputmode.mode=="none" ) {
        ipm      = inputmode_type();
        ipm.mode = "none";
        return;
    } 
    // Accessing the registers already checks for conforming
    // hardware [attempting to access a Mk5A register on a Mk5B
    // will throw up :)]
    mode = *(ioboard[mk5areg::mode]);
    vlba = *(ioboard[mk5areg::vlba]);
    // decode it. Start off with default of 'unknown'
    mk5a_inputmode.mode = "?";
    if( mode>7 )
        mk5a_inputmode.mode = "tvg";
    else if( mode==4 )
        mk5a_inputmode.mode = "st";
    else if( mode<4 )
        mk5a_inputmode.mode = (vlba?("vlba"):("mark4"));
    // Uses same code -> ntrack mapping as outputboard
    // start off with 'unknown' (ie '0' (zero))
    mk5a_inputmode.ntracks = 0;
    cme  = code2ntrack(codemap, mode);
    // Note: as we may be doing TVG, we should not throw
    // upon not finding the mode!
    if( cme!=codemap.end() )
        mk5a_inputmode.ntracks = cme->numtracks;

    // get the notclock
    mk5a_inputmode.notclock = *(ioboard[mk5areg::notClock]);

    // and the errorbits
    mk5a_inputmode.errorbits = *(ioboard[mk5areg::errorbits]);

    // Copy over to user
    ipm = mk5a_inputmode;

    return;
}

// First modify a *copy* of the current input-mode,
// such that if something fails, we do not
// clobber the current config.
// Well, that's not totally true. At some point we
// write into the h/w and could still throw
// an exception ...
void runtime::set_input( const inputmode_type& ipm ) {
    bool                          is_vlba, is_mark4;
    format_type                   track( fmt_unknown );
    inputmode_type                curmode( mk5a_inputmode );
    ioboard_type::mk5aregpointer  mode    = ioboard[ mk5areg::mode ];
    ioboard_type::mk5aregpointer  vlba    = ioboard[ mk5areg::vlba ];

    // If we're setting the "none" mode, do not even try to
    // access/alter the HW
    if( ipm.mode=="none" ) {
        mk5a_inputmode.mode = "none";
        trk_format          = fmt_none;
        return;
    }
    // transfer parameters from argument to desired new mode
    // but only those that are set
    if( !ipm.mode.empty() )
        curmode.mode    = ipm.mode;
    if( ipm.ntracks>0 )
        curmode.ntracks = ipm.ntracks;

    // notClock is boolean and as such cannot be set to
    // 'undefined' (lest we introduce FileNotFound tri-state logic ;))
    // (Hint: http://worsethanfailure.com/Articles/What_is_Truth_0x3f_.aspx ... )
    curmode.notclock    = ipm.notclock;

    // The VLBA bit must be set if (surprise surprise) mode=vlba
    is_vlba  = (curmode.mode=="vlba");
    is_mark4 = (curmode.mode=="mark4");
    if( curmode.mode=="st" ) {
        mode = 4;
    } else if( curmode.mode=="tvg" || curmode.mode=="test" ) {
        mode = 8;
    } else if( curmode.mode=="vlbi" || is_vlba || is_mark4 ) {
        // transfer the boolean value 'is_vlba' to the hardware
        vlba = is_vlba;

        if( is_vlba )
            track = fmt_vlba;
        if( is_mark4 )
            track = fmt_mark4;

        // read back from h/w, now bung in ntrack code
        switch( curmode.ntracks ) {
            case 32:
                mode = 0;
                break;
            case 64:
                mode = 1;
                break;
            case 16:
                mode = 2;
                break;
            case 8:
                mode = 3;
                break;
            default:
                ASSERT2_NZERO(0, SCINFO("Unsupported nr-of-tracks " << ipm.ntracks));
                break;
        }
    } else if( curmode.mode.find("mark5a+")!=string::npos ) {
        // Mark5B playback on Mark5A+.
        // It's recognized (that's why I have this else() block here, to
        // keep it from throwing an exception) but we leave the inputsection 
        // of the I/O board unchanged as ... we cannot read Mk5B data :)
        track = fmt_mark5b;
    } else 
        ASSERT2_NZERO(0, SCINFO("Unsupported inputboard mode " << ipm.mode));

    mk5a_inputmode = curmode;

    // Good. Succesfully set Mark5A inputmode. Now update 'n_trk'
    n_trk          = (unsigned int)mk5a_inputmode.ntracks;
    // and the trackformat
    trk_format     = track;
    return;
}
// Get current mark5b inputmode
// makes no sense on a DOM
void runtime::get_input( mk5b_inputmode_type& ipm ) const {
    // Update internal copy of the inputmode
    // If the h/w is NOT a Mk5B/DIM, the register-access code
    // will throw up so we don't have to check
    mk5b_inputmode.k             = ioboard[ mk5breg::DIM_K ];
    mk5b_inputmode.j             = ioboard[ mk5breg::DIM_J ];
    mk5b_inputmode.selpps        = ioboard[ mk5breg::DIM_SELPP ];
    mk5b_inputmode.selcgclk      = ioboard[ mk5breg::DIM_SELCGCLK ];
    mk5b_inputmode.bitstreammask = (((unsigned int)ioboard[mk5breg::DIM_BSM_H])<<16)|
                                    (ioboard[mk5breg::DIM_BSM_L]);
    mk5b_inputmode.tvrmask       = (((unsigned int)ioboard[mk5breg::DIM_TVRMASK_H])<<16)|
                                    (ioboard[mk5breg::DIM_TVRMASK_L]);
    mk5b_inputmode.userword      = ioboard[ mk5breg::DIM_USERWORD ];
    mk5b_inputmode.fpdp2         = ioboard[ mk5breg::DIM_REQ_II ];
    mk5b_inputmode.startstop     = ioboard[ mk5breg::DIM_STARTSTOP ];
    mk5b_inputmode.gocom         = ioboard[ mk5breg::DIM_GOCOM ];
    mk5b_inputmode.seldim        = ioboard[ mk5breg::DIM_SELDIM ];
    mk5b_inputmode.seldot        = ioboard[ mk5breg::DIM_SELDOT ];
    mk5b_inputmode.erf           = ioboard[ mk5breg::DIM_ERF ];
    mk5b_inputmode.tvgsel        = ioboard[ mk5breg::DIM_TVGSEL ];

    // Now copy it into the user's variable and be done with it
    ipm = mk5b_inputmode;
    return;
}

// We must be able to verify that the number of bits set
// in the bitstreammask is a valid one
static const unsigned int  bsmvals[] = {1,2,4,8,16,32};
static set<unsigned int>   valid_nbit = set<unsigned int>(bsmvals, bsmvals+(sizeof(bsmvals)/sizeof(bsmvals[0])));

// Set a mark5b inputmode
void runtime::set_input( const mk5b_inputmode_type& ipm ) {
    // ord'nary variables
    int          j, k, pps, tvg;
    double       clkf;
    unsigned int bsm, nbit_bsm;

    // If we're setting the "none" mode, do not access/alter HW
    if( ipm.datasource=="none" ) {
        trk_format                  = fmt_none;
        mk5b_inputmode.datasource = "none";
        return;
    }

    // Initialize set of valid values
    if( valid_nbit.empty() )
        valid_nbit.insert( bsmvals, bsmvals+(sizeof(bsmvals)/sizeof(bsmvals[0]))+1 );

    // Before clobbering internal copy, verify all constraints we
    // know of in ipm and/or current setting.
    j    = (ipm.j>=0)?(ipm.j):(mk5b_inputmode.j);
    k    = (ipm.k>=0)?(ipm.k):(mk5b_inputmode.k);
    tvg  = (ipm.tvg>=0)?(ipm.tvg):(mk5b_inputmode.tvg);
    pps  = (ipm.selpps>=0)?(ipm.selpps):(mk5b_inputmode.selpps);
    bsm  = (ipm.bitstreammask>0)?(ipm.bitstreammask):(mk5b_inputmode.bitstreammask);
    clkf = (ipm.clockfreq>0.03)?(ipm.clockfreq):(mk5b_inputmode.clockfreq);

    // count number of bits set in the bitstreammask; it MUST be a power of two
    nbit_bsm = 0;
    for( unsigned int m=0x1, n=0; n<32; m<<=1, ++n )
        (void)((bsm&m)?(++nbit_bsm):(false));

    // Ok, do all tests we'd like to do
    ASSERT2_COND( (k>=0 && k<=5),
                  SCINFO(" Requested 'k' (freq) out of range: [0,5] is valid range") );
    ASSERT2_COND( (j>=0 && j<=k),
                  SCINFO(" Requested 'j' (decimation) out of range: [0,k] "
                                         << "(current k=" << k << ") is valid range") );
    ASSERT2_COND( (pps>=0 && pps<=3),
                  SCINFO(" Requested PulsePerSecondSync-Source is not valid") );
    ASSERT2_COND( (nbit_bsm>0 && valid_nbit.find(nbit_bsm)!=valid_nbit.end()),
                  SCINFO(" Invalid nbit_bsm (" << nbit_bsm << "), must be power of 2") );
    // If usr requests an internal clockfreq>=40.0MHz, we can't do that!
    ASSERT2_COND( (ipm.selcgclk?(clkf<40.0):(true)),
                  SCINFO(" Req. clockfreq " << clkf << " out of range, 40.0(MHz) is max") );

    // Check if fpdp2 requested: we only allow that if
    // the h/w says it's supported
    if( ipm.fpdp2 )
        ASSERT2_COND( (ioboard.hardware()&ioboard_type::fpdp_II_flag),
                      SCINFO( " FPDP2 Mode requested but h/w doesn't seem to support that") );

    // Verify tvg value
    // (3->8) are only valid in combination with FPDP2
    // The mode-command has already done that

    // Ok, all that can be verified has been.

    // k<=4 [ie f<=32MHz]? => Program clock chip for 32MHz
    // k==5 must rely on externally supplied 64MHz clock as
    // the on-board clockchip cannot go beyond 40MHz ....

    // Program clockchip if a different frequency requested than it's
    // currently at
    if( clkf>0.03 && ::fabs(mk5b_inputmode.clockfreq-clkf)>0.03 ) {
        ioboard.setMk5BClock( clkf );
        mk5b_inputmode.clockfreq = clkf;
    }
    // recompute the trackbitrate, accounting for VLBA format
    // and non-data-replacement headers
    trk_bitrate  = mk5b_inputmode.clockfreq * 1.125 * 1.008 * 1.0E6;
#if 0
    if( k<=4 )
        ioboard.setMk5BClock( 32.0 );
#endif
    mk5b_inputmode.j             = j;
    mk5b_inputmode.k             = k;
    mk5b_inputmode.tvg           = tvg;
    mk5b_inputmode.selpps        = pps;
    mk5b_inputmode.fpdp2         = ipm.fpdp2;
    mk5b_inputmode.bitstreammask = bsm;
    if( !ipm.datasource.empty() )
        mk5b_inputmode.datasource    = ipm.datasource;
    
    // these are taken over unconditionally
    // so if you want to leave them as-is,
    // first get the current inputmode and modify it
    // and then set the modified version.
    mk5b_inputmode.selcgclk  = ipm.selcgclk;
    mk5b_inputmode.userword  = ipm.userword;
    mk5b_inputmode.startstop = ipm.startstop;
    mk5b_inputmode.gocom     = ipm.gocom;
    mk5b_inputmode.tvrmask   = ipm.tvrmask;
    mk5b_inputmode.tvgsel    = ipm.tvgsel;

    // If the h/w is NOT a Mk5B/DIM, the register-access code
    // will throw up so we don't have to check
    ioboard[ mk5breg::DIM_K ]         = mk5b_inputmode.k;
    ioboard[ mk5breg::DIM_J ]         = mk5b_inputmode.j;
    ioboard[ mk5breg::DIM_SELPP ]     = mk5b_inputmode.selpps;
    ioboard[ mk5breg::DIM_SELCGCLK ]  = mk5b_inputmode.selcgclk;
    // Note: truncating/masking is automagically done by the regpointer stuff!
    ioboard[ mk5breg::DIM_BSM_L ]     = mk5b_inputmode.bitstreammask;
    ioboard[ mk5breg::DIM_BSM_H ]     = (mk5b_inputmode.bitstreammask >> 16);
    ioboard[ mk5breg::DIM_TVRMASK_L ] = mk5b_inputmode.tvrmask;
    ioboard[ mk5breg::DIM_TVRMASK_H ] = (mk5b_inputmode.tvrmask >> 16);

    ioboard[ mk5breg::DIM_USERWORD ]  = mk5b_inputmode.userword;
    ioboard[ mk5breg::DIM_REQ_II ]    = mk5b_inputmode.fpdp2;
    ioboard[ mk5breg::DIM_STARTSTOP ] = mk5b_inputmode.startstop;
    ioboard[ mk5breg::DIM_GOCOM ]     = mk5b_inputmode.gocom;
    ioboard[ mk5breg::DIM_SELDOT ]    = mk5b_inputmode.seldot;
    ioboard[ mk5breg::DIM_SELDIM ]    = mk5b_inputmode.seldim;
    ioboard[ mk5breg::DIM_TVGSEL ]    = mk5b_inputmode.tvgsel;

    // Good. Set the Mark5B/DIM inputmode. Update n_trk!
    // In this case, the number of tracks is the number of
    // bits set in bitstreammask, or nbit_bsm, for short!
    n_trk = nbit_bsm;

    // set the trackformat
    trk_format = fmt_mark5b;
    return;
}

// Get current mark5b inputmode on a DOM OR on
// a generic computer w/o I/O board.
// This is faker but hey, you can't have everyting!
// At least it forces you to give it sensible values so 
// you can detect anomalies.
void runtime::set_input( const mk5bdom_inputmode_type& ipm ) {
    // Make sure this only gets run onna Mark5B/DOM
    // OR on a machine with NO hardware at all
    ASSERT_COND( ioboard.hardware()&ioboard_type::dom_flag ||
                 ioboard.hardware().empty() );

    // Mark5B modes are 'ext' 'tvg[+<num>]', 'ramp'
    if( ipm.mode=="ext" || ipm.mode.find("tvg")==0 || ipm.mode=="ramp" )
        trk_format = fmt_mark5b;
    else if( ipm.mode=="none" )
        trk_format = fmt_none;
    else if( ipm.mode=="vlba" )
        trk_format = fmt_vlba;
    else if( ipm.mode=="mark4" )
        trk_format = fmt_mark4;
    else if( ipm.mode.empty()==false )
        ASSERT2_COND(false, SCINFO("Mode " << ipm.mode << " is not a valid mode(unrecognized)"));

    // If ntrack set, assert it is a sensible value.
    // Depend on the current track-format on how to 
    // parse/interpret the ntrack thingy.
    if( ipm.ntrack.empty()==false ) {
        ASSERT2_COND( trk_format!=fmt_none,
                      SCINFO("Cannot set ntrack=" << ipm.ntrack << " when no trackformat known") );

        if( trk_format==fmt_mark5b ) {
            // interpret it as a mark5b bitstreammask
            unsigned int bsm = ::strtoul( ipm.ntrack.c_str(), 0, 16 );
            unsigned int nbit_bsm;

            // Assert the same conditions as on a Mark5B/DIM
            nbit_bsm = 0;
            for( unsigned int m=0x1, n=0; n<32; m<<=1, ++n )
                (void)((bsm&m)?(++nbit_bsm):(false));
            ASSERT2_COND( (nbit_bsm>0 && valid_nbit.find(nbit_bsm)!=valid_nbit.end()),
                    SCINFO(" Invalid nbit_bsm (" << nbit_bsm << "), must be power of 2") );
            n_trk = nbit_bsm;
        } else if( trk_format!=fmt_none ) {
            // Mark4/VLBA - ntrack is just the number of tracks.
            // Must be power-of-two, >4 and <= 64
            unsigned int ntrack;
            ASSERT_COND( ::sscanf(ipm.ntrack.c_str(), "%u", &ntrack)==1 );
            ASSERT2_COND( ((ntrack>4) && (ntrack<=64) && (ntrack & (ntrack-1))==0),
                          SCINFO("ntrack (" << ntrack << ") is NOT a power of 2 which is >4 and <=64") );
            n_trk = ntrack;
        } else {
            ASSERT2_COND(false, SCINFO("Mark5B/DOM unhandled trackformat " << trk_format
                                       << " when attempting to set ntrack"));
        }
    }
    return;
}


void runtime::reset_ioboard( void ) const {
    // See what kinda hardware we have
    if( ioboard.hardware()&ioboard_type::mk5a_flag ) {
        // Ok, so it was a Mk5A board. We know how to reset those!

        // pulse the 'R'eset register
        DEBUG(1,"Resetting Mk5A IOBoard" << endl);
        ioboard_type::mk5aregpointer  w0 = ioboard[ mk5areg::ip_word0 ];

        // Note: the sequence between the #if 0 ... #endif
        // is the sequence how we would LIKE to do it [just toggling
        // bits] but somehow that doesn't work. Somehow the timing is
        // such that we have to write whole words rather than just modifying
        // single bits. It is most unfortunate!
        w0 = 0;
        w0 = 0x200;
        w0 = 0;
#if 0
        ioboard[ mk5areg::R ] = 0;
        ioboard[ mk5areg::R ] = 1;
        ioboard[ mk5areg::R ] = 0;
#endif
        DEBUG(1,"IOBoard reset" << endl);
    } else {
        DEBUG(-1,"Cannot reset IOBoard 'cuz this hardware (" << ioboard.hardware()
                 << ") is not recognized!!!" << endl);
    }
    return;
}

void runtime::get_output( outputmode_type& opm ) const {
    unsigned short               trka, trkb;
    unsigned short               code;
    codemap_type::const_iterator cme;

    if( mk5a_outputmode.mode=="none" ) {
        opm = outputmode_type();
        opm.mode = "none";
        return;
    }

    // Update current outputmode
    mk5a_outputmode.active     = *(ioboard[ mk5areg::Q ]);
    mk5a_outputmode.synced     = *(ioboard[ mk5areg::S ]);
    mk5a_outputmode.numresyncs = *(ioboard[ mk5areg::NumberOfReSyncs ]);
    mk5a_outputmode.throttle   = *(ioboard[ mk5areg::SF ]);
    // reset the SuspendFlag [cf IOBoard.c ...]
    ioboard[ mk5areg::SF ] = 0;

    trka                   = *(ioboard[ mk5areg::ChASelect ]);
    trkb                   = *(ioboard[ mk5areg::ChBSelect ]);
    /// and frob the tracknrs..
    mk5a_outputmode.tracka      = trka + (trka>31?70:2);
    mk5a_outputmode.trackb      = trkb + (trkb>31?70:2);

    // And decode 'code' into mode/format?
    code = *(ioboard[ mk5areg::CODE ]);

    DEBUG(2,"Read back code " << hex_t(code) << endl);
    cme  = code2ntrack(codemap, code);
    ASSERT2_COND( (cme!=codemap.end()),
                  SCINFO("Failed to find entry for CODE#" << code) );
    mk5a_outputmode.ntracks = cme->numtracks;
  
    // Now that we've updated our internal copy of the outputmode,
    // we can copy it over to the user
    opm = mk5a_outputmode;
    return;
}

void runtime::set_output( const outputmode_type& opm ) {
    bool                          is_vlba, is_mk5b;
    unsigned int                  mk5b_trackmap;
    unsigned short                code;
    outputmode_type               curmode( mk5a_outputmode );
    ioboard_type::mk5aregpointer  vlba    = ioboard[ mk5areg::V ];
    ioboard_type::mk5aregpointer  ap      = ioboard[ mk5areg::AP ];
    ioboard_type::mk5aregpointer  tmap[2] = { ioboard[ mk5areg::AP1 ],
                                              ioboard[ mk5areg::AP2 ] };

    // If we're setting the "none" mode, do not alter/access the HW
    if( opm.mode=="none" ) {
        mk5a_outputmode.mode = "none";
        return;
    }

    // 'curmode' holds the current outputmode
    // Now transfer values from the argument 'opm'
    // and overwrite the values in 'curmode' that are set
    // in 'opm' [it it possible to just change the 
    // playrate w/o changing the mode!]
    if( !opm.mode.empty() ) {
        curmode.mode    = opm.mode;
        // mark5a+ is (silently) changed into mark5a+0
        if( curmode.mode=="mark5a+" )
            curmode.mode="mark5a+0";
    }
    if( !opm.format.empty() )
        curmode.format  = opm.format;
    if( opm.freq>=0.0 )
        curmode.freq    = opm.freq;
    if( opm.ntracks>0 )
        curmode.ntracks = opm.ntracks;
    if( opm.tracka>0 )
        curmode.tracka  = opm.tracka;
    if( opm.trackb>0 )
        curmode.trackb  = opm.trackb;

    // go from mode(text) -> mode(encoded value)

    // Note: 
    //    std::string::find() returns a 'std::string::size_type' [index]
    //    with 'std::string::npos' as invalid ("not found") return code.
    //    We specifically check for a '0' (which is NOT std::string::npos!)
    //    return value: we want the substring 'mark5a+' to appear exactly
    //    at the beginning of the mode!
    // *if* we detect mk5b playback, try to get the mapping number from the mode
    if( (is_mk5b=(curmode.mode.find("mark5a+")==0))==true ) {
        // Assert we are succesfull in scanning exactly one number
        ASSERT2_COND( ::sscanf(curmode.mode.c_str(), "mark5a+%u", &mk5b_trackmap)==1,
                      SCINFO(" Invalid Mk5A+ mode, must include one number after mark5a+"));
        // And make sure it's a valid trackmap
        ASSERT_COND( mk5b_trackmap<3 );
        DEBUG(2, " Mk5B Playback on Mk5A+: Using built-in trackmap #"
                 << mk5b_trackmap << endl);
    }

    // The VLBA bit must be set if (surprise surprise) mode=vlba
    // or mode is one of the mark5a+n ( 0<=n<=2), [Mark5B datastream]
    is_vlba  = (curmode.mode=="vlba" || is_mk5b);

    // Always program a frequency. Do not support setting a negative
    // frequency. freq<0.001 (really, we want to test ==0.0 but
    // on floats/doubles that's not wise to do...
    if( curmode.freq<0.0 )
        curmode.freq = 8.0;

    if( ::fabs(curmode.freq)<0.001 ) {
        ioboard[ mk5areg::I ] = 0;
        DEBUG(2, "Setting external clock on output board" << endl);
    } else {
        // do program the frequency
        double                        freq = curmode.freq;
        // was unsigned long but on LP64, UL is 64bits iso 32!
        unsigned int                  dphase;
        unsigned char*                dp( (unsigned char*)&dphase );
        ioboard_type::mk5aregpointer  w0    = ioboard[ mk5areg::ip_word0 ];
        ioboard_type::mk5aregpointer  w2    = ioboard[ mk5areg::ip_word2 ];

        // From comment in IOBoard.c
        // " Yes, W0 = phase, cf. AD9850 writeup, p. 10 "
        // and a bit'o googling and finding/reading the h/w documentation
        // of this chip and looking at the registernaming in IOBoard.c and
        // the PDF, it would seem that the clockchip
        // on the board is an "Analog Devices #9850" chip.
        //
        // From the mentioned writeup [I've added the pdf into 
        // the CVS] of how to make the device read a new clockfreq..
        // i think it should be done differently...
        // [I've included the PDF into CVS so you can check for
        //  yourself]
        // take care of parity
        if( is_vlba || curmode.format=="mark4" )
            freq *= 1.125;
        // correct for VLBA non-data-replacement headers
        if( is_vlba )
            freq *= 1.008;
        // if 64 tracks, double the freq
        if( curmode.ntracks==64 )
            freq *= 2.0;
        // cache it for other parts of the s/w to use it
        trk_bitrate = freq * 1.0E6;

        //dphase = (unsigned long)(freq*42949672.96+0.5);
        // According to the AD9850 manual, p.8:
        // f_out = (dphase*input_clk)/ 2^32  (1)
        // judging from the code in IOBoard.c it follows that
        // the AD9850 on board the I/O board is fed with a 100MHz
        // clock - so we just reverse (1)
        dphase = (unsigned int)(((freq*4294967296.0)/100.0)+0.5);
        DEBUG(2,"dphase = " << hex_t(dphase) << " (" << dphase << ")" << endl);
        // According to the AD9850 doc:
        //   rising edge of FQ_UD resets the 'address' pointer to
        //   zero, after that, five rising edges of w_clk are used to
        //   transfer 5 * 8bits into the device. After 5 w_clk's
        //   any more w_clks are ingnored until the next 
        //   fq_ud rising edge or a reset...

        // 1) trigger a rising edge on fq_ud [force it to go -> 0 -> 1
        //    to make sure there *is* a rising edge!]
        // 2) clock the thingy into the registers with
        //    five w_clks
        for( unsigned int i=0; i<5; ++i ) {
            w0 = ((i==0)?0:dp[4-i]);
            w2 = 1;
            w2 = 0;
        }
        w0 = 0x100;
        w0 = 0;
        // done!
        ioboard[ mk5areg::I ] = 1;
        DEBUG(2, "Set internal clock on output board @" << freq
                 << "MHz [" << curmode.freq << "MHz entered]" << endl);
    }

    // Ok, clock has been programmed and (hopefully) has become stable...
    // (See ProgrammingOutputBoard.pdf)
    ioboard_type::mk5aregpointer Q = ioboard[ mk5areg::Q ];
    ioboard_type::mk5aregpointer C = ioboard[ mk5areg::C ];

    // start re-initialization sequence
    // as per the pdf we must do this everytime
    // the clock has been tampered with
    Q = 0;

    // enable fill detection
    ioboard[ mk5areg::F ]   = 1;

    // bit-sized registers can safely be assigned bool's (the code makes sure
    // that 'true' translates to '1' and 'false' to '0')
    vlba = is_vlba;
    ap   = is_mk5b;

    // Reset all 'A+' bits
    tmap[0] = 0;
    tmap[1] = 0;

    // If it's mk5b data we're playing back, we must also
    // set the ap1/ap2 bits.
    if( is_mk5b ) {
        // We must also assure that the 'ap' bit did set to '1'.
        // If it didn't, this is not a Mark5A+ [just a Mark5A], incapable
        // of playing back mark5b data!
        ASSERT2_COND( *ap==1,
                SCINFO("This is not a Mark5A+ and cannot play back Mark5B data."));
        // Good. Select the appropriate trackmap
        // As we already reset (ap1,ap2) to (0,0) we only have to 
        // deal with trackmap #1 and #2.
        // trackmap   | ap1 | ap2
        // ----------------------
        //     1         1     0
        //     2         0     1
        if( mk5b_trackmap>0 )
            tmap[ mk5b_trackmap-1 ] = 1;
    }

    // decide on the code
    code = 0;
    if( curmode.mode=="st" )
        code = 4;
    else if( curmode.mode=="tvg" ) {
        // doc sais: "Not implemented" ... let's find out!
        code = 8;
    } else {
        codemap_type::const_iterator cme = ntrack2code(codemap, curmode.ntracks);
        
        ASSERT2_COND( cme!=codemap.end(),
                      SCINFO("Unsupported number of tracks: " << curmode.ntracks) );
        code = cme->code;
        DEBUG(2,"Found codemapentry code/ntrk: " << cme->code << "/" << cme->numtracks);
    }
    // write the code to the H/W
    ioboard[ mk5areg::CODE ] = code;

    DEBUG(2,"write code " << hex_t(code) << " to output-board" << endl);

    // Pulse the 'C' register
    C = 1;
    C = 0;

    // and switch 'Q' back on
    Q = 1;

    // And store current mode
    mk5a_outputmode = curmode;

    // And update the number of tracks. In this case it is
    // the number of output tracks
    n_trk = (unsigned int)mk5a_outputmode.ntracks;
    return;
}

unsigned int runtime::ntrack( void ) const {
    return n_trk;
}

double runtime::trackbitrate( void ) const {
    return trk_bitrate;
}
format_type runtime::trackformat( void ) const {
    return trk_format;
}


runtime::~runtime() {
    DEBUG(3, "~runtime(): Cleaning up runtime" << endl);
    // if threadz running, kill'm!
    DEBUG(3, "Stopping processingchain .... ");
    this->processingchain.stop();
    DEBUG(3, "ok." << endl);
}

// scoped lock for the runtime
scopedrtelock::scopedrtelock(runtime& rte):
    rteref(rte)
{
  rteref.lock();
}

scopedrtelock::~scopedrtelock() {
    rteref.unlock();
}
