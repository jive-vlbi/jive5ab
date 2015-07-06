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


typedef std::vector<chain::stepid> stepids_type;

// Need a guard function such that if the transfer finishes,
// the transfer is reset automatically
void net2fileguard_fun(runtime* rteptr, stepids_type v) {
    try {
        DEBUG(3, "net2file guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );

        for( stepids_type::iterator s=v.begin(); s!=v.end(); s++ )
            rteptr->processingchain.communicate(*s, &::close_filedescriptor);
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "net2file finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "net2file finalization threw an unknown exception" << std::endl );        
    }
    rteptr->transfermode = no_transfer;
}

// Cleaning up the unix stuff will now be a registered final function
// so we can take it out of the "net2file=close" code
void net2file_cleanup_host(runtime* rteptr, const string oldhost) {
    // If we was doing unix we unlink the server path
    if( rteptr->netparms.get_protocol()=="unix" )
        ::unlink( rteptr->netparms.host.c_str() );

    // put back original host
    rteptr->netparms.host = oldhost;
}


string net2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember the stepid that does the writing, such that we can enquire
    // the amount of bytes it has written
    static per_runtime<chain::stepid> writestep;
    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is possible always, command only when
    // doing nothing or net2file
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==net2file))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode!=net2file ) {
            reply << "inactive : 0";
        } else {
            // We must retrieve the byte counter from the actual step that
            // did the writing
            reply << "active : " << rte.statistics.counter( writestep[&rte] );
        }
        // this displays the flags that are set, in HRF
        //reply << " : " << rte.transfersubmode;
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;
    // net_protocol == udp/tcp
    // open : <filename> [: <strict> ]
    // net_protocol == unix
    // open : <filename> : <unixpath> [ : <strict> ]
    //
    //   <strict>: if given, it must be "1" to be recognized
    //      "1": IF a trackformat is set (via the "mode=" command)
    //           then the (when necessary, decompressed) datastream 
    //           will be run through a filter which ONLY lets through
    //           frames of the datatype indicated by the mode.
    //       default <strict> = 0
    //           (false/not strict/no filtering/blind dump-to-disk)
    //
    if( args[1]=="open" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const string            proto( rte.netparms.get_protocol() );
            const string            oldhost = rte.netparms.host;
            const bool              unix( proto=="unix" );
            const string            filename( OPTARG(2, args) );
            const string            strictarg( OPTARG((unsigned int)(unix?4:3), args) ); 
            const string            uxpath( (unix?OPTARG(3, args):"") );
            unsigned int            strict = 0;
            stepids_type            fdsteps;
            chain::stepid           rdstep, wrstep;
                
            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );
            ASSERT2_COND( (!unix || (unix && uxpath.empty()==false)),
                          SCINFO(" no unix socket given") );

            // We could replace this with
            //  strict = (strictarg=="1")
            // but then the user would not know if his/her value of
            // strict was actually used. better to cry out loud
            // if we didn't recognize the value
            if( strictarg.size()>0 ) {
                char*         eocptr;
                unsigned long strictval = 0;
                   
                errno     = 0; 
                strictval = ::strtoull(strictarg.c_str(), &eocptr, 0);

                // !(A || B) => !A && !B
                ASSERT2_COND( !(strictval==0 && eocptr==strictarg.c_str()),
                              SCINFO("Failed to parse 'strict' value") );
                ASSERT2_COND(strictval>0 && strictval<3, SCINFO("<strict>, when set, MUST be 1 or 2"));
                strict = (unsigned int)strictval;
            }

            // Conflicting request: at the moment we cannot support
            // strict mode on reading compressed Mk4/VLBA data; bits of
            // the syncword will also be compressed and hence, after 
            // decompression, the syncword will contain '0's rather
            // than all '1's, making the framesearcher useless
            ASSERT2_COND( !strict || (strict && !(rte.solution && (rte.trackformat()==fmt_mark4 || rte.trackformat()==fmt_vlba))),
                          SCINFO("Currently we cannot have strict mode with compressed Mk4/VLBA data") );

            // Now that we have all commandline arguments parsed we may
            // construct our headersearcher
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain
            // Also register cancellationfunctions that will close the
            // network and file filedescriptors and notify the threads
            // that it has done so - the threads pick up this signal and
            // terminate in a controlled fashion

            // clear lasthost so it won't bother the "getsok()" which
            // will, when the net_server is created, use the values in
            // netparms to decide what to do.
            // The previous value is saved in 'oldhost' (above) and will
            // be put back by a registered final function (see below)
            // "net2file_cleanup_host()"
            if( unix )
                rte.netparms.host = uxpath;
            else
                rte.netparms.host.clear();

            // start with a network reader
            // HV: 06-Jun-2014 Tell it to accept partial blocks
            rdstep = c.add(&netreader, 32, &net_server, networkargs(&rte, true));
            c.register_cancel(rdstep, &close_filedescriptor);
            fdsteps.push_back( rdstep );

            // Insert a decompressor if needed
            if( rte.solution )
                c.add(&blockdecompressor, 10, &rte);

            // Insert a framesearcher, if strict mode is requested
            // AND there is a dataformat to look for ...
            if( strict && dataformat.valid() ) {
                c.add(&framer<frame>, 10, framerargs(dataformat, &rte, strict>1));
                // only pass on the binary form of the frame
                c.add(&frame2block, 3);
            }

            // And write into a file
            wrstep = c.add(&fdwriter<block>,  &open_file, filename, &rte);
            c.register_cancel(wrstep, &close_filedescriptor);
            fdsteps.push_back( wrstep );
            // store the write step for future reference
            writestep[&rte] = wrstep;

            // Register the guard functions
            // 1) close file descriptors
            // 2) unlink unix server path + put back old host
            c.register_final(&net2fileguard_fun, &rte, fdsteps);
            c.register_final(&net2file_cleanup_host, &rte, oldhost);

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = net2file;
            rte.processingchain = c;
            rte.processingchain.run();
            // Under certain circumstances (currently "mode==none") we allow variable block sizes
            rte.processingchain.communicate(rdstep, &fdreaderargs::set_variable_block_size,
                                            !dataformat.valid());

            // Also find out the current file size (note: it helps asking
            // the right step ... the one that's actually writing to the
            // file! D'oh!
            const off_t  fsz = rte.processingchain.communicate(wrstep, &fdreaderargs::get_file_size);

            reply << " 0 : " << fsz << " ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="close" ) {
        recognized = true;
        // This can be done unconditionally:
        writestep[&rte] = chain::invalid_stepid;

        if( rte.transfermode!=no_transfer ) {
            string error_message;
            try {
                // Ok. stop the threads
                rte.processingchain.stop();
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop processing chain: ") + e.what();
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop processing chain, unknown exception");
            }
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

            if ( error_message.empty() ) {
                reply << " 0 ;";
            }
            else {
                reply << " 4" << error_message << " ;";
            }
        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
