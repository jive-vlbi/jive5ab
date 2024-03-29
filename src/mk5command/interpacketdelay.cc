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


string interpacketdelay_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Both query and command can be issued *always*

    if( qry ) {
        int    ipd = rte.netparms.interpacketdelay_ns;

        reply << " 0 : ";

        if( ipd % 1000 )
            reply << format("%f", float(ipd)/1000.0f);
        else
            reply << ipd / 1000;

        // Append theoretical ipd if auto ipd configured
        if( rte.netparms.interpacketdelay_ns<0 ) {
            int theo = rte.netparms.theoretical_ipd_ns;

            if( theo % 1000 )
                reply << " : " << format("%f", float(theo)/1000.0f);
            else
                reply << " : " << theo / 1000;
        }
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
    char*  eocptr;
    double ipd;

    ipd = ::strtod(args[1].c_str(), &eocptr);

    // Check if it's an acceptable "ipd" value 
    // the end-of-string character may be '\0' or 'n' (for nano-seconds)
    // or 'u' for micro seconds (==default)
    const bool unit_is_us = (eocptr!=args[1].c_str() && (*eocptr=='u' || *eocptr=='\0'));

    EZASSERT2(eocptr!=args[1].c_str() && ::strchr("nu\0", *eocptr) && errno!=ERANGE &&
                     ipd>=-1.0 && ipd<(unit_is_us?INT_MAX/1000:INT_MAX),
              cmdexception,
              EZINFO("ipd '" << args[1] << "' NaN/out of range (range: [-1.0," << (unit_is_us?INT_MAX/1000:INT_MAX)
                     << (unit_is_us?"us": "ns") << "])") );
    // not specified in ns? then assume us (or it was explicit us)
    if( *eocptr!='n' )
        ipd *= 1000;

    // great. install new value
    // Before we do that, grab the mutex, as other threads may be
    // using this value ...
    RTEEXEC(rte, rte.netparms.interpacketdelay_ns=(int)ipd);

    reply << " 0 ;";

    return reply.str();
}
