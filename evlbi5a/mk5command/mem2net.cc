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
#include <threadfns.h>
#include <tthreadfns.h>
#include <interchainfns.h>
#include <iostream>

using namespace std;


// Implement mem2net auto-finish. This allows for mem2net=stop
// to trickle all buffered bytes out and then return the transfer
// to 'idle'. The beauty would be that it also closes the network file
// descriptor such that the remote end also automatically terminates and
// closes files &cet
void mem2netguard_fun(runtime* rteptr, chain::stepid sid) {
    try {
        DEBUG(3, "mem2net guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );

        rteptr->processingchain.communicate(sid, &::close_filedescriptor);
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "mem2net finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "mem2net finalization threw an unknown exception" << std::endl );        
    }
    rteptr->transfermode = no_transfer;
}


string mem2net_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // Need to remember the queue-reader's stepid for communication
    static per_runtime<chain::stepid>   queuereader_sid;

    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query may execute always, command registers 'busy' if
    // not doing nothing or mem2net
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==mem2net))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode!=mem2net ) {
            reply << "inactive";
        } else {
            reply << rte.netparms.host << " : " << rte.transfersubmode;
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
    // <connect>
    if( args[1]=="connect" ) {
        recognized = true;
        // if transfermode is already mem2net, we ARE already connected
        // (only mem2net::disconnect clears the mode to doing nothing)
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const string&           proto   = rte.netparms.get_protocol();
            const bool              rtcp    = (proto=="rtcp");
            chain::stepid           sid;

            // good. pick up optional hostname/ip to connect to
            // unless it's rtcp
            if( args.size()>2 && !args[2].empty() ) {
                if( !rtcp )
                    rte.netparms.host = args[2];
                else
                    DEBUG(0, args[0] << ": WARN! Ignoring supplied host '" << args[2] << "'!" << endl);
            }

            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            // constrain sizes based on network parameters and optional
            // compression. If this is the Mark5A version of 
            // mem2net it can only yield mark4/vlba data and for
            // these formats the framesize/offset is irrelevant for
            // compression since each individual bitstream has full
            // headerinformation.
            // If, otoh, we're running on a mark5b we must look for
            // frames first and compress those.
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
                
            // come up with a theoretical ipd
            compute_theoretical_ipd(rte);
                
            // now start building the processingchain
            queue_reader_args qargs(&rte);
            qargs.reuse_blocks = true;

            // If we're doing uncompressed data transfer over the net using
            // a streaming protocol (tcp*, udt) we can get away with the
            // stupid queue reader; no attention will be paid to the actual
            // block sizes!
            const bool stupid = !rte.solution && (proto=="udt" || proto.find("tcp")!=string::npos);

            // Need to remember the stepid of the queue-reader; we need it twice
            sid = c.add( stupid ? &stupid_queue_reader : queue_reader, 10, qargs);

            c.register_cancel(sid, &cancel_queue_reader);
            queuereader_sid[&rte] = sid;

            // register a finalizer which removes the interchain queue as
            // soon as the transfer is finished
            c.register_final(&finalize_queue_reader, &rte);

            // If compression requested then insert that step now
            if( rte.solution ) {
                // In order to prevent bitshift (when the datastream
                // does not start exactly at the start of a dataframe)
                // within a dataframe (leading to throwing away the
                // wrong bitstream upon compression) we MUST always
                // insert a framesearcher.
                // This guarantees that only intact frames are sent
                // to the compressor AND the compressor knows exactly
                // where all the bits of the bitstreams are
                compressorargs cargs( &rte );

                DEBUG(0, "mem2net: enabling compressor " << dataformat << endl);
                if( dataformat.valid() ) {
                    c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                    c.add(&framecompressor, 10, compressorargs(&rte));
                } else {
                    c.add(&blockcompressor, 10, &rte);
                }
            }
                
            // Write to network; remember the stepid because we need that
            // for the cancellation function and the finalizer
            sid = c.add(&netwriter<block>, &net_client, networkargs(&rte));
            // The cancellation function
            c.register_cancel(sid, &close_filedescriptor);
            // And the finalizer
            c.register_final(&mem2netguard_fun, &rte, sid);

            rte.transfersubmode.clr_all().set(wait_flag);

            // reset statistics counters
            rte.statistics.clear();

            // The very last thing we do is to start the
            // system - running the chain may throw up and we shouldn't
            // be in an indefinite state
            rte.processingchain = c;
            rte.processingchain.run();

            // Update global transferstatus variables to
            // indicate what we're doing
            rte.transfermode = mem2net;

                
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }
    if ( args[1]=="on" ) {
        recognized = true;
        if ( rte.transfermode == mem2net && (rte.transfermode & wait_flag) ) {
            rte.processingchain.communicate(0, &queue_reader_args::set_run, true);
            reply << " 0 ;";
        }
        else {
            reply << " 6 : " << args[0] << " not connected ;";
        }
    }
    // <stop> (=gentle stop: send all buffered bytes)
    if( args[1]=="stop" ) {
        recognized = true;
        // Only allow if we're doing mem2net.
        // Don't care if we were running or not
        if( rte.transfermode!=no_transfer ) {
            try {
                // We must stop the queue reader & delayed-disable the
                // processing chain in order to flush all the data still
                // queued
                rte.processingchain.communicate(queuereader_sid[&rte], &cancel_queue_reader);

                // Stop ingesting data into the processing chain; keep on
                // processing everything that's buffered. We don't wait 
                // for it to finish
                rte.processingchain.delayed_disable();
                reply << " 1 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to delayed-disable processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to delayed-disable processing chain, unknown exception ;";
            }
        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    // <disconnect>
    if( ( args[1]=="disconnect" ) ) {
        recognized = true;
        // Only allow if we're doing mem2net.
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

            rte.processingchain = chain();
            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();
        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
