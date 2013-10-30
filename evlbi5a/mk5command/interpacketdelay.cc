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


string interpacketdelay_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    if( qry ) {
        reply << " 0 : ";
        if( rte.netparms.interpacketdelay<0 )
            reply << "auto : " << rte.netparms.theoretical_ipd << " usec";
        else 
            reply << rte.netparms.interpacketdelay << " usec";
        reply << " ;";
        return reply.str();
    }

    // if command, we must have an argument
    if( args.size()<2 || args[1].empty() ) {
        reply << " 8 : Command must have argument ;";
        return reply.str();
    }

    // (attempt to) parse the interpacket-delay-value
    // from the argument. No checks against the value
    // are done as all values are acceptable (<0, 0, >0)
    int   ipd;

    ASSERT_COND( (::sscanf(args[1].c_str(), "%d", &ipd)==1) );

    // great. install new value
    // Before we do that, grab the mutex, as other threads may be
    // using this value ...
    RTEEXEC(rte, rte.netparms.interpacketdelay=ipd);

    reply << " 0 ;";

    return reply.str();
}
