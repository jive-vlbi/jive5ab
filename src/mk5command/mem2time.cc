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
#include <interchainfns.h>
#include <threadfns.h>
#include <tthreadfns.h>
#include <iostream>

using namespace std;

// connect to the datastream, find the frames and decode the timestamps
string mem2time_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // need to remember the stepid of the timedecoder step
    static per_runtime<chain::stepid> timepid;
    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is always possible, command only if doing nothing or already
    // doing mem2time
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==mem2time))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode!=mem2time ) {
            reply << "inactive";
        } else {
            // get the last os + data timestamps and format them
            highresdelta_type      dt;
            const timegrabber_type times = rte.processingchain.communicate(timepid[&rte], &timegrabber_type::get_times);

            reply << "O/S : " << tm2vex(times.os_time) << " : ";
            reply << "data : " << tm2vex(times.data_time) ;
            dt = times.os_time - times.data_time;
            reply << " : " << format("%.3lf", boost::rational_cast<double>(dt)) << "s";
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;
    // <open>
    if( args[1]=="open" ) {
        recognized = true;
        // Attempt to set up the timedecoding chain
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            // In order to be able to decode data we certainly must have
            // a valid data format
            EZASSERT(dataformat.valid(), Error_Code_6_Exception);

            // now start building the processingchain

            // Read from the interchain queue. We use the stupid reader
            // because we send the blocks straight into a framer, which
            // doesn't care about blocksizes. And we can re-use the 
            // blocks because we don't need to go a different blocksize
            queue_reader_args qargs(&rte);
            qargs.run          = true;
            qargs.reuse_blocks = true;
            c.register_cancel(c.add(&stupid_queue_reader, 10, qargs),
                              &cancel_queue_reader);
            // register the queue_reader finalizer
            c.register_final(&finalize_queue_reader, &rte);

            // Add the framer
            c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                
            // And the timegrabber
            timepid[&rte] = c.add(&timegrabber);

            rte.transfersubmode.clr_all().set(run_flag);

            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing
            rte.transfermode = mem2time;

            // The very last thing we do is to start the
            // system - running the chain may throw up and we shouldn't
            // be in an indefinite state
            rte.processingchain = c;
            rte.processingchain.run();
                
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }
    // <close>
    if( ( args[1]=="close" ) ) {
        recognized = true;
        // Only allow if we're doing mem2time.
        // Don't care if we were running or not
        if( rte.transfermode!=no_transfer ) {
            try {
                // do a blunt stop. at the sending end we do not care that
                // much processing every last bit still in our buffers
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }

            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();
            timepid.erase( &rte );

        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
