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
#include <signal.h>
#include <iostream>

using namespace std;


// struct to communicate between the trackmask_fn & the trackmask computing
// thread
struct computeargs_type {
    data_type     trackmask;
    // write solution in here
    runtime*      rteptr;

    computeargs_type() :
        trackmask( trackmask_empty ), rteptr( 0 )
    {}
};
void* computefun(void* p) {
    computeargs_type*  computeargs = (computeargs_type*)p;

    DEBUG(0, "computefun: start computing solution for " << hex_t(computeargs->trackmask) << endl);
    computeargs->rteptr->solution = solve(computeargs->trackmask);
    DEBUG(0, "computefun: done computing solution for " << hex_t(computeargs->trackmask) << endl);
    DEBUG(0, computeargs->rteptr->solution << endl);
    return (void*)0;
}

string trackmask_fn(bool q, const vector<string>& args, runtime& rte) {
    // computing the trackmask may take a considerable amount of time
    // so we do it in a thread. As long as the thread is computing we
    // report our status as "1" ("action initiated or enabled but not
    // completed" as per Mark5 A/B Commandset v 1.12)
    static per_runtime<pthread_t*>       runtime_computer;
    static per_runtime<computeargs_type> runtime_computeargs;

    if ( runtime_computer.find(&rte) == runtime_computer.end() ) {
        runtime_computer[&rte] = NULL;
    }
    pthread_t*& computer = runtime_computer[&rte];
    computeargs_type& computeargs = runtime_computeargs[&rte];

    // automatic variables
    char*           eocptr;
    const bool      busy( computer!=0 && ::pthread_kill(*computer, 0)==0 );
    ostringstream   reply;

    // before we do anything, update our bookkeeping.
    // if we're not busy (anymore) we should update ourselves to accept
    // further incoming commands.
    if( !busy ) {
        delete computer;
        computer = 0;
    }

    // now start forming the reply
    reply << "!" << args[0] << (q?('?'):('='));

    if( busy ) {
        reply << " 5 : still computing compressionsteps ;";
        return reply.str();
    }

    // good, check if query
    if( q ) {
        reply << " 0 : " << hex_t(computeargs.trackmask) << " : " << rte.signmagdistance << " ;";
        return reply.str();
    }
    // must be command then. we do not allow the command when doing a
    // transfer
    if( rte.transfermode!=no_transfer ) {
        reply << " 6 : cannot set trackmask whilst transfer in progress ;";
        return reply.str();
    }
    // we require at least the trackmask
    if( args.size()<2 || args[1].empty() ) {
        reply << " 8 : Command needs argument! ;";
        return reply.str();
    }
    errno                 = 0;
    computeargs.trackmask = ::strtoull(args[1].c_str(), &eocptr, 0);
    // !(A || B) => !A && !B
    ASSERT2_COND( !(computeargs.trackmask==0 && eocptr==args[1].c_str()) && !(computeargs.trackmask==~((uint64_t)0) && errno==ERANGE),
                  SCINFO("Failed to parse trackmask") );
                 
    // The sign magnitude distance is optional, default value 0
    // which means no magnitude restoration effort is made
    rte.signmagdistance = 0;
    if( args.size()>2 ) {
        ASSERT2_COND( ::sscanf(args[2].c_str(), "%d", &rte.signmagdistance) == 1,
                      SCINFO("Failed to parse sign-magnitude distance") );
    }

    // no tracks are dropped
    if( computeargs.trackmask==((uint64_t)0xffffffff << 32) + 0xffffffff ) 
        computeargs.trackmask=0;

    // Right - if no trackmask, clear it also from the runtime environment.
    // If yes trackmask, start a thread to compute the solution
    if( computeargs.trackmask ) {
        computer           = new pthread_t;
        computeargs.rteptr = &rte;

        // attempt to start the thread. if #fail then clean up
        PTHREAD2_CALL( ::pthread_create(computer, 0, computefun, &computeargs),
                       delete computer; computer = 0 );
        reply << " 1 : start computing compression steps ;";
    } else {
        rte.solution = solution_type();
        reply << " 0 : " << hex_t(computeargs.trackmask) << " : " << rte.signmagdistance << " ;";
    }
    return reply.str();
}
