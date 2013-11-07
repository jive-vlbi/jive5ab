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
#include <iostream>

using namespace std;


string mem2sfxc_fn(bool qry, const vector<string>& args, runtime& rte ) {
    const transfer_type rtm( ::string2transfermode(args[0]) );
    const transfer_type ctm( rte.transfermode );

    ostringstream       reply;
    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Qry may execute always, command will register busy if
    // not doing nothing or mem2sfxc
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==mem2sfxc))

    if ( qry ) {
        reply << "0 : " << (ctm == rtm ? "active" : "inactive") << " ;";
        return reply.str();
    }

    // handle command
    if ( args.size() < 2 ) {
        reply << "8 : " << args[0] << " requires a command argument ;";
        return reply.str();
    }
    
    if ( args[1] == "open" ) {
        if ( ctm != no_transfer ) {
            reply << "6 : already doing " << rte.transfermode << " ;";
            return reply.str();
        }

        if ( args.size() < 3 ) {
            reply << "8 : open command requires a file argument ;";
            return reply.str();            
        }

        const string filename( args[2] );

        // Now that we have all commandline arguments parsed we may
        // construct our headersearcher
        const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                           (unsigned int)rte.trackbitrate(),
                                           rte.vdifframesize());
        
        // set read/write and blocksizes based on parameters,
        // dataformats and compression
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
        
        chain c;
        
        // start with a queue reader
        c.register_cancel(c.add(&stupid_queue_reader, 10, queue_reader_args(&rte)),
                          &cancel_queue_reader);
        // Insert fake frame generator
        c.add(&faker, 10, fakerargs(&rte));
        
        // And write into a socket
        c.register_cancel( c.add(&sfxcwriter, &open_sfxc_socket, filename, &rte),
                           &close_filedescriptor);

        // reset statistics counters
        rte.statistics.clear();

        rte.transfermode    = rtm;
        rte.processingchain = c;
        rte.processingchain.run();
        
        rte.processingchain.communicate(0, &queue_reader_args::set_run, true);
        
       reply << " 0 ;";

    }
    else if ( args[1] == "close" ) {
        if ( ctm != rtm ) {
            reply << "6 : not doing " << args[0] << " ;";
            return reply.str();
        }
        
        try {
            rte.processingchain.stop();
            reply << "0 ;";
        }
        catch ( std::exception& e ) {
            reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
        }
        catch ( ... ) {
            reply << " 4 : Failed to stop processing chain, unknown exception ;";
        }
        
        rte.transfersubmode.clr_all();
        rte.transfermode = no_transfer;
        
   }
    else {
        reply << "2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    return reply.str();
}
