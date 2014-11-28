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
#include <sys/timeb.h>
#include <sciprint.h>
#include <iostream>

using namespace std;

// tstat? 
//   "old" style/default output format:
//   !tstat? 0 : <delta-t> : <transfer> : <step1 name> : <step1 rate> : <step2 name> : ... ;
//      with 
//         <stepN name> is the descriptive ASCII text of the actual step
//         <stepN rate> formatted <float>[mckMG]bps"
//         <delta-t>    elapsed wall-clock time since last invocation of
//                      "tstat?". If >1 user is polling "tstat?" you'll
//                      get funny results
//
// tstat= <mumbojumbo>  (tstat as a command rather than a query)
//   whatever argument you specify is completely ignored.
//   the format is now:
//
//   !tstat= 0 : <timestamp> : <transfer> : <step1 name> : <step1 counter> : <step2 name> : <step2 counter>
//       <timestamp>  UNIX timestamp + added millisecond fractional seconds formatted as a float
//      
//       This allows you to poll at your own frequency and compute the rates
//       for over that period. Or graph them. Or throw them away.
string tstat_fn(bool q, const vector<string>& args, runtime& rte ) {
    double                          dt;
    uint64_t                        fifolen;
    const double                    fifosize( 512 * 1024 * 1024 );
    ostringstream                   reply;
    chainstats_type                 current;
    transfer_type                   transfermode;
    struct timeb                    time_cur;
    static per_runtime<timeb*>      time_last_per_runtime;
    struct timeb*&                  time_last = time_last_per_runtime[&rte];
    static per_runtime<chainstats_type> laststats_per_runtime;
    chainstats_type&                laststats = laststats_per_runtime[&rte];
    chainstats_type::const_iterator lastptr, curptr;

    // The tstat command and query can always be performed

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    // make a copy of the statistics with the lock on the runtimeenvironment
    // held
    RTEEXEC(rte, transfermode = rte.transfermode; current=rte.statistics);

    if( transfermode==no_transfer ) {
        reply << "0 : 0.0 : no_transfer ;";
        return reply.str();
    }

    // must serialize access to the StreamStor
    // (hence the do_xlr_[un]lock();
    do_xlr_lock();
    ::ftime( &time_cur );
    fifolen = ::XLRGetFIFOLength(rte.xlrdev.sshandle());
    do_xlr_unlock();

    // Are we called as a command? Then our lives are much easier!
    if( !q ) {
        double tijd = (double)time_cur.time + ((double)time_cur.millitm)/1.0e3;

        // indicate succes and output timestamp + transfermode
        reply << " 0 : "
              << format("%.3lf", tijd) << " : "
              << transfermode ;

        // output each chainstatcounter
        for(curptr=current.begin(); curptr!=current.end(); curptr++)
            reply << " : " << curptr->second.stepname << " : " << curptr->second.count;

        // finish off with the FIFOLength counter
        reply << " : FIFOLength : " << fifolen;

        // and terminate the reply
        reply << ';';
        return reply.str();
    }

    // Must check if the current transfer matches the saved one - if not we
    // must restart our timing
    for(lastptr=laststats.begin(), curptr=current.begin();
        lastptr!=laststats.end() && curptr!=current.end() &&
            lastptr->first==curptr->first && // check that .first (==stepid) matches
            lastptr->second.stepname==curptr->second.stepname; // check that stepnames match
        lastptr++, curptr++) {};
    // If not both lastptr & curptr point at the end of their respective
    // container we have a mismatch and must start over
    if( !(lastptr==laststats.end() && curptr==current.end()) ) {
        delete time_last;
        time_last = NULL;
    }

    if( !time_last ) {
        time_last = new struct timeb;
        *time_last = time_cur;
    }

    // Compute 'dt'. If it's too small, do not even try to compute rates
    dt = (time_cur.time + time_cur.millitm/1000.0) - 
         (time_last->time + time_last->millitm/1000.0);

    if( dt>0.1 ) {
        double fifolevel    = ((double)fifolen/fifosize) * 100.0;

        // Indicate success and report dt in seconds and the
        // transfer we're running
        reply << " 0 : "
              << format("%5.2lfs", dt) << " : "
              << transfermode ;
        // now, for each step compute the rate. we've already established
        // equivalence making the stop condition simpler
        for(curptr=current.begin(), lastptr=laststats.begin();
            curptr!=current.end(); curptr++, lastptr++) {
            double rate = (((double)(curptr->second.count-lastptr->second.count))/dt)*8.0;
            reply << " : " << curptr->second.stepname << " " << sciprintd(rate,"bps");
        }
        // Finish off with the FIFO percentage
        reply << " : F" << format("%4.1lf%%", fifolevel) << " ;";
    } else {
        // dt is too small; request to try again
        reply << " 1 : Retry - we're initialized now : " << transfermode << " ;";
    }

    // Update statics
    *time_last = time_cur;
    laststats  = current;
    return reply.str();
}
