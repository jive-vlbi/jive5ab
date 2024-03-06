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
#include <threadfns.h>    // for all the processing steps + argument structs
#include <threadfns/multisend.h>
#include <inttypes.h>     // For SCNu64 and friends
#include <limits.h>


#include <iostream>

using namespace std;

// The disk2net 'guard' or 'finally' function
void vbs2netguard_fun(runtime* rteptr) {
    try {
        DEBUG(3, "vbs2net guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "vbs2net finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "vbs2net finalization threw an unknown exception" << std::endl );        
    }
    rteptr->transfermode = no_transfer;
}


// Per runtime we keep the settings of how many parallel readers +
// senders are started.
// The default c'tor assumes 1 each - the absolute minimum
struct nthread_type {
    unsigned int    nParallelReader;
    unsigned int    nParallelSender;

    nthread_type() :
        nParallelReader( 1 ), nParallelSender( 1 )
    {}
};


// Support vbs2net
string vbs2net_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream                    reply;
    const transfer_type              ctm( rte.transfermode ); // current transfer mode
    static per_runtime<nthread_type> nthread;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is *always* possible, command will register 'busy'
    // if not doing nothing or the requested transfer mode 
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==vbs2net))

    // Good. See what the usr wants
    if( qry ) {
        // may query 'nthread' rather than vbs2net status
        //    vbs2net?         => vbs2net status
        //    vbs2net? nthread => query how many threads configured
        const string    what( OPTARG(1, args) );

        // Queries always work
        reply << " 0 : ";

        if( what=="nthread" ) {
            reply << nthread[&rte].nParallelReader << " : " << nthread[&rte].nParallelSender;
        } else {
            if( ctm==no_transfer ) {
                reply << "inactive";
            } else {
                string status = "inactive";
                if ( rte.transfersubmode & run_flag ) {
                    status = "active";
                }
                else if ( rte.transfersubmode & connected_flag ) {
                    status = "connected";
                }
                // we ARE running so we must be able to retrieve the lasthost
                reply << status
                      << " : " << rte.netparms.get_host()
                      << " : " << rte.statistics.counter(0);
            }
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
    //  0         1         2        3
    //  vbs2net = connect : <scan> : [<host>]
    //     <scan> will collect all data files from
    //            /mnt/disk*/<scan>/* and send them
    //     <host> is optional (remembers last host, if any)
    if( args[1]=="connect" ) {
        recognized = true;
        // if transfermode is already disk2net, we ARE already connected
        if( rte.transfermode==no_transfer ) {
            // build up a new instance of the chain
            chain                   c;
            const string            protocol( rte.netparms.get_protocol() );
            const string            scan( OPTARG(2, args) );
            const string            host( OPTARG(3, args) );
            chain::stepid           s0, s1, s2;
            const nthread_type&     nthreadref = nthread[&rte];

            // At the moment we can only do this over tcp or udt or unix
            EZASSERT2( protocol=="tcp" || protocol=="udt", cmdexception,
                       EZINFO("only supported on tcp or udt protocol") )

            EZASSERT2( !scan.empty(), cmdexception, EZINFO("Must provide scan name") )

            // the networkspecifics. 
            if( !host.empty() )
                rte.netparms.set_host( host );

            // add the steps to the chain. 
            s0 = c.add(&rsyncinitiator, nthreadref.nParallelReader+1, rsyncinitargs(scan, networkargs(&rte, rte.netparms)));
            s1 = c.add( &parallelreader2, 4, multireadargs() );
            s2 = c.add( &parallelsender, networkargs(&rte) );

            // Cancellation functions, if any
            c.register_cancel(s0, &rsyncinit_close);

            // Configure the number of parallel readers/senders 
            c.nthread( s1, nthreadref.nParallelReader );
            c.nthread( s2, nthreadref.nParallelSender );

            // Register a finalizer which automatically clears the transfer when done 
            // we will typically have to wait for all readers + senders to
            // finish
            c.register_final(&vbs2netguard_fun, &rte);

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();
                
            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode = vbs2net;
        
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
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
    // vbs2net = nthread : [<nReader>] : [<nSender>]
    if( args[1]=="nthread" ) {
        char*             eocptr;
        const string      nRd_s( OPTARG(2, args) );
        const string      nSnd_s( OPTARG(3, args) );
        nthread_type&     nthreadref = nthread[&rte];

        // Actually, we don't care if we got arguments. If we have'm we 
        // check + use 'm otherwise it's just a no-op :D
        recognized = true;
        reply << " 0 ;";

        // first up - number of parallel readers
        if( nRd_s.empty()==false ) {
            unsigned long int nRd;

            errno = 0;
            nRd   = ::strtoul(nRd_s.c_str(), &eocptr, 0);

            // Check if it's a sensible "unsigned" value for nthread - must
            // have at least 1!
            EZASSERT2(eocptr!=nRd_s.c_str() && *eocptr=='\0' && errno!=ERANGE && nRd>0 && nRd<=UINT_MAX,
                      cmdexception,
                      EZINFO("nParallelReader '" << nRd_s << "' out of range") );

            // We've verified it ain't bigger than UINT_MAX so this cast is safe
            nthreadref.nParallelReader = (unsigned int)nRd;
        }
        // Number of parallel senders
        if( nSnd_s.empty()==false ) {
            unsigned long int nSnd;

            errno = 0;
            nSnd  = ::strtoul(nSnd_s.c_str(), &eocptr, 0);

            // Check if it's a sensible "unsigned" value for nthread - must
            // have at least 1!
            EZASSERT2(eocptr!=nSnd_s.c_str() && *eocptr=='\0' && errno!=ERANGE && nSnd>0 && nSnd<=UINT_MAX,
                      cmdexception,
                      EZINFO("nParallelSender '" << nSnd_s << "' out of range") );

            // We've verified it ain't bigger than UINT_MAX so this cast is safe
            nthreadref.nParallelSender = (unsigned int)nSnd;
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
