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
#include <libvbs.h>       // D'oh
#include <threadfns.h>    // for all the processing steps + argument structs
#include <tthreadfns.h>
#include <inttypes.h>     // For SCNu64 and friends
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>

using namespace std;

void close_vbs(fdreaderargs* fdr) {
    if( fdr->fd!=-1 ) {
        DEBUG(3, "close_vbs: closing fd#" << fdr->fd << endl);
        ::vbs_close( fdr->fd );
    }
    fdr->fd = -1;
}


struct d2n_data_type {
    fdreaderargs*   disk_args;
    chain::stepid   vbsstep;
    chain::stepid   netstep;

    d2n_data_type():
        disk_args( 0 ), vbsstep( chain::invalid_stepid ), netstep( chain::invalid_stepid )
    {}

    ~d2n_data_type() {
        delete disk_args;
        disk_args = 0;
    }
};

typedef per_runtime<d2n_data_type> d2n_map_type;


// The disk2net 'guard' or 'finally' function
void disk2netvbsguard_fun(runtime* rteptr, d2n_map_type::iterator d2nptr) {
    d2n_data_type&  d2n_data( d2nptr->second );
    try {
        DEBUG(3, "disk2net guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );

        if( d2n_data.vbsstep!=chain::invalid_stepid )
            rteptr->processingchain.communicate(d2n_data.vbsstep, &::close_vbs);
        if( d2n_data.netstep!=chain::invalid_stepid )
            rteptr->processingchain.communicate(d2n_data.netstep, &::close_filedescriptor);

    }
    catch ( const std::exception& e) {
        DEBUG(-1, "disk2net finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "disk2net finalization threw an unknown exception" << std::endl );        
    }
    // It is of paramount importance that the runtime's transfermode 
    // gets rest to idle, even in the face of exceptions and lock failures
    // and whatnots
    rteptr->transfermode = no_transfer;
    rteptr->transfersubmode.clr( run_flag );
    // Make absolutely sure that memory is released
    delete d2n_data.disk_args;
    d2n_data.disk_args = 0;
}


// We only do disk2net
string disk2net_vbs_fn( bool qry, const vector<string>& args, runtime& rte) {
    static d2n_map_type    d2n_map;
    ostringstream          reply;
    const transfer_type    ctm( rte.transfermode ); // current transfer mode
    d2n_map_type::iterator d2nptr   = d2n_map.insert( make_pair(&rte, d2n_data_type()) ).first;
    d2n_data_type&         d2n_data = d2nptr->second;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is *always* possible, command will register 'busy'
    // if not doing nothing or the requested transfer mode 
    INPROGRESS(rte, reply, !(qry || ctm==disk2net || ctm==no_transfer))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( ctm==disk2net ) {
            string                 status = "inactive";

            if ( rte.transfersubmode & run_flag )
                status = "active";
            else if ( rte.transfersubmode & connected_flag )
                status = "connected";
            
            // we ARE running so we must be able to retrieve the lasthost
            reply << status
                  << " : " << rte.netparms.host;
            if( d2nptr!=d2n_map.end() ) {
                if ( (rte.transfersubmode & run_flag) && (rte.transfersubmode & connected_flag) ) {
                    off_t start = rte.processingchain.communicate(d2n_data.vbsstep, &fdreaderargs::get_start);
                    reply << " : " << start
                          << " : " << rte.statistics.counter(d2n_data.vbsstep) + start
                          << " : " << rte.processingchain.communicate(d2n_data.vbsstep, &fdreaderargs::get_end);               
                } 
                else {
                    reply << " : " << rte.statistics.counter(d2n_data.vbsstep);
                }
            }
        } else {
            reply << "inactive";
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }


    bool                   recognized = false;


    // <connect>
    //
    //  disk2net = connect : <host>
    //     <host> is optional (remembers last host, if any)
    if( args[1]=="connect" ) {
        recognized = true;
        
        // Will start with a clean slate
        d2nptr->second = d2n_data_type();

        // if transfermode is already disk2net, we ARE already connected
        // (only disk2net::disconnect clears the mode to doing nothing)
        if( rte.transfermode==no_transfer ) {
            // build up a new instance of the chain
            chain                   c;
            const string            protocol( rte.netparms.get_protocol() );
            const string            host( OPTARG(2, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            // Make sure that a scan has been set
            EZASSERT2(rte.mk6info.scanName.empty()==false, cmdexception, 
                      EZINFO(" no scan was set using scan_set="));

            // {disk|fill|file}playback has no mode/playrate/number-of-tracks
            // we do offer compression ... :P
            // HV: 08/Dec/2010  all transfers now key their constraints
            //                  off of the set mode. this allows better
            //                  control for all possible transfers
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            throw_on_insane_netprotocol(rte);

            // After having constrained ourselves, we may safely compute a
            // theoretical IPD
            compute_theoretical_ipd( rte );

            // the networkspecifics. 
            if( !host.empty() )
                rte.netparms.host = host;

            // The chain is vbsreader + netwriter
            // (attempt to) open the recording
            d2n_data.disk_args = open_vbs(rte.mk6info.scanName, &rte);

            // Overwrite values with what we're supposed to be doing
            d2n_data.disk_args->set_start( rte.mk6info.fpStart );
            d2n_data.disk_args->set_end( rte.mk6info.fpEnd );
            d2n_data.disk_args->set_variable_block_size( !(dataformat.valid() && rte.solution) );
            d2n_data.disk_args->set_run( false );

            d2n_data.vbsstep = c.add(&vbsreader, 10, d2n_data.disk_args);
            c.register_cancel(d2n_data.vbsstep, &close_vbs);

            // if the trackmask is set insert a blockcompressor 
            if( rte.solution )
                c.add(&blockcompressor, 10, &rte);

            // register the cancellationfunction for the networkstep
            // which we will first add ;)
            // it will be called at the appropriate moment
            d2n_data.netstep = c.add(&netwriter<block>, &net_client, networkargs(&rte));
            c.register_cancel(d2n_data.netstep, &close_filedescriptor);

            // Register a finalizer which automatically clears the transfer when done
            c.register_final(&disk2netvbsguard_fun, &rte, d2nptr);

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();

            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode = disk2net;
       
            // HV/BE 9 dec 2014 disk2net=connect:... should return '1'
            //                  because we cannot guarantee that the 
            //                  connect phase in the chain has already
            //                  completed 
            reply << " 1 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }

    // <on> : turn on dataflow
    //   disk2net=on[:[+<start_byte>][:<end_byte>|+<amount>]]
    if( args[1]=="on" ) {
        recognized = true;
        // only allow if transfermode==disk2net && submode hasn't got the running flag
        // set AND it has the connectedflag set, because then we can turn
        // the data flow on
        if( rte.transfermode==disk2net && (rte.transfersubmode&connected_flag) && (rte.transfersubmode&run_flag)==false ) {
            off_t           start  = 0;
            off_t           end;
            const string    startstr( OPTARG(2, args) );
            const string    endstr( OPTARG(3, args) );

            // Pick up optional extra arguments:
                
            // start-byte #
            // HV: 11Jun2015 change order a bit. Allow for "+start" to
            //               skip the read pointer wrt to what it was set to
            start = rte.processingchain.communicate(d2n_data.vbsstep, &fdreaderargs::get_start);
            end   = rte.processingchain.communicate(d2n_data.vbsstep, &fdreaderargs::get_end);

            if( startstr.empty()==false ) {
                char*       eocptr;
                uint64_t    tmpstart;

                if( startstr[0]=='-' ) {
                    reply << " 8 : relative start bytenumber not allowed here ;";
                    return reply.str();
                }
                errno = 0;
                tmpstart = ::strtoull(startstr.c_str(), &eocptr, 0);
                ASSERT2_COND( *eocptr=='\0' && eocptr!=startstr.c_str() && errno==0,
                              SCINFO(" failed to parse start byte number") );
                if( startstr[0]=='+' )
                    start += tmpstart;
                else
                    start  = tmpstart;
            }
            // end-byte #
            // if prefixed by "+" this means: "end = start + <this value>"
            // rather than "end = <this value>"
            if( endstr.empty()==false ) {
                char*       eocptr;
                uint64_t    tmpend;

                if( endstr[0]=='-' ) {
                    reply << " 8 : relative end byte number not allowed here ;";
                    return reply.str();
                }
                errno = 0;
                tmpend = ::strtoull(startstr.c_str(), &eocptr, 0);
                ASSERT2_COND( *eocptr=='\0' && eocptr!=endstr.c_str() && errno==0,
                              SCINFO(" failed to parse end byte number") );
                end = (off_t)tmpend;
                if( endstr[0]=='+' )
                    end += start;
                ASSERT2_COND( (end == 0) || (end>start), SCINFO("end-byte-number should be > start-byte-number"));
            }

            // now assert valid start and end, if any
            // so the threads, when kicked off, don't have to
            // think but can just *go*!
            if ( end<start ) {
                reply << " 6 : end byte should be larger than start byte ;";
                return reply.str();
            }

            // Now communicate all to the appropriate step in the chain.
            // We know the diskreader step is always the first step ..
            // make sure we do the "run -> true" as last one, as that's the condition
            // that will make the diskreader go
            rte.processingchain.communicate(d2n_data.vbsstep, &fdreaderargs::set_start,  start);
            rte.processingchain.communicate(d2n_data.vbsstep, &fdreaderargs::set_end,    end);
            rte.processingchain.communicate(d2n_data.vbsstep, &fdreaderargs::set_run,    true);
            reply << " 0 ;";
        } else {
            // transfermode is either no_transfer or disk2net, nothing else
            if( rte.transfermode==disk2net ) {
                if( rte.transfersubmode&connected_flag )
                    reply << " 6 : already running ;";
                else
                    reply << " 6 : not connected yet ;";
            } else 
                reply << " 6 : not doing anything ;";
        }
    }

    // <disconnect>
    if( args[1]=="disconnect" ) {
        recognized = true;
        // Only allow if we're doing disk2net.
        // Don't care if we were running or not
        if( rte.transfermode!=no_transfer ) {
            try {
                // let the runtime stop the threads
                rte.processingchain.stop();
                
                rte.transfersubmode.clr( connected_flag );
                reply << " 1 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
