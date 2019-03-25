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
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <busywait.h>
#include <iostream>

using namespace std;


// wait for 1PPS-sync to appear. This is highly Mk5B
// spezifik so if you call this onna Mk5A itz gonna FAIL!
// Muhahahahaa!
//  pps=* [force resync]  (actual argument ignored)
//  pps?  [report 1PPS status]
string pps_fn(bool q, const vector<string>& args, runtime& rte) {
    double              dt;
    ostringstream       reply;
    const double        syncwait( 3.0 ); // max time to wait for PPS, in seconds
    struct ::timeval    start, end;
    const unsigned int  selpp( *rte.ioboard[mk5breg::DIM_SELPP] );
    const transfer_type ctm( rte.transfermode );

    reply << "!" << args[0] << (q?('?'):('='));

    // This is an unlisted/unofficial command. It allows one to sync the pps
    // manually. Let's support doing it as long as the i/o board is not in
    // use (command). Query may be executed always.
    INPROGRESS(rte, reply, !q && (fromio(ctm) || toio(ctm)))

    // if there's no 1PPS signal set, we do nothing
    if( selpp==0 ) {
        reply << " 6 : No 1PPS signal set (use 1pps_source command) ;";
        return reply.str();
    }

    // good, check if query
    if( q ) {
        const bool  sunk( *rte.ioboard[mk5breg::DIM_SUNKPPS] );
        const bool  e_sync( *rte.ioboard[mk5breg::DIM_EXACT_SYNC] );
        const bool  a_sync( *rte.ioboard[mk5breg::DIM_APERTURE_SYNC] );

        // check consistency: if not sunk, then neither of exact/aperture
        // should be set (i guess), nor, if sunk, may both be set
        // (the pps is either exact or outside the window but not both)
        reply << " 0 : " << (!sunk?"NOT ":"") << " synced ";
        if( e_sync )
            reply << " [not incident with DOT1PPS]";
        if( a_sync )
            reply << " [> 3 clocks off]";
        reply << " ;";
        return reply.str();
    }

    // ok, it was command.
    // trigger a sync attempt, wait for some time [3 seconds?]
    // at maximum for the PPS to occur, clear the PPSFLAGS and
    // then display the systemtime at which the sync occurred

    // Note: the poll-loop below might be implementen rather 
    // awkward but I've tried to determine the time-of-sync
    // as accurate as I could; therefore I really tried to 
    // remove as much "unknown time consuming" systemcalls
    // as possible.
    volatile bool      sunk = false;
    const unsigned int wait_per_iter = 2; // 2 microseconds/iteration
    unsigned long int  max_loops = ((unsigned long int)(syncwait*1.0e6)/wait_per_iter);

    // Pulse SYNCPPS to trigger zynchronization attempt!
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 1;
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 0;

    // now wait [for some maximum amount of time]
    // for SUNKPPS to transition to '1'
    ::gettimeofday(&start, 0);
    while( max_loops-- ) {
        if( (sunk=*rte.ioboard[mk5breg::DIM_SUNKPPS])==true )
            break;
        // Ok, SUNKPPS not 1 yet.
        // sleep a bit and retry
        busywait( wait_per_iter );
    };
    ::gettimeofday(&end, 0);
    dt = ((double)end.tv_sec + (double)end.tv_usec/1.0e6) -
        ((double)start.tv_sec + (double)start.tv_usec/1.0e6);

    if( !sunk ) {
        reply << " 4 : Failed to sync to 1PPS within " << dt << "seconds ;";
    } else {
        char      tbuf[128];
        double    frac_sec;
        struct tm gmt;

        // As per Mark5B-DIM-Registers.pdf Sec. "Typical sequence of operations":
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 1;
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 0;

        // convert 'timeofday' at sync to gmt
        ASSERT_NZERO( ::gmtime_r(&end.tv_sec, &gmt) );
        frac_sec = end.tv_usec/1.0e6;
        ::strftime(tbuf, sizeof(tbuf), "%a %b %d %H:%M:", &gmt);
        reply << " 0 : sync @ " << tbuf
              << format("%0.8lfs", ((double)gmt.tm_sec+frac_sec)) << " [GMT]" << " ;";
    }
    return reply.str();
}
