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


// Mark5A(+) playrate function
string playrate_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    outputmode_type opm;
    rte.get_output( opm );

    if( qry ) {
        double          clkfreq, clkgen;
        clkfreq  = opm.freq;
        clkfreq *= 9.0/8.0;

        clkgen = clkfreq;
        if ( opm.submode == "64" ) {
            clkgen *= 2;
        }

        // need implementation of table
        // listed in Mark5ACommands.pdf under "playrate" command
        reply << "0 : " << opm.freq << " : " << clkfreq << " : " << clkgen << " ;";
        return reply.str();
    }

    // if command, we require 'n argument
    if( (args.size()<2) || ((args[1] != "ext") && (args.size() < 3)) ) {
        reply << "8 : not enough arguments to command ;";
        return reply.str();
    }

    if ( args[1] == "ext" ) {
        // external, just program 0
        opm.freq = 0;
    }
    else {
        opm.freq = ::strtod(args[2].c_str(), 0);
        if ( (args[1] == "clock") || (args[1] == "clockgen") ) {
            // need to strip the 9/8 parity bit multiplier
            opm.freq /= 9.0/8.0;
            if ( (opm.mode == "vlba") || 
                 ((opm.mode == "st") && (opm.submode == "vlba")) ) {
                // and strip the the vlba header
                opm.freq /= 1.008;
            }
            if ( (args[1] == "clockgen") && (opm.submode == "64") ) {
                // and strip the frequency doubling
                opm.freq /= 2;
            }
        }
        else if ( args[1] != "data" ) {
            reply << " 8 : reference must be data, clock, clockgen or ext ;";
            return reply.str();
        }
    }
                  
    
    DEBUG(2, "Setting clockfreq to " << opm.freq << endl);
    rte.set_output( opm );
        
    // indicate success
    reply << " 0 ;";
    return reply.str();
}
