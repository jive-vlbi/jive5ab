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


string file2check_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Qry is always possible, command only if doing nothing
    // or already doing file2check
    // !q && !(ctm==no_transfer || ctm==file2check) =>
    // !(q || ctm==no_transfer || ctm==file2check)
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==file2check))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode!=file2check ) {
            reply << "inactive : 0";
        } else {
            reply << "active : " << 0 ;
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
    // file2check = connect : <filename> [: 0]
    //    "[: 0]" == turn strict mode off (it is on by default)
    //    if the option is given it is checked it is "0"
    if( args[1]=="connect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const string            filename( OPTARG(2, args) );
            const string            strictopt( OPTARG(3, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // If there's no frameformat given we can't do anything
            // usefull
            EZASSERT2( dataformat.valid(), cmdexception,
                       EZINFO("please set a mode to indicate which data you expect") );

            // these arguments MUST be given
            EZASSERT( filename.empty()==false, cmdexception );

            // If the strict thingy is given, ensure it is "0"
            EZASSERT(strictopt.empty()==true || (strictopt=="0"), cmdexception);

            // Conflicting request: at the moment we cannot support
            // strict mode on reading compressed Mk4/VLBA data; bits of
            // the syncword will also be compressed and hence, after 
            // decompression, the syncword will contain '0's rather
            // than all '1's, making the framesearcher useless
            EZASSERT2( !(rte.solution && (rte.trackformat()==fmt_mark4 || rte.trackformat()==fmt_vlba)), cmdexception,
                       EZINFO("Currently we cannot deal with compressed Mk4/VLBA data") );

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain

            // Add a step to the chain (c.add(..)) and register a
            // cleanup function for that step, in one go
            c.register_cancel( c.add(&fdreader, 32, &open_file, filename, &rte),
                               &close_filedescriptor);

            // Insert a decompressor if needed
            if( rte.solution )
                c.add(&blockdecompressor, 10, &rte);

            // Insert a framesearcher
            c.add(&framer<frame>, 10, framerargs(dataformat, &rte, strictopt.empty() /*true*/));

            // And send to the timedecoder
            c.add(&timeprinter, dataformat);

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = file2check;
            rte.processingchain = c;
            rte.processingchain.run();
                
            rte.processingchain.communicate(0, &fdreaderargs::set_run, true);

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="disconnect" ) {
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

        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
