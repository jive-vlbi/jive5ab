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
#include <sciprint.h>
#include <timewrap.h>
#include <interchain.h>
#include <mk5_exception.h>
#include <dotzooi.h>
#include <headersearch.h>
#include <ezexcept.h>

// c++
#include <set>

// C and system includes
#include <signal.h>
#include <math.h>
#include <time.h>

using namespace std;


DECLARE_EZEXCEPT(rte_error)
DEFINE_EZEXCEPT(rte_error)


// evlbi stats counters
evlbi_stats_type::evlbi_stats_type():
    ooosum(0), pkt_in( 0 ), pkt_lost( 0 ), pkt_ooo( 0 ),
    pkt_disc( 0 ), gap_sum( 0 ),
    discont( 0 ), discont_sz( 0 )
{}

evlbi_stats_type& evlbi_stats_type::operator+=(evlbi_stats_type const& other) {
    if( this!=&other ) {
        ooosum   += other.ooosum;
        pkt_in   += other.pkt_in;
        pkt_lost += other.pkt_lost;
        pkt_ooo  += other.pkt_ooo;
        pkt_disc += other.pkt_disc;
    }
    return *this;
}


string fmt_evlbistats(const evlbi_stats_type& es) {
    return fmt_evlbistats(es, "total:%t:ooo:%o:disc:%d:lost:%l:extent:%R");
}

string fmt_evlbistats(const evlbi_stats_type& es, char const* const fmt) {
    static char const* const pct_fmt = "%5.2lf%%";
    double              pct_lst( 0 );
    double              pct_ooo( 0 );
    double              pct_disc( 0 );
    double              avg_extent( 0 );
    double              avg_gap( 0 );
    double              avg_discsz( 0 );
    char const*         cur;
    ostringstream       output;
    pcint::timeval_type now = pcint::timeval_type::now();

    // do some computing
    // ooosum = SUM( ABS( DELTA(expectseqnr - receivedseqnr) ) )
    if( es.pkt_in ) {
        double total    = (double)es.pkt_in + (double)es.pkt_lost;
        pct_lst  = ((double)es.pkt_lost/total) * 100.0;
        pct_ooo  = ((double)es.pkt_ooo/total) * 100.0;
        pct_disc = ((double)es.pkt_disc/total) * 100.0;
    }
    if( es.pkt_ooo ) {
        avg_extent = (double)es.ooosum/(double)es.pkt_ooo;
        avg_gap    = (double)es.gap_sum/(double)es.pkt_ooo;
    }
    if( es.discont )
        avg_discsz = (double)es.discont_sz/(double)es.discont;

    // check what the format looks like
    for( cur=fmt; *cur; cur++ ) {
        switch( *cur ) {
            case '%':
                // formatting is required
                // look at what we're expected to print
                cur++;
                switch( *cur ) {
                    case '\0':
                        break;
                    // total amount of pakkits
                    case 't':
                        output << es.pkt_in;
                        break;

                    // amount of sequencenumbers lost as count or percentage
                    case 'l':
                        output << es.pkt_lost;
                        break;
                    case 'L':
                        output << format(pct_fmt, pct_lst);
                        break;

                    // out-of-order pakkits, count or percentage of total
                    case 'o':
                        output << es.pkt_ooo;
                        break;
                    case 'O':
                        output << format(pct_fmt, pct_ooo);
                        break;
                    
                    // discarded packets, count or percentage of total
                    case 'd':
                        output << es.pkt_disc;
                        break;
                    case 'D':
                        output << format(pct_fmt, pct_disc);
                        break;

                    // reordering extent: total amount, average/packet
                    //   or avergage gap (in packets) between
                    //   reordering events
                    case 'r':
                        output << es.ooosum;
                        break;
                    case 'R':
                        output << avg_extent << "seqnr/pkt";
                        break;
                    case 'G':
                        output << avg_gap << "seqnr/gap";
                        break;

                    // discontinuities: number of those + avg. discontinuity
                    // size
                    case 'c':
                        output << es.discont;
                        break;
                    case 'C':
                        output << avg_discsz << "seqnr/discontinuity";
                        break;

                    // timestamp. raw unixtimestamp (+millisecond fraction
                    // or human readable timeformat
                    case 'u':
                        output << format("%.3lf",
                                         ((double)now.timeValue.tv_sec +
                                          (((double)now.timeValue.tv_usec)/1.0e6)));
                        break;
                    case 'U':
                        {
                            struct tm thetime;
                            // Create a timestring of the form:
                            // convert now to gmtime
                            if( ::gmtime_r(&now.timeValue.tv_sec, &thetime)!=0 ) {
                                char      tmstr[64];
                                size_t    l;

                                // use strftime to make this: YYYY-MM-DD HHhMMmSS
                                if( (l=::strftime(tmstr, sizeof(tmstr), "%Y-%m-%d %Hh%Mm%S", &thetime))!=0 ) {
                                    // good, that worked. now all we need to add is fractional seconds
                                    char  millisecstr[16];
                                    // form it as "d.ddds" and we append the decimal-point + following characters
                                    // to the actual timeformat
                                    if( ::snprintf(&millisecstr[0], sizeof(millisecstr), "%.3fs", ((double)now.timeValue.tv_usec/1.0e6))>0 ) {
                                        char* dp = ::strchr(millisecstr, '.');
                                        if( dp ) {
                                            if( ::snprintf(&tmstr[l], sizeof(tmstr)-l, "%s", dp)<(int)(sizeof(tmstr)-l) )
                                                output << tmstr;
                                            else
                                                output << "<tmstr buffer overflowed>";
                                        } else {
                                            output << "<no decimal point in millisecondfield?!>";
					}
                                    } else {
                                        output << "<snprintf(3) for millisecondfield fails>";
				   }
                                } else {
                                    output << "<strftime(3) failure>";
                                }
                            } else {
                                output << "<gmtime(3) failure>";
                            }
                        }
                        break;

                    default:
                        // eeeeeh say wut? unrecognized format
                        // so push the character on  unmodified
                        output << '%' << *cur;
                        break;
                }
                break;

            default:
                // him no are a formatting character.
                // push it through unmodified
                output << *cur;
                break;
        }
    }
    return output.str();
}


// shorthand for: (s==mark5adefault)?(d):(e)
// (ie: if setup==mark5adefault use 'd'(efault) otherwise
//  use the 'e'(mpty) value)
#define M5A(s,d,e) \
    (s==mark5adefault)?(d):(e)

// inputboard mode status
inputmode_type::inputmode_type( inputmode_type::setup_type setup ):
    mode( M5A(setup, "st", "") ),
    submode( M5A(setup, "mark4", "") ),
    notclock( true ),
    errorbits( 0 )
{}
ostream& operator<<(ostream& os, const inputmode_type& ipm) {
    os << ipm.submode << " " << ipm.mode << " [R:" << !ipm.notclock
       << " E:" << bin_t(ipm.errorbits) << "]";
    return os;
}

//
// Outputboard mode status
//
outputmode_type::outputmode_type( outputmode_type::setup_type setup ):
    mode( M5A(setup, "st", "") ),
    freq( M5A(setup, 8000000, 0) ),
    active( false ), synced( false ),
    tracka( M5A(setup, 2, -1) ),
    trackb( M5A(setup, 2, -1) ),
    submode( M5A(setup, "mark4", "") ),
    numresyncs( -1 ), throttle( false ),
    format( M5A(setup, "mark4", "") )
{}

ostream& operator<<(ostream& os, const outputmode_type& opm ) {
    os << opm.submode << " " << opm.mode << "(" << opm.format << ") "
       << "@" << format("%7.5lfs/s/trk", boost::rational_cast<double>(opm.freq/1000000)) << " "
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
    clockfreq( M5B(setup, 32000000, 0) ), // 32MHz
    tvg( M5B(setup, 0, -1) ), // TVG default mode == 0 [goes with 'datasource==ext']
    bitstreammask( M5B(setup, ~((unsigned int)0), 0) ), // all 32tracks
    k( M5B(setup, 4, -1) ), // f_default = 2^(k+1), k=4 => 32MHz default
    j( M5B(setup, 0, -1) ), // no decimation
    selpps( M5B(setup, 3, -1) ), // Sync to VSI1PPS by default
    selcgclk( false ), // no diff between empty/default [default: external clock]
    userword( M5B(setup, (unsigned short)0xbead, (unsigned short)0x0) ), // Verbatim from Mark5A.c
    fpdp2( false ), // do not request it by default
    startstop( false ), // don't cycle it by default
    hdr2( 0 ), hdr3( 0 ), // No difference between empty & default
    tvrmask( M5B(setup, (unsigned int)(~((unsigned int)0)), 0) ),
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
    mode( M5B(setup, "ext", "") ),          // Default mark5b
    ntrack( M5B(setup, "0xffffffff", "") ), // Default = 32 track BitStreamMask
    decimation( M5B(setup, 0, 0) ),         // Default = 1 (== no decimation)
    clockfreq( M5B(setup, 32000000, 0) )      // Default = 32 MHz
{}

ostream& operator<<(ostream& os, const mk5bdom_inputmode_type& ipm ) {
    os << "[" << ipm.mode << " ntrack:" << ipm.ntrack << "]";
    return os;
}


unsigned int constant( chain*, unsigned int n ) {
    return n;
}

std::string no_memstat( void ) {
    return std::string("no memstat available");
}

//
//
//    The actual runtime 
//
//
runtime::runtime():
    interchain_source_queue( NULL ),
    transfermode( no_transfer ), transfersubmode( transfer_submode() ),
    signmagdistance( 0 ),
    current_scan( 0 ),
    current_taskid( invalid_taskid ),
    protected_count( 0 ),
    disk_state_mask( erase_flag | play_flag | record_flag ),
    verbose_scancheck( true ),
    mk5a_inputmode( inputmode_type::empty ), mk5a_outputmode( outputmode_type::empty ),
    mk5b_inputmode( mk5b_inputmode_type::empty ),
    mk5bdom_inputmode( mk5bdom_inputmode_type::empty ),
    n_trk( 0 ), trk_bitrate( 0 ), trk_format(fmt_none),
    bufsizegetter( makethunk(&constant, (unsigned int)0) ),
    memstatgetter( makethunk(&no_memstat) )
{
    // already set up the mutex and the condition variable
    PTHREAD_CALL( ::pthread_mutex_init(&rte_mutex, 0) );
}

// the runtime with (valid) xlrdevice and ioboard
runtime::runtime(xlrdevice xlr, ioboard_type iob):
    interchain_source_queue( NULL ),
    transfermode( no_transfer ), transfersubmode( transfer_submode() ),
    xlrdev( xlr ),
    ioboard( iob ),
    signmagdistance( 0 ),
    current_scan( 0 ),
    current_taskid( invalid_taskid ),
    protected_count( 0 ),
    disk_state_mask( erase_flag | play_flag | record_flag ),
    mk5a_inputmode( inputmode_type::empty ), mk5a_outputmode( outputmode_type::empty ),
    mk5b_inputmode( mk5b_inputmode_type::empty ),
    mk5bdom_inputmode( mk5bdom_inputmode_type::empty ),
    /*mk5b_outputmode( mk5b_outputmode_type::empty ),*/
    n_trk( 0 ), trk_bitrate( 0 ), trk_format(fmt_none),
    bufsizegetter( makethunk(&constant, (unsigned int)0) ),
    memstatgetter( makethunk(&no_memstat) )
{
    // already set up the mutex and the condition variable
    PTHREAD_CALL( ::pthread_mutex_init(&rte_mutex, 0) );
}

// Delegate to the bufsizegetter
unsigned int runtime::get_buffersize( void ) {
    unsigned int rv;

    // call it
    bufsizegetter( &processingchain );
    // extract the return value
    bufsizegetter.returnval(rv);
    return rv;
}

// Allow the outside world to replace the bufsize-getter
curry_type runtime::set_bufsizegetter( curry_type tt ) {
    curry_type   old = bufsizegetter;

    ASSERT2_COND( tt.returnvaltype()==typeid(unsigned int).name(),
                  SCINFO("Your function call is flawed. It does not return 'unsigned int' but " << tt.returnvaltype()) );

    bufsizegetter = tt;
    return old;
}

string runtime::get_memory_status( void ) {
    string  rv;

    memstatgetter();
    memstatgetter.returnval(rv);
    return rv;
}

thunk_type runtime::set_memstat_getter(thunk_type tt) {
    thunk_type old = memstatgetter;

    ASSERT2_COND( tt.returnvaltype()==typeid(std::string).name(),
                  SCINFO("Your function call is flawed. It does not return 'std::string' but " << tt.returnvaltype()) );

    memstatgetter = tt;
    return old;
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
    // Uses same code -> submode mapping as outputboard
    // start off with 'unknown'
    mk5a_inputmode.submode = "";
    cme  = code2submode(codemap, mode);
    if ( mk5a_inputmode.mode == "st" ) {
        mk5a_inputmode.submode = (vlba ? "vlba" : "mark4" );
    }
    else {
        // Note: as we may be doing TVG, we should not throw
        // upon not finding the mode!
        if( cme!=codemap.end() )
            mk5a_inputmode.submode = cme->submode;
    }

    // get the notclock
    mk5a_inputmode.notclock = *(ioboard[mk5areg::notClock]);

    // and the errorbits
    mk5a_inputmode.errorbits = (char)(*(ioboard[mk5areg::errorbits]));

    // Copy over to user
    ipm = mk5a_inputmode;

    return;
}

// First modify a *copy* of the current input-mode,
// such that if something fails, we do not
// t
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

    if ( ipm.mode=="mark4" ) {
        EZASSERT2( *ioboard[mk5areg::errorbits] == 0, Error_Code_8_Exception, EZINFO("check formatter serial number even") );
    }
    // transfer parameters from argument to desired new mode
    // but only those that are set
    if( !ipm.mode.empty() )
        curmode.mode    = ipm.mode;
    if( !ipm.submode.empty() )
        curmode.submode = ipm.submode;

    // notClock is boolean and as such cannot be set to
    // 'undefined' (lest we introduce FileNotFound tri-state logic ;))
    // (Hint: http://worsethanfailure.com/Articles/What_is_Truth_0x3f_.aspx ... )
    curmode.notclock    = ipm.notclock;

    // The VLBA bit must be set if (surprise surprise) mode=vlba
    is_vlba  = (curmode.mode=="vlba");
    is_mark4 = (curmode.mode=="mark4");
    if( curmode.mode=="st" ) {
        if ( curmode.submode=="vlba" ) {
            track = fmt_vlba_st;
            vlba = true;
        }
        else if ( curmode.submode=="mark4" ) {
            track = fmt_mark4_st;
            vlba = false;
        }
        else {
            THROW_EZEXCEPT(Error_Code_8_Exception, "submode not mark4 or vlba");
        }
        n_trk = 32;
        mode = 4;
    } else if( curmode.mode=="tvg" || curmode.mode=="test" ) {
        // tvg on the Mark5A produces VLBA formatted data
        mode = 8;
        n_trk = 32;
        track = fmt_vlba;
    } else if( curmode.mode=="vlbi" || is_vlba || is_mark4 ) {
        // transfer the boolean value 'is_vlba' to the hardware
        vlba = is_vlba;

        if( is_vlba )
            track = fmt_vlba;
        if( is_mark4 )
            track = fmt_mark4;

        // construct a map from submode string to <mode, n_trk> pair
        typedef std::pair< unsigned int, unsigned int > mt_type;
        typedef std::pair< std::string, mt_type >       submode_map_type;
        static const submode_map_type submodes[] = 
            {submode_map_type("32", mt_type(0, 32)),
             submode_map_type("64", mt_type(1, 64)),
             submode_map_type("16", mt_type(2, 16)),
             submode_map_type("8",  mt_type(3, 8))};
        static const std::map< std::string, mt_type > submodes_map( &submodes[0], &submodes[0] + sizeof(submodes)/sizeof(submodes[0]) );
        // read back from h/w, now bung in ntrack code
        std::map< std::string, mt_type >::const_iterator iter = submodes_map.find(curmode.submode);
        if ( iter != submodes_map.end() ) {
            mode = iter->second.first;
            n_trk = iter->second.second;
        }
        else {
            THROW_EZEXCEPT(Error_Code_8_Exception, "Unsupported nr-of-tracks " << ipm.submode);
        }
    } else if( curmode.mode.find("mark5a+")!=string::npos ) {
        // Mark5B playback on Mark5A+.
        // It's recognized (that's why I have this else() block here, to
        // keep it from throwing an exception) but we leave the inputsection 
        // of the I/O board unchanged as ... we cannot read Mk5B data :)
        track = fmt_mark5b;
    } else 
        THROW_EZEXCEPT(Error_Code_8_Exception, "Unsupported inputboard mode " << ipm.mode);

    // Overwrite internal copy of the mode
    // and clear the magic mode!
    mk5a_inputmode    = curmode;
    mk5bdom_inputmode = mk5bdom_inputmode_type( mk5bdom_inputmode_type::empty );

    // Good. Succesfully set Mark5A inputmode. Now update the submode and trackformat
    trk_format    = track;
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
// HV: April 17 2012 - Added 64 bits as acceptable number of bits
//                     This is for the dBBC and only works
//                     on a generic PC - Mark5A(+)/B(+) do not
//                     support this
static const unsigned int      bsmvals[] = {1,2,4,8,16,32,64};
static const set<unsigned int> valid_nbit = set<unsigned int>(bsmvals, bsmvals+((sizeof(bsmvals)/sizeof(bsmvals[0]))));

// Set a mark5b inputmode
void runtime::set_input( const mk5b_inputmode_type& ipm ) {
    // ord'nary variables
    int             j, k, pps, tvg;
    unsigned int    bsm, nbit_bsm;
    samplerate_type clkf;

    // It could also be that we enter here on account of clock_set
    // with VDIF already set as trackformat!
    // Must take care to not break those settings!

    // Before clobbering internal copy, verify all constraints we
    // know of in ipm and/or current setting.
    j    = (ipm.j>=0)?(ipm.j):(mk5b_inputmode.j);
    k    = (ipm.k>=0)?(ipm.k):(mk5b_inputmode.k);
    tvg  = (ipm.tvg>=0)?(ipm.tvg):(mk5b_inputmode.tvg);
    pps  = (ipm.selpps>=0)?(ipm.selpps):(mk5b_inputmode.selpps);
    bsm  = (ipm.bitstreammask>0)?(ipm.bitstreammask):(mk5b_inputmode.bitstreammask);
    clkf = (ipm.clockfreq>0)?(ipm.clockfreq):(mk5b_inputmode.clockfreq);

    // count number of bits set in the bitstreammask; it MUST be a power of two
    nbit_bsm = 0;
    for( unsigned int m=0x1, n=0; n<32; m<<=1, ++n )
        (void)((bsm&m)?(++nbit_bsm):(false));

    // Ok, do all tests we'd like to do
    EZASSERT2( (k>=0 && k<=5), rte_error,
               EZINFO(" Requested 'k' (freq) out of range: [0,5] is valid range") );
    EZASSERT2( (j>=0 && j<=k), rte_error,
               EZINFO(" Requested 'j' (decimation) out of range: [0,k] "
                                         << "(current k=" << k << ") is valid range") );
    EZASSERT2( (pps>=0 && pps<=3), rte_error,
               EZINFO(" Requested PulsePerSecondSync-Source is not valid") );
    EZASSERT2( (nbit_bsm>0 && valid_nbit.find(nbit_bsm)!=valid_nbit.end()), rte_error,
               EZINFO(" Invalid nbit_bsm (" << nbit_bsm << "), must be power of 2") );
    // If usr requests an internal clockfreq>=40.0MHz, we can't do that!
    EZASSERT2( (ipm.selcgclk?(clkf<40000000):(true)), rte_error,
               EZINFO(" Req. clockfreq " << clkf << " out of range, 40.0(MHz) is max") );

    // Check if fpdp2 requested: we only allow that if
    // the h/w says it's supported
    if( ipm.fpdp2 )
        EZASSERT2( (ioboard.hardware()&ioboard_type::fpdp_II_flag), rte_error,
                   EZINFO( " FPDP2 Mode requested but h/w doesn't seem to support that") );

    // Verify tvg value
    // (3->8) are only valid in combination with FPDP2
    // The mode-command has already done that

    // Ok, all that can be verified has been.

    // k<=4 [ie f<=32MHz]? => Program clock chip for 32MHz
    // k==5 must rely on externally supplied 64MHz clock as
    // the on-board clockchip cannot go beyond 40MHz ....

    // Program clockchip if a different frequency requested than it's
    // currently at AND if the internal clock has been requested
    if( ipm.selcgclk && clkf>0 && (mk5b_inputmode.clockfreq!=clkf) ) {
        ioboard.setMk5BClock( boost::rational_cast<double>(clkf/1000000) );
        mk5b_inputmode.clockfreq = clkf;
    }

    // HV: 24-sep-2013 Let's first figure out what the data format
    //                 is that we're looking at.
    //
    //                 If ipm.datasource.empty() we're coming here
    //                 on account of a "clock_set" call and the track
    //                 format should not be altered.
    //
    //                 If non-empty it must be a mark5b "mode=" command
    //
    //                 "mode=ext/int:..." will have "ipm.datasource=ext/int"
    //                 and "mode=vdif:..." will not end up at this point
    //                 of the code - the only code paths that lead to
    //                 here are:
    //                     * mode=ext|int|tvg: ....
    //                     * clock_set= ....
    //                 and telling *them* apart is done using the
    //                 "datasource" emptyness property
    if( !ipm.datasource.empty() ) {
        // Good. Set the Mark5B/DIM inputmode. Update n_trk!
        // In this case, the number of tracks is the number of
        // bits set in bitstreammask, or nbit_bsm, for short!
        n_trk = nbit_bsm;

        // set the trackformat
        trk_format = (ipm.tvg>1?fmt_unknown:fmt_mark5b);
    }

    // recompute the trackbitrate, accounting for decimation
    // HV: 24-sep-2013 This should only happen when the Mark5B format
    //                 is set.
    //     06-jan-2014 The track bit rate should follow the "K" parameter
    //                 and not the programmed clock frequency!
    if( trk_format==fmt_mark5b )
        trk_bitrate  = ((1 << (k+1)) * 1000000) / (1 << j);
    else
        // setting clock_freq on non-Mark5B data only sets track bit rate
        // directly since there's no decimation
        trk_bitrate  =  ((1 << (k+1)) * 1000000);

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

    // And clear the 'magic mode' (mk5bdom_inputmode_type)
    mk5bdom_inputmode = mk5bdom_inputmode_type( mk5bdom_inputmode_type::empty );

    // After having, potentially, reset the clock frequency
    // we must program the length of the DOT PPS second for
    // monitoring
    // Depending on internal or external clock we must set
    // the PPS length. With "ext" clock it's always "1.0",
    //    HV: UPDATE 18-Nov-2013  Welllll ... with the external
    //        clock you just can't tell! We don't know (nor can we
    //        program/tell) what the clock rate on the VSI bus is
    //        (for that is what "ext" refers to). Therefore, we 
    //        cannot _predict_ what the 1PPS duration is - it will 
    //        depend on the ratio between actual VSI clock frequency and
    //        the "K" value programmed. Therefore we ASSUME it should be
    //        "1.0" seconds (the 1PPS length). The code will start to
    //        complain loudly and bitterly if the external clock + 
    //        programmed data rate (DOT1PPS freq, 2**(K+1)) are not equal!
    //        I'd say "WIN!".
    // With "int" clock it's the ratio of clock-generator freq
    // and DOT1PPS freq (==2**(K+1))
    if( mk5b_inputmode.selcgclk )
        set_pps_length( (1.0e6 * ::exp((mk5b_inputmode.k + 1) * M_LN2)) / boost::rational_cast<double>(mk5b_inputmode.clockfreq));
    else
        set_pps_length( 1.0 );

    // iff fpdp2 requested, do make sure it got set!
    if( mk5b_inputmode.fpdp2 ) {
        EZASSERT2( *ioboard[mk5breg::DIM_II], Error_Code_8_Exception,
                   EZINFO("fpdp mode II requested but h/w can't do it") );
    }

    return;
}

// Get current mark5b inputmode on a DOM OR on
// a generic computer w/o I/O board.
// This is fake but hey, you can't have everyting!
// At least it forces you to give it sensible values so 
// you can detect anomalies.
//
// HV/BE: Mar 2014 - We want to support setting the 'mode'
//                   using Walter Brisken's normalized format
//                   (see headersearch.{h,cc} - e.g. "VDIF_8224-1024-16-2")
//                   and we want to support it on all systems.
//                   This allows any format to be read from disk and 
//                   be processed.
//                   We decided to (re-)use the mk5bdom_inputmode
//                   to store the mode information in, in this
//                   case, to set it apart from the hardware
//                   mode.
//                   It should be well understood that IF you set the 
//                   mode using that format, (1) the hardware doesn't
//                   get reprogrammed and (2) a "clock_set=", "play_rate="
//                   or a "mode=" with a valid 'hardware mode' (ie as per
//                   Mark5* Command Set documentation) will make the
//                   system forget the mode set via the Walter B format.
void runtime::set_input( const mk5bdom_inputmode_type& ipm ) {
    // HV: 26-May-2014 It could be that we enter this function
    //                 because of either a "mode=" or "play_rate=" or
    //                 "clock_set="
    //                 We look at which parameters are set in the mode
    //                 to decide which one we suspect.
    //
    //                 For setting Mark5A/Mark5B formats both "mode" and
    //                 "ntrack" fields are non-empty.
    //
    //                 For setting Mark5C mode, we support "unk"(known). So
    //                 this looks like a magic mode command.
    //                 The other format supported on Mark5B is
    //                 "mark5b:0xfffff" (so it looks like an ordinary mode
    //                 cmd)
    //
    const bool is5c      = (ioboard.hardware()&ioboard_type::mk5c_flag);
    const bool freqcmd   = (ipm.clockfreq!=0);
    const bool magicmode = (ipm.mode.empty()==false && ipm.ntrack.empty()==true && !freqcmd);
    const bool abcmode   = (ipm.mode.empty()==false && ipm.ntrack.empty()==false && !freqcmd);
    const bool modecmd   = (magicmode || abcmode);
    const bool nonemode  = (magicmode && (ipm.mode=="none" || (is5c && ipm.mode=="unk")));

    // Test validity of input - we must know for sure that what we're passed
    // makes sense; that it's not that someone is trying to set all
    // parameters at once
    EZASSERT2(freqcmd!=modecmd, rte_error, EZINFO("Not a mode XOR freq cmd"));

    // Set no format
    if( nonemode ) {
        mk5bdom_inputmode.mode = "none";
        trk_format             = fmt_none;
        return;
    }

    // March 2014: Walter's 'magic mode' string is a one-string-sets-all format.
    if( magicmode ) {
        headersearch_type*  fmtptr;

        // The command given was "mode=<string>" (with only one argument
        // so it better had be a VALID string!
        EZASSERT2( fmtptr=::text2headersearch(ipm.mode), rte_error, 
                   EZINFO("Mode '" << ipm.mode << "' is not a valid mode"));
        // Ok, we got a headersearch_type, so the mode was valid.
        // Transfer all necessary values to internal copies
        trk_format               = fmtptr->frameformat;
        trk_bitrate              = fmtptr->trackbitrate;
        n_trk                    = fmtptr->ntrack;
        vdif_framesize           = fmtptr->payloadsize;
        mk5bdom_inputmode.mode   = ipm.mode; 
        mk5bdom_inputmode.ntrack = "";
        return;
    }
    
    // Ok, it wasn't a 'magic mode' command - attempt normal parsing.
    // Could still be "mode=" "play_rate=" or "clock_set="
    //
    // Mark5A modes are 'mark4' | 'vlba' : <ntrack
    // Mark5B modes are 'ext' : <bitstream mask>
    // Mark5C modes for Mark5B format is 'mark5b' : <bitstream mask>
    if( abcmode ) {
        if( ipm.mode=="ext" || ipm.mode.find("tvg")==0 || (is5c && ipm.mode=="mark5b"))
            trk_format = fmt_mark5b;
        //else if( ipm.mode=="ramp" )
        //    trk_format = fmt_unknown;
        //else if( ipm.mode=="none" )
        //    trk_format = fmt_none;
        else if( ipm.mode=="vlba" )
            trk_format = fmt_vlba;
        else if( ipm.mode=="mark4" )
            trk_format = fmt_mark4;
        else
            EZASSERT2(false, rte_error, EZINFO("Mode " << ipm.mode << " is not a valid mode(unrecognized)"));

        // In 'abcmode' commands, the 'ntrack' is always set. Make sure it's
        // sensible.
        // Depend on the current track-format on how to 
        // parse/interpret the ntrack thingy.

        // HV: April 17 2012 - 
        //       The digital BBC (dBBC) outputs Mark5B frames
        //       with up to 64 tracks at 64 Mbps per track
        //       [yes, this is over the Mk5B spec of 2Gbps], it
        //       is 4Gbps. We must be able to deal with that
        //       data *sigh*, so we silently change the bsm into
        //       a 64bit quantity and allow 64 '1's in it.
        if( trk_format==fmt_mark5b ) {
            // interpret it as a mark5b bitstreammask [widened to 64 bits]
            unsigned int   nbit_bsm;
            const uint64_t bsm = (uint64_t)::strtoull( ipm.ntrack.c_str(), 0, 16 );

            // Assert the same conditions as on a Mark5B/DIM
            //  * April 17 2012 - allow 64 bits
            nbit_bsm = 0;
            for( uint64_t m=0x1, n=0; n<64; m<<=1, ++n )
                (void)((bsm&m)?(++nbit_bsm):(false));
            EZASSERT2( (nbit_bsm>0 && valid_nbit.find(nbit_bsm)!=valid_nbit.end()), rte_error,
                       EZINFO(" Invalid nbit_bsm (" << nbit_bsm << "), must be power of 2") );
            n_trk = nbit_bsm;
        } else if( trk_format!=fmt_none ) {
            // Mark4/VLBA - ntrack is just the number of tracks.
            // Must be power-of-two, >4 and <= 64
            unsigned int num_track;
            EZASSERT( ::sscanf(ipm.ntrack.c_str(), "%u", &num_track)==1, rte_error );
            EZASSERT2( ((num_track>4) && (num_track<=64) && (num_track & (num_track-1))==0), rte_error,
                       EZINFO("ntrack (" << num_track << ") is NOT a power of 2 which is >4 and <=64") );
            n_trk = num_track;
        } else {
            EZASSERT2(false, rte_error, EZINFO("Mark5B/DOM + Mark5C - unhandled trackformat " << trk_format
                                               << " when attempting to set ntrack"));
        }
        
        // Check decimation
        if( ipm.decimation!=0 ) {
            // decimation of 1 => no decimation
            const int decimation = ipm.decimation;

            // Only supported for Mark5
            EZASSERT2(trk_format==fmt_mark5b, rte_error, EZINFO("Setting decimation on non-mark5b format is not allowed"));

            // Only powers of 2 are allowed
            EZASSERT2(decimation>0 && (unsigned int)decimation<UINT_MAX && (decimation & (decimation-1))==0, rte_error,
                      EZINFO("decimation '" << ipm.decimation << "' invalid, not a power of 2 < UINT_MAX") );
            // Because of precision we do not allow decimation to be > clock_freq.
            // We can only apply the decimation if we have a clock frequency.
            if( mk5bdom_inputmode.clockfreq ) {
                // clockfreq * 2^-decimation
                samplerate_type  clockfreq_after_decimation = mk5bdom_inputmode.clockfreq / decimation;
                EZASSERT2(clockfreq_after_decimation>=1000000, rte_error, EZINFO("Sorry, we do not allow decimation to below 1 MHz"));
                trk_bitrate = clockfreq_after_decimation;
            } 
            // But *do* record the actual decimation; if someone sets a
            // clock freq later we will take it into account then as well
            mk5bdom_inputmode.decimation = ipm.decimation;
        }

        // Copy over ntrack/mode
        mk5bdom_inputmode.mode   = ipm.mode;
        mk5bdom_inputmode.ntrack = ipm.ntrack;
    }

    // If it's play_rate/clock_set?
    if( freqcmd ) {
        const samplerate_type  f_in( ipm.clockfreq / 1000000 );
        
        EZASSERT2(f_in>=2 && f_in<=64, rte_error,
                  EZINFO(" Requested frequency " << f_in << " <2 or >64 is not allowed") );

        // Assert if power of 2
        EZASSERT2(f_in.denominator()==1 && (f_in.numerator() & (f_in.numerator()-1))==0, rte_error,
                  EZINFO(" Requested frequency " << f_in << " is not a power of 2") );

        mk5bdom_inputmode.clockfreq = ipm.clockfreq;

        // take decimation into account if we're on Mark5B format *and*
        // the decimation is >0. Note that the decimation that is stored
        // is the *actual* decimation! 
        if( trk_format==fmt_mark5b && mk5bdom_inputmode.decimation>0 ) {
            samplerate_type  clockfreq_after_decimation = mk5bdom_inputmode.clockfreq / mk5bdom_inputmode.decimation;
            EZASSERT2(clockfreq_after_decimation>=1000000, rte_error, EZINFO("Sorry, we do not allow decimation to below 1 MHz"));
            trk_bitrate = clockfreq_after_decimation;
        } else
            trk_bitrate = mk5bdom_inputmode.clockfreq;
    }
    return;
}


void runtime::get_input( mk5bdom_inputmode_type& ipm ) const {
    ipm = mk5bdom_inputmode;
}

void runtime::reset_ioboard( void ) const {
    // See what kinda hardware we have
    if( ioboard.hardware()&ioboard_type::io5a_flag ) {
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

    if ( code == 4 ) {
        mk5a_outputmode.mode = "st";
        mk5a_outputmode.submode = ( ioboard[ mk5areg::V ] ? "vlba" : "mark4" );
    }
    else {
        cme  = code2submode(codemap, code);
        ASSERT2_COND( (cme!=codemap.end()),
                      SCINFO("Failed to find entry for CODE#" << code) );
        mk5a_outputmode.submode = cme->submode;
    }
  
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
    if( opm.freq!=0 )
        curmode.freq    = opm.freq;
    if( !opm.submode.empty() )
        curmode.submode = opm.submode;
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
        EZASSERT2( ::sscanf(curmode.mode.c_str(), "mark5a+%u", &mk5b_trackmap)==1, rte_error,
                   EZINFO(" Invalid Mk5A+ mode, must include one number after mark5a+"));
        // And make sure it's a valid trackmap
        EZASSERT( mk5b_trackmap<3, rte_error );
        DEBUG(2, " Mk5B Playback on Mk5A+: Using built-in trackmap #"
                 << mk5b_trackmap << endl);
    }

    // The VLBA bit must be set if (surprise surprise) mode=vlba
    // or mode is one of the mark5a+n ( 0<=n<=2), [Mark5B datastream]
    is_vlba  = (curmode.mode=="vlba" || is_mk5b || ((curmode.mode=="st") && (curmode.submode=="vlba")));

    // Always program a frequency. Do not support setting a negative
    // frequency. freq<0.001 (really, we want to test ==0.0 but
    // on floats/doubles that's not wise to do...
    // Update: 02 Nov 2015 - with 'rational' samplerates, tests for ==0 actually CAN be done!
    if( curmode.freq==0 )
        curmode.freq = 8000000;

    if( curmode.freq<0 ) {
        ioboard[ mk5areg::I ] = 0;
        DEBUG(2, "Setting external clock on output board" << endl);
    } else {
        // do program the frequency
        samplerate_type               freq = curmode.freq;
        // was unsigned long but on LP64, UL is 64bits iso 32!
        unsigned int                  dphase;
        unsigned char*                dp( (unsigned char*)&dphase );
        ioboard_type::mk5aregpointer  w0    = ioboard[ mk5areg::ip_word0 ];
        ioboard_type::mk5aregpointer  w2    = ioboard[ mk5areg::ip_word2 ];

        // cache it for other parts of the s/w to use it
        trk_bitrate = freq /* already in MHz */; /* 11Jan2017 No! freq is now in Hz!!! */

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
            freq *= samplerate_type(9, 8) /*1.125*/;
        // correct for VLBA non-data-replacement headers
        if( is_vlba )
            freq *= samplerate_type(126, 125) /*1.008*/;
        // if 64 tracks, double the freq
        if( curmode.submode=="64" )
            freq *= 2;

        //dphase = (unsigned long)(freq*42949672.96+0.5);
        // According to the AD9850 manual, p.8:
        // f_out = (dphase*input_clk)/ 2^32  (1)
        // judging from the code in IOBoard.c it follows that
        // the AD9850 on board the I/O board is fed with a 100MHz
        // clock - so we just reverse (1)
        // 11Jan2017 - freq is now in Hz, not in MHz anymore!
        dphase = (unsigned int)(((boost::rational_cast<double>(freq/1000000)*4294967296.0)/100.0)+0.5);
        DEBUG(5,"dphase = " << hex_t(dphase) << " (" << dphase << ")" << endl);
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
        DEBUG(2, "Set internal clock on output board @" << freq/1000000
                 << "MHz [" << curmode.freq/1000000 << "MHz entered]" << endl);
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
        EZASSERT2( *ap==1, rte_error,
                   EZINFO("This is not a Mark5A+ and cannot play back Mark5B data."));
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

        codemap_type::const_iterator cme = submode2code(codemap, curmode.submode);
        
        EZASSERT2( cme!=codemap.end(), rte_error,
                   EZINFO("Unsupported submode: " << curmode.submode) );
        code = cme->code;
        DEBUG(2,"Found codemapentry code/submode: " << cme->code << "/" << cme->submode << endl);
    }
    // write the code to the H/W
    ioboard[ mk5areg::CODE ] = code;

    DEBUG(5,"write code " << hex_t(code) << " to output-board" << endl);

    // Pulse the 'C' register
    C = 1;
    C = 0;

    // and switch 'Q' back on
    Q = 1;

    // map the submode to a number of tracks
    typedef std::pair< std::string, unsigned int> submode_map_type;
    static const submode_map_type submodes[] = 
            {submode_map_type("32", 32),
             submode_map_type("64", 64),
             submode_map_type("16", 16),
             submode_map_type("8",  8),
             submode_map_type("mark4",  32), // must be st mode
             submode_map_type("vlba",  32), // must be st mode
            };
    static const std::map< std::string, unsigned int > submodes_map( &submodes[0], &submodes[0] + sizeof(submodes)/sizeof(submodes[0]) );
    std::map< std::string, unsigned int >::const_iterator iter = submodes_map.find( curmode.submode );
    ASSERT2_COND( iter != submodes_map.end(), SCINFO("Unsupported submode " << curmode.submode) );

    // And store current mode
    mk5a_outputmode = curmode;

    // And update the number of tracks. In this case it is
    // the number of output tracks
    n_trk = iter->second;

    // Right, the output mode has been set or the Mk5A play rate has been
    // changed so it's time to clear the 'magic mode'
    mk5bdom_inputmode = mk5bdom_inputmode_type( mk5bdom_inputmode_type::empty );

    return;
}

unsigned int runtime::mark5aplus_fanout( void ) const {
    if ( (ioboard.hardware()&ioboard_type::io5a_flag) && (trk_format == fmt_mark5b) ) {
        unsigned int mode = ioboard[ mk5areg::AP1 ] + 2 * ioboard[ mk5areg::AP2 ];
        if ( mode == 0 ) {
            return 1;
        }
        if ( mode == 1 ) {
            return 4;
        }
        if ( mode == 2 ) {
            return 2;
        }
        throw xlrexception("mark5a+ mode not in [0, 2]");
    }
    return 1;
}

unsigned int runtime::ntrack( void ) const {
    return n_trk / mark5aplus_fanout();
}

samplerate_type runtime::trackbitrate( void ) const {
    return trk_bitrate * mark5aplus_fanout();
}
format_type runtime::trackformat( void ) const {
    return trk_format;
}
unsigned int runtime::vdifframesize( void ) const {
    return vdif_framesize;
}

void runtime::set_trackbitrate(const samplerate_type& bitrate) {
   ASSERT2_COND( ((ioboard.hardware()&ioboard_type::io5a_flag)==false) &&
                  ((ioboard.hardware()&ioboard_type::io5b_flag)==false),
                SCINFO("You can only call this function on a generic PC or a Mark5C") );
    trk_bitrate = bitrate;
}

void runtime::setCurrentScan( unsigned int index ) {
    ROScanPointer scan = xlrdev.getScan( index );
    pp_current = scan.start();
    pp_end = scan.start() + scan.length();
    current_scan = index;
}

runtime::~runtime() {
    DEBUG(3, "Cleaning up runtime" << endl);
    // if threadz running, kill'm!
    DEBUG(4, "Stopping processingchain .... " << endl);
    this->processingchain.stop();
    DEBUG(4, "Stopping processingchain: ok." << endl);
    if( interchain_source_queue ) {
        remove_interchain_queue(interchain_source_queue);
        interchain_source_queue = 0;
    }
    DEBUG(4, "Forcing processingchain decrease refcount ..." << endl);
    this->processingchain = chain();
    DEBUG(4, "Forcing processingchain decrease refcount: ok." << endl);
    DEBUG(4, "Removing key=" << (void*)this << " from " << key_deleters.size() << " per_runtime<> mappings" << endl);
    for(key_deleter_type::iterator p = key_deleters.begin(); p!=key_deleters.end(); p++)
        p->second(p->first, (void*)this);
    DEBUG(4, "Done." << endl);
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
