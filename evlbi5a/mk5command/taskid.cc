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
#include <iostream>

using namespace std;


// set/query the taskid
string task_id_fn(bool qry, const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        const unsigned int tid = rte.current_taskid;

        reply << " 0 : ";
        if( tid==runtime::invalid_taskid )
            reply << "none";
        else 
            reply << tid;
        reply << " ;";
        return reply.str();
    }

    // check if argument given and if we're not doing anything
    if( args.size()<2 ) {
        reply << " 8 : no taskid given ;";
        return reply.str();
    }

    if( rte.transfermode!=no_transfer ) {
        reply << " 6 : cannot set/change taskid during " << rte.transfermode << " ;";
        return reply.str();
    }

    // Gr8! now we can set the actual taskid
    if( args[1]=="none" )
        rte.current_taskid = runtime::invalid_taskid;
    else
        rte.current_taskid = (unsigned int)::strtol(args[1].c_str(), 0, 0);
    reply << " 0 ;";

    return reply.str();
}
