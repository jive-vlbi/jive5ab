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


string position_fn(bool q, const vector<string>& args, runtime& rte) {
    // will return depending on actual query:
    // pointers: <record pointer> : <scan start> : <scan end> ; scan start and end are filled with "-" for now
    // position: <record pointer> : <play pointer>
    ostringstream              reply;

    reply << "!" << args[0] << (q?("? "):("= "));
  
    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    } 

    // This query can only not be done during a bank switch;
    // it is especially used during conditioning
    INPROGRESS(rte, reply,  rte.transfermode==bankswitch)

    // Now we can safely proceed
    reply << "0 : " << ::XLRGetLength(rte.xlrdev.sshandle()) << " : ";

    if (args[0] == "position") {
        reply << rte.pp_current.Addr + (rte.transfermode == disk2out ? ::XLRGetPlayLength(rte.xlrdev.sshandle()) : 0);
    }
    else if (args[0] == "pointers") {
        reply << rte.pp_current << " : " << rte.pp_end;
    }
    else {
        THROW_EZEXCEPT(cmdexception, "query '" + args[0] + "' not recognized in position_fn");
    }
    reply << " ;";

    return reply.str();
}
