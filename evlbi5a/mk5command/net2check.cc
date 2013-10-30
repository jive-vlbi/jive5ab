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
#include <iostream>

using namespace std;


string net2check_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember previous host setting
    static per_runtime<string> hosts;
    // automatic variables
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==net2check);

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( !atm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            reply << "active : 0";
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
    // open [: [<start>] : [<inc>] : [<time>] ]
    //    initialize the fillpattern start value with <start>
    //    and increment <inc> [64bit numbers!]
    //
    //    Defaults are:
    //      <start> = 0x1122334411223344
    //      <int>   = 0
    //
    //    <time>   if this is a non-empty string will do/use
    //             timestampchecking, fillpattern ignored
    if( args[1]=="open" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            char*                   eocptr;
            chain                   c;
            const bool              dotime = (OPTARG(4, args).empty()==false);
            fillpatargs             fpargs(&rte);
            const string            start_s( OPTARG(2, args) );
            const string            inc_s( OPTARG(3, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            if( start_s.empty()==false ) {
                errno       = 0;
                fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                // !(A || B) => !A && !B
                ASSERT2_COND( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                              SCINFO("Failed to parse 'start' value") );
            }
            if( inc_s.empty()==false ) {
                errno      = 0;
                fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                // !(A || B) => !A && !B
                ASSERT2_COND( !(fpargs.inc==0 && eocptr==inc_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                              SCINFO("Failed to parse 'inc' value") );
            }
                
            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain
            // clear lasthost so it won't bother the "getsok()" which
            // will, when the net_server is created, use the values in
            // netparms to decide what to do.
            // Also register cancellationfunctions that will close the
            // network and file filedescriptors and notify the threads
            // that it has done so - the threads pick up this signal and
            // terminate in a controlled fashion
            hosts[&rte] = rte.netparms.host;
            rte.netparms.host.clear();

            // Add a step to the chain (c.add(..)) and register a
            // cleanup function for that step, in one go
            c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                               &close_filedescriptor);

            // Insert a decompressor if needed
            if( rte.solution )
                c.add(&blockdecompressor, 32, &rte);

            // And write to the checker
            if( dotime ) {
                c.add(&framer<frame>, 32, framerargs(dataformat, &rte, false));
                c.add(&timechecker,  dataformat);
            } else
                c.add(&checker, fpargs);

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = net2check;
            rte.processingchain = c;
            rte.processingchain.run();

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="close" ) {
        recognized = true;
        if( rte.transfermode!=no_transfer ) {
            try {
                // Ok. stop the threads
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

            // put back original host
            rte.netparms.host = hosts[&rte];

            reply << " 0 ;";
        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
