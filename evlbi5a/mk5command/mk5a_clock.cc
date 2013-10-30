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
#include <mk5command/in2netsupport.h>
#include <iostream>

using namespace std;


string mk5a_clock_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream               reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing record - we shouldn't be here!
    if( qry ) {
        reply << " 0 : " << !(rte.ioboard[ mk5areg::notClock ]) << " ;";
        return reply.str();
    }

    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    //rte.ioboard[ mk5areg::notClock ] = (args[1]=="off");
    if( args[1]=="on" ) {
        in2net_transfer<mark5a>::start(rte);
        reply << " 0 ; ";
    } else if (args[1]=="off" ) {
        in2net_transfer<mark5a>::stop(rte);
        reply << " 0 ; ";
    } else  {
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }

    return reply.str();
}
