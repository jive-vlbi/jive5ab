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
#include <streamutil.h>     // for printf-like formatting on C++ streams
#include <iostream>
#include <limits.h>

using namespace std;


string ackperiod_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Both query and command can be issued *always*

    if( qry ) {
        reply << " 0 : " << rte.netparms.ackPeriod << " ;";
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
    char*     eocptr;
    long int  ack;
    const int a_min = std::numeric_limits<int>::min(), a_max = std::numeric_limits<int>::max();

    ack = ::strtol(args[1].c_str(), &eocptr, 0);

    // Check if it's an acceptable "ack" value 
    EZASSERT2(eocptr!=args[1].c_str() && *eocptr=='\0' && errno!=ERANGE &&
              (ack>=a_min) && (ack<=a_max),
              cmdexception,
              EZINFO("ack '" << args[1] << "' not a number/out of range (range: " << a_min << "-" << a_max << ")"));

    // great. install new value
    // Before we do that, grab the mutex, as other threads may be
    // using this value ...
    RTEEXEC(rte, rte.netparms.set_ack(ack));

    reply << " 0 ;";

    return reply.str();
}
