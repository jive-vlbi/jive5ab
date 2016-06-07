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


string net2sfxc_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember previous host setting
    static per_runtime<string>       hosts;
    static per_runtime<unsigned int> bps;
    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode
    const transfer_type rtm( ::string2transfermode(args[0]) ); // requested transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query always possible, command only when doing nothing
    // or already doing net2sfxc or net2sfxcfork
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==rtm))

    // Good. See what the usr wants
    if( qry ) {
        const string arg( OPTARG(1, args) );

        if( !arg.empty() ) {
            if( arg=="bits_per_sample" ) {
                per_runtime<unsigned int>::iterator  p = bps.find( &rte );
                if( p==bps.end() )
                    reply << "6 : no value for bits_per_sample has been set yet;";
                else
                    reply << "0 : " << p->second << " ;";
            } else if( !arg.empty() ) {
                reply << "8 : unrecognized query argument '" << arg << "' ;";
            }
        } else {
            reply << " 0 : ";
            if( ctm!=rtm ) {
                reply << "inactive : 0";
            } else if ( rte.transfermode==net2sfxc ){
                reply << "active : " << 0 /*rte.nbyte_from_mem*/;
            } else {
                reply << "forking : " << 0 /*rte.nbyte_from_mem*/;
            }
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
    // open : <filename> [: <strict> ]
    //   <strict>: if given, it must be "1" to be recognized
    //      "1": IF a trackformat is set (via the "mode=" command)
    //           then the (when necessary, decompressed) datastream 
    //           will be run through a filter which ONLY lets through
    //           frames of the datatype indicated by the mode.
    //   <extrastrict>: if given it must be "0"
    //           this makes the framechecking less strict by
    //           forcing only a match on the syncword. By default it is
    //           on, you can only turn it off. 
    //       
    //       default <strict> = 0
    //           (false/not strict/no filtering/blind dump-to-disk)
    //
    if( args[1]=="open" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            bool                    strict( false );
            chain                   c;
            const string            filename( OPTARG(2, args) );
            const string            strictarg( OPTARG(3, args) ); 
            const string            proto( rte.netparms.get_protocol() );

                
            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );

            // We could replace this with
            //  strict = (strictarg=="1")
            // but then the user would not know if his/her value of
            // strict was actually used. better to cry out loud
            // if we didn't recognize the value
            if( strictarg.size()>0 ) {
                ASSERT2_COND(strictarg=="1", SCINFO("<strict>, when set, MUST be 1"));
                strict = true;
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
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            throw_on_insane_netprotocol(rte);

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
                c.add(&blockdecompressor, 10, &rte);

            // Insert a framesearcher, if strict mode is requested
            // AND there is a dataformat to look for ...
            if( strict && dataformat.valid() ) {
                c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                // only pass on the binary form of the frame
                c.add(&frame2block, 3);
            }

            if ( rtm == net2sfxcfork ) {
                c.add(&queue_forker, 10, queue_forker_args(&rte));
            }

            // Insert fake frame generator
            // Update: check if a bits-per-sample value was set, come up with default of
            // '2' in case when it's not.
            per_runtime<unsigned int>::iterator p = bps.find( &rte );
            c.add(&faker, 10, fakerargs(&rte, (p!=bps.end()) ? p->second : 2 ));

            // And write into a socket
            c.register_cancel( c.add(&sfxcwriter,  &open_sfxc_socket, filename, &rte),
                               &close_filedescriptor );

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = rtm;
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
    if( args[1]=="bits_per_sample" ) {
        recognized = true;
        // Can only do this if not doing mem2sfxc
        if( rte.transfermode==no_transfer ) {
            // We *must* have a second argument, the actual bits-per-sample
            // value ..
            const string       bps_s( OPTARG(2, args) );

            if( bps_s.empty() ) {
                reply << " 8 : command requires an actual bits-per-sample value ;";
            } else {
                // Ok, let's see what we got
                char*              eocptr;
                unsigned long int  bits_per_sample;

                errno           = 0;
                bits_per_sample = ::strtoul(bps_s.c_str(), &eocptr, 0);
                EZASSERT2(eocptr!=bps_s.c_str() && *eocptr=='\0' &&
                          errno!=EINVAL && errno!=ERANGE && bits_per_sample>0 && bits_per_sample<=32,
                          cmdexception, EZINFO("invalid value for bits_per_sample given"));
                bps[ &rte ] = (unsigned int)bits_per_sample;
                reply << " 0 ;";
            }
        } else {
            reply << " 5 : not whilst " << rte.transfermode << " is active ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
