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
#include <interchainfns.h>
#include <iostream>

using namespace std;


struct dupvars {
    bool           expand;
    unsigned int   factor;
    unsigned int   sz;

    dupvars() :
        expand(false), factor(0), sz(0)
    {}
};

string net2mem_fn(bool qry, const vector<string>& args, runtime& rte ) {
    static per_runtime<string>   host;
    static per_runtime<dupvars>  arecibo;
    
    const transfer_type rtm( string2transfermode(args[0]) );
    const transfer_type ctm( rte.transfermode );

    ostringstream       reply;
    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if ( qry ) {
        const string areciboarg = OPTARG(1, args);
        if( ::strcasecmp(areciboarg.c_str(), "ar")==0 ) {
            per_runtime<dupvars>::const_iterator dvptr = arecibo.find(&rte);

            if( dvptr!=arecibo.end() ) {
                const dupvars& dv( dvptr->second );

                reply << " 0 : " << (dv.expand?"true":"false") << " : "
                      << dv.sz << "bit" << " : x" << dv.factor << " ;";
            } else {
                reply << " 0 : false : not configured in this runtime ;";
            }
            return reply.str();
        }
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
            reply << "6 : cannot start " << args[0] << " while doing " << ctm << " ;";
            return reply.str();
        }

        // constraint the expected sizes
        const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                           (unsigned int)rte.trackbitrate(),
                                           rte.vdifframesize());
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

        // Start building the chain
        // clear lasthost so it won't bother the "getsok()" which
        // will, when the net_server is created, use the values in
        // netparms to decide what to do.
        // Also register cancellationfunctions that will close the
        // network and file filedescriptors and notify the threads
        // that it has done so - the threads pick up this signal and
        // terminate in a controlled fashion
        host[&rte] = rte.netparms.host;

        rte.netparms.host.clear();
        
        chain c;

        c.register_cancel( c.add(&netreader, 10, &net_server, networkargs(&rte)),
                           &close_filedescriptor);

        // Insert a decompressor if needed
        if( rte.solution ) {
            c.add(&blockdecompressor, 10, &rte);
        }

        if( arecibo.find(&rte)!=arecibo.end() ) {
            const dupvars& dvref = arecibo[&rte];
            if( dvref.expand ) {
                DEBUG(0, "Adding Arecibo duplication step" << endl);
                c.add(&duplicatorstep, 10, duplicatorargs(&rte, dvref.sz, dvref.factor));
            }
        }

        // And write to mem
        c.add( queue_writer, queue_writer_args(&rte) );

        // reset statistics counters
        rte.statistics.clear();
        rte.transfersubmode.clr_all();

        rte.transfermode = rtm;
        rte.processingchain = c;
        rte.processingchain.run();

        reply << "0 ;";

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

        // put back original host
        rte.netparms.host = host[&rte];
    }
    else if( ::strcasecmp(args[1].c_str(), "ar")==0 ) {
        // Command is:
        //   net2mem = ar : <sz> : <factor>
        // Will duplicate <sz> bits times <factor>
        //    net2mem = ar : off
        // Will turn this hack off
        char*         eocptr;
        dupvars&      dv = arecibo[&rte];
        const string  szstr( OPTARG(2, args) );
        const string  factorstr( OPTARG(3, args) );

        if( ctm!=no_transfer ) {
            reply << "6 : cannot change " << args[0] << " while doing " << ctm << " ;";
            return reply.str();
        }

        if( szstr=="off" ) {
            dv.expand = false;
        } else {
            // Both arguments must be given
            ASSERT2_COND(szstr.empty()==false, SCINFO("Specify the input itemsize (in units of bits)"));
            ASSERT2_COND(factorstr.empty()==false, SCINFO("Specify a duplication factor"));

            errno = 0;
            dv.sz = (unsigned int)::strtoul(szstr.c_str(), &eocptr, 0);
            ASSERT2_COND(eocptr!=szstr.c_str() && *eocptr=='\0' && errno!=ERANGE && errno!=EINVAL, 
                         SCINFO("Failed to parse the itemsize as a number"));
            errno     = 0;
            dv.factor = (unsigned int)::strtoul(factorstr.c_str(), &eocptr, 0);
            ASSERT2_COND(eocptr!=factorstr.c_str() && *eocptr=='\0' && errno!=ERANGE && errno!=EINVAL, 
                         SCINFO("Failed to parse the factor as a number"));
            dv.expand = true;
        }
        reply << " 0 ;";
    }
    else {
        reply << "2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }

    return reply.str();
}
