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


string mem2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // Need to remember the queue-reader's stepid for communication
    static per_runtime<chain::stepid>   queuereader_sid;

    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query may execute always, command only when
    // doing nothing or already doing mem2file
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==mem2file))

    if ( qry ) {
        reply << "0 : ";
        if ( rte.transfermode != mem2file ) {
            reply << "inactive ;";
            return reply.str();
        }

        if ( rte.processingchain.communicate(0, &queue_reader_args::is_finished)) {
            if ( rte.processingchain.communicate(1, &fdreaderargs::is_finished) ) {
                reply << "done";
            }
            else {
                reply << "flushing";
            }
        }
        else {
            reply << "active";
        }
        reply << " : " << rte.statistics.counter(1) << " ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        reply << "8 : command requires an argument ;";
        return reply.str();
    }

    if ( args[1] == "on" ) {
        if ( rte.transfermode != no_transfer ) {
            reply << "6 : already doing " << rte.transfermode << " ;";
            return reply.str();
        }

        // mem2file = on : <file> : <max bytes in buffer> [: <file option>]
        const string filename    ( OPTARG(2, args) );
        const string bytes_string( OPTARG(3, args) );
              string option      ( OPTARG(4, args) );
        
        if ( filename.empty() ) {
            reply << "8 : command requires a destination file ;";
            return reply.str();
        }

        // HV: 31-Mar-2014  Implement a default #-of-bytes to buffer
        uint64_t bytes = 256 * 1024 * 1024;

        if( bytes_string.empty()==false ) {
            char*    eocptr;
            errno = 0;
            bytes = ::strtoull(bytes_string.c_str(), &eocptr, 0);
            ASSERT2_COND( (bytes!=0 || eocptr!=bytes_string.c_str()) && errno!=ERANGE && errno!=EINVAL,
                          SCINFO("Failed to parse bytes to buffer") );
        }

        if ( option.empty() ) {
            option = "n";
        }

        // now start building the processingchain
        const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                           rte.trackbitrate(),
                                           rte.vdifframesize());
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
     
        unsigned int blocks = (bytes + rte.netparms.get_blocksize() - 1) / rte.netparms.get_blocksize(); // round up

        
        chain          c;
        chain::stepid  sid;

        // For a transfer like this a stupid queue reader should be enough
        sid = c.add(&stupid_queue_reader, blocks, queue_reader_args(&rte));
        c.register_cancel(sid, &cancel_queue_reader);
        queuereader_sid[&rte] = sid;

        // also register a finalizer that removes the queue as soon as
        // the transfer is finished
        c.register_final(&finalize_queue_reader, &rte);
   
        c.add(&fdwriter<block>,  &open_file, filename + "," + option, &rte );
       
        // reset statistics counters
        rte.statistics.clear();

        rte.transfermode    = mem2file;
        rte.processingchain = c;
        rte.processingchain.run();

        rte.processingchain.communicate(0, &queue_reader_args::set_run, true);
    }
    else if ( args[1] == "stop" ) {
        if ( rte.transfermode != mem2file ) {
            reply << "6 : not doing " << args[0] << " ;";
            return reply.str();
        }
        // We must stop the queue reader & delayed-disable the
        // processing chain in order to flush all the data still
        // queued
        rte.processingchain.communicate(queuereader_sid[&rte], &cancel_queue_reader);

        rte.processingchain.delayed_disable();
    }
    else if ( args[1] == "off" ) {
        if ( rte.transfermode != mem2file ) {
            reply << "6 : not doing " << args[0] << " ;";
            return reply.str();
        }

        rte.transfermode = no_transfer;

        rte.processingchain.stop();
        rte.processingchain = chain();
    }
    else {
        reply << "2 : " << args[1] << " is not a valid command argument ;";
        return reply.str();
    }

    reply << "0 ;";
    return reply.str();
}
