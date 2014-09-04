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
#include <headersearch.h>
#include <inttypes.h>     // For SCNu64 and friends
#include <limits.h>
//#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
//   #include <ifaddrs.h>
//   #include <stdio.h>
//   #include <stdlib.h>
//   #include <unistd.h>
#include <map>


#include <iostream>

using namespace std;



// Get interface => IP address mapping
//   Note: we only work with IPv4!!
typedef map<string, string>  if2addr_type;

if2addr_type mk_if2addr( void );

static if2addr_type if2addr = mk_if2addr();




void net2vbsguard_fn(runtime* rteptr) {
    try {
        DEBUG(3, "net2vbs guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "net2vbs finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "net2vbs finalization threw an unknown exception" << std::endl );        
    }
    rteptr->transfermode = no_transfer;
}

// Per runtime we keep the settings of how many parallel network readers
// (only with "net2vbs", not used when used as "record") + how many
// parallel file writers are started.
// The default c'tor assumes 1 each - the absolute minimum
struct nthread_type {
    unsigned int    nParallelReader;
    unsigned int    nParallelWriter;

    nthread_type() :
        nParallelReader( 1 ), nParallelWriter( 1 )
    {}
};

// Support net2vbs
string net2vbs_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream                    reply;
    const transfer_type              ctm( rte.transfermode ); // current transfer mode
    static per_runtime<nthread_type> nthread;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is *always* possible, command will register 'busy'
    // if not doing nothing or the requested transfer mode 
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==net2vbs))

    // Good. See what the usr wants
    if( qry ) {
        // may query 'nthread' rather than vbs2net status
        //    vbs2net?         => vbs2net status
        //    vbs2net? nthread => query how many threads configured
        const string    what( OPTARG(1, args) );

        // Queries always work
        reply << " 0 : ";

        if( what=="nthread" ) {
            reply << nthread[&rte].nParallelReader << " : " << nthread[&rte].nParallelWriter;
        } else {
            if( ctm!=net2vbs ) {
                reply << "inactive";
            } else {
                string status = "waiting";

                // we ARE running so we must be able to retrieve the lasthost
                reply << "active"
                      //<< " : " << rte.netparms.host
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


    // net2vbs = open [no options yet]
    // record  = on : <scan name>
    if( (args[0]=="net2vbs" && args[1]=="open") ||
        (args[0]=="record"  && args[1]=="on") ) {
        recognized = true;
        // if transfermode is already disk2net, we ARE already connected
        if( rte.transfermode==no_transfer ) {
            // build up a new instance of the chain
            chain                   c;
            const bool              rsync = (args[0]=="net2vbs");
            const string            protocol( rte.netparms.get_protocol() );
            const string            scanname( OPTARG(2, args) );
            chain::stepid           s1, s2;
            const nthread_type&     nthreadref = nthread[&rte];

            // At the moment we can only do rsync over tcp or udt 
            if( rsync  ) {
                EZASSERT2( protocol=="tcp" || protocol=="udt", cmdexception,
                           EZINFO("only supported on tcp or udt protocol") )
            } else {
                EZASSERT2( !scanname.empty(), cmdexception,
                           EZINFO("must provide a scan name") )
                // Also, when doing recording, we must constrain our 
                // block size and other packet parameters
                const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                                   (unsigned int)rte.trackbitrate(),
                                                   rte.vdifframesize());

                EZASSERT2( dataformat.valid(), cmdexception,
                           EZINFO("can only record a known data format") );

                // on the flexbuff we must always constrain our blocks to an integral number of frames
                rte.sizes = constrain(rte.netparms, dataformat, rte.solution, constraints::BYFRAMESIZE);
            }

            // add the steps to the chain. 
            if( rsync ) {
                // rsync version
                s1 = c.add( &parallelnetreader, 4, &mk_server, &rte, rte.netparms );
                c.register_cancel(s1, &mna_close);
                // Five parallel readers
                c.nthread( s1, nthreadref.nParallelReader );
            } else {
                // just suck the network card empty, allowing for partial
                // blocks
                c.register_cancel( c.add(&netreader, 8, &net_server, networkargs(&rte, true)),
                                   &close_filedescriptor);
                // Must add a step which transforms block => chunk_type,
                // i.e. count the chunks and generate filenames
                c.add( &chunkmaker, 2,  scanname );
            }

            // Eight writers in parallel
            s2 = c.add( &parallelwriter, &get_mountpoints, &rte );
            c.register_cancel(s2, &mfa_close);
            c.nthread( s2, nthreadref.nParallelWriter );

            // register the finalization function
            c.register_final( &net2vbsguard_fn, &rte );

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();
                
            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode = net2vbs;
        
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }
    // net2vbs = close
    // record  = off
    if( (args[0]=="net2vbs" && args[1]=="close") ||
        (args[0]=="record" && args[1]=="off") ) {
            recognized = true;
            // Only allow if we're doing net2vbs
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
    // net2vbs = nthread : [<nReader>] : [<nWriter>]
    if( args[1]=="nthread" ) {
        char*             eocptr;
        const string      nRd_s( OPTARG(2, args) );
        const string      nWrt_s( OPTARG(3, args) );
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
        // Number of parallel writers
        if( nWrt_s.empty()==false ) {
            unsigned long int nWrt;

            errno = 0;
            nWrt  = ::strtoul(nWrt_s.c_str(), &eocptr, 0);

            // Check if it's a sensible "unsigned" value for nthread - must
            // have at least 1!
            EZASSERT2(eocptr!=nWrt_s.c_str() && *eocptr=='\0' && errno!=ERANGE && nWrt>0 && nWrt<=UINT_MAX,
                      cmdexception,
                      EZINFO("nParallelSender '" << nWrt << "' out of range") );

            // We've verified it ain't bigger than UINT_MAX so this cast is safe
            nthreadref.nParallelWriter = (unsigned int)nWrt;
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}


///////////////// helper stuff

// Adapted from http://man7.org/linux/man-pages/man3/getifaddrs.3.html

if2addr_type mk_if2addr( void ) {
    if2addr_type    rv;
#if 0
    struct ifaddrs* ifaddr;
    // How fatal do we want this to be?
    //ASSERT_ZERO( ::getifaddrs(&ifaddr) );
    
    if( ::getifaddrs(&ifaddr)==-1 ) {
        DEBUG(-1, "mk_if2addr: failed to get ifaddrs - cannot use interface names with record=on" << endl);
        return rv;
    }

    // Walk through linked list, maintaining head pointer so we
    // can free list later
    for( struct ifaddrs* ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next ) {
        int    r;
        char   host[NI_MAXHOST];

        // We only do non-null IPv4 addresses ...
        if( ifa->ifa_addr==NULL || ifa->ifa_addr->sa_family!=AF_INET )
            continue;

        r = ::getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), 
                          host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if( r!=0 ) {
            DEBUG(-1, "mk_if2addr: failed to get nameinfo for " << ifa->ifa_name << " - " 
                      << ::gai_strerror(r) << endl);
            continue;
        }
        rv[ ifa->ifa_name ] = host;
    }
    ::freeifaddrs( ifaddr );
#else
    // http://stackoverflow.com/questions/4937529/polling-interface-names-via-siocgifconf-in-linux
    DEBUG(0, "mk_if2addr/not implemented yet! Need SIOCGIFCONF implementation!" << endl);
#endif
    return rv;
}
