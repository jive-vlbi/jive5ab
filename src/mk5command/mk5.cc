// Generic Mark5 utilities shared between all Mark5 flavours
// Copyright (C) 2007-2013 Harro Verkouter
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
#include <mk5command/mk5.h>
#include <mk5_exception.h>
#include <dotzooi.h>
#include <timezooi.h>
#include <jive5a_bcd.h>

using namespace std;

// Set up the Mark5B/DIM input section to:
//   * sync to 1PPS (if a 1PPS source is set)
//   * set the time at the next 1PPS
//   * start generating on the next 1PPS
// Note: so *if* this function executes completely,
// it will start generating diskframes.
//
// This is done to ascertain the correct relation
// between DOT & data.
//
// dfhg = disk-frame-header-generator
//
// 'maxsyncwait' is the amount of time in seconds the system
// should at maximum wait for a 1PPS to appear.
// Note: if you said "1pps_source=none" then this method
// doesn't even try to wait for a 1pps, ok?
void start_mk5b_dfhg( runtime& rte, double /*maxsyncwait*/ ) {
    const double    minttns( 0.7 ); // minimum time to next second (in seconds)
    // (best be kept >0.0 and <1.0 ... )

    // Okie. Now it's time to start prgrm'ing the darn Mk5B/DIM
    // This is a shortcut: we rely on the Mk5's clock to be _quite_
    // accurate. We have to set the DataObservingTime at the next 1PPS
    // before we kick off the data-frame-header-generator.
    // Make sure we are not too close to the next integral second:
    // we need some processing time (computing JD, transcode to BCD
    // write into registers etc).
    int                         mjd;
    int                         tmjdnum; // truncated MJD number
    int                         nsssomjd;// number of seconds since start of mjd
    time_t                      tmpt;
    double                      ttns; // time-to-next-second, delta-t
    struct tm                   gmtnow;
    pcint::timeval_type         dot;
    mk5breg::regtype::base_type time_h, time_l;

    // Trigger reset of all DIM statemachines. As per
    // the docs, this 'does not influence any settable
    // DIM parameter' (we hope)
    rte.ioboard[ mk5breg::DIM_RESET ] = 1;
    rte.ioboard[ mk5breg::DIM_RESET ] = 0;

    // Great. Now wait until we reach a time which is sufficiently before 
    // the next integral second of DOT!
    do {
        // wait 1 millisecond (on non-RT kernels this is probably more like
        // 10ms)
        ::usleep(100);
        // Get the DOT and verify it's running
        dot_type dt = get_dot();
        EZASSERT2( dt, cmdexception, EZINFO("DOT not valid, status: " << dt.dot_status) );

        // compute time-to-next-(integral) DOT second
        ttns = 1.0 - (double)(dt.dot.timeValue.tv_usec/1.0e6);
        dot = dt.dot;
    } while( ttns<minttns );
    
    // Good. Now be quick about it.
    // We know what the DOT will be (...) at the next 1PPS.
    // From the wait loop above we have our latest estimate of the
    // actual DOT.
    // Add 1 second, transform
    // Transform localtime into GMT, get the MJD of that,
    // transform that to "VLBA-JD" (MJD % 1000) and finally
    // transform *that* into B(inary)C(oded)D(ecimal) and
    // write it into the DIM
    // Note: do NOT forget to increment the tv_sec value 
    // because we need the next second, not the one we're in ;)
    // and set the tv_usec value to '0' since ... well .. it
    // will be the time at the next 1PPS ...
    tmpt = (time_t)(dot.timeValue.tv_sec + 1);
    ::gmtime_r( &tmpt, &gmtnow );

    // Get the MJD daynumber
    mjd = ::jdboy( gmtnow.tm_year+1900 ) + gmtnow.tm_yday;
    tmjdnum  = (mjd % 1000);
    nsssomjd = gmtnow.tm_hour * 3600 + gmtnow.tm_min*60 + gmtnow.tm_sec;
    DEBUG(2, "Got mjd for next 1PPS: " << mjd << " => TMJD=" << tmjdnum << ". Number of seconds: " << nsssomjd << endl);

    // Now we must go to binary coded decimal
    unsigned int t1, t2;
    bcd(tmjdnum, t1);
    bcd(nsssomjd, t2);

    // Transfer to the correct place in the start_time
    // JJJS   SSSS
    // time_h time_l
    time_h  = (mk5breg::regtype::base_type)(((t1 & 0xfff)) << 4);

    // Get the 5th bcd digit of the 'seconds-since-start-of-mjd'
    // and move it into the lowest bcd of the high-word of START_TIME
    time_h = (mk5breg::regtype::base_type)(time_h | ((t2 >> 16)&0xf));

    // the four lesser most-significant bcd digits of the 
    // 'seconds-since-start etc' go into the lo-word of START_TIME
    time_l  = (mk5breg::regtype::base_type)(t2 & 0xffff);
    DEBUG(2, "Writing BCD StartTime H:" << hex_t(time_h) << " L:" << hex_t(time_l) << endl);

    // Fine. Bung it into the DIM
    rte.ioboard[ mk5breg::DIM_STARTTIME_H ] = time_h;
    rte.ioboard[ mk5breg::DIM_STARTTIME_L ] = time_l;

    // Now we issue a SETUP, wait for at least '135 data-clock-cycles'
    // before releasing it. We'll approximate this by just sleeping
    // 10ms.
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 1;
    ::usleep( 10000 );
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 0;

    // Weehee! Start the darn thing on the next PPS!
    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 1;

    return;
}

// Based on the information found in the runtime compute
// the theoretical IPD. 
// YOU MUST HAVE FILLED "rte.sizes" WITH THE RESULT OF A constrain()
// FUNCTION CALL BEFORE ACTUALLY CALLING THIS ONE!
void compute_theoretical_ipd( runtime& rte ) {
    netparms_type&     net( rte.netparms );
    const unsigned int datagramsize( rte.sizes[constraints::write_size] );

    if( datagramsize>0 ) {
        // total bits-per-second to send divided by the mtu (in bits)
        //  = number of packets per second to send. from this follows
        //  the packet spacing trivially
        // the trackbitrate already includes headerbits; both
        // for VLBA non-data-replacement bitrate and for Mark4 datareplacement.
        // the amount of headerbits for Mk5B format is marginal wrt total
        // bitrate.
        //
        // 03 Sep 2013: HV - correction factor changed from 0.9 => 1.1
        //                   just found out that the correction factor
        //                   corrected the wrong way around. The idea is
        //                   to compensate for headers and other overhead
        //                   but as it was it would make the ipd
        //                   consistently too large (by more than 10%)
        //                   causing auto_ipd to let the FIFO fill
        //                   up/overflow.
        //
        // TODO: take compression into account
        //       30 Jun 2010 HV - hopefully done.
        //
        // 20 Aug 2010: HV - ipd @ 1Gbps comes out as 124 us which is too
        //                   large; see FIFO filling up. Decided to add
        //                   a 0.9 fraction to the theoretical ipd
        const double correctionfactor( 1.1 );
        const double factor((rte.solution)?(rte.solution.compressionfactor()):1.0);
        const double n_pkt_p_s = ((rte.ntrack() * boost::rational_cast<double>(rte.trackbitrate()) * factor) / (datagramsize*8))
                                 * correctionfactor;
        DEBUG(3, "compute_theoretical_ipd: ntrack=" << rte.ntrack() << " @" << rte.trackbitrate() << " dgsize="
                 << datagramsize << " => n_pkt_p_s=" << n_pkt_p_s << endl);
        // Note: remember! ipd should be in units of microseconds
        //       previous computation (before fix) yielded units 
        //       of seconds .. d'oh!
        if( n_pkt_p_s>0 ) {
            // floor(3) the value into integral nanoseconds;
            // the IPD can better be too small rather than too high.
            net.theoretical_ipd_ns = (int) ::floor( 1.0E9/n_pkt_p_s );
            DEBUG(1, "compute_theoretical_ipd: " << net.theoretical_ipd_ns << "ns" << endl);
        }
    }
    return;
}

// XLR Buffer creation and destruction
XLR_Buffer::XLR_Buffer( uint64_t len ) :
    data( new READTYPE[(len + sizeof(READTYPE) - 1) / sizeof(READTYPE)] )
{}
    
XLR_Buffer::~XLR_Buffer() {
    delete [] data;
}

// Functions to transfer between user interface and Mark5A I/O board values
mk5areg::regtype::base_type track2register( unsigned int track ) {
    if ( 2 <= track && track <= 33 ) {
        return track - 2;
    }
    if ( 102 <= track && track <= 133 ) {
        return track - 102 + 32;
    }
    THROW_EZEXCEPT(cmdexception, "track (" << track << ") not in allowed ranges [2, 32] or [102,133]");
}

unsigned int register2track( mk5areg::regtype::base_type reg ) {
    if ( reg < 32 ) {
        return reg + 2;
    }
    else {
        return reg + 102 - 32;
    }
}

// Throw on insane net_protocol settings
void throw_on_insane_netprotocol(runtime& rte) {
    static struct {
        uint64_t blocksize;
        uint64_t nblock;
    } netprotocol;

    netprotocol.blocksize = rte.sizes[constraints::blocksize];
    netprotocol.nblock    = rte.netparms.nblock;

    EZASSERT2(netprotocol.blocksize * netprotocol.nblock < 128*MB, cmdexception,
              EZINFO("Check net_protocol - more than 128MB requested"));
}

