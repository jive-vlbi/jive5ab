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
#include <streamutil.h>
#include <iostream>
#include <sstream>

using namespace std;


// Mark5A(+) playrate function
string playrate_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Query can be executed always, command only when the i/o board
    // is available [i.e. we register "busy" if the i/o board is in use]
    INPROGRESS(rte, reply, !qry && (fromio(ctm) || toio(ctm)))

    outputmode_type opm;
    rte.get_output( opm );

    if( qry ) {
        // If we're in 'magic mode' (mode+trackbitrate set from one
        // string - Walter Brisken format, e.g. "VDIFL_5000-512-8-1")
        // we produce slightly different output
        samplerate_type         clkfreq = rte.trackbitrate()/1000000, clkgen;
        mk5bdom_inputmode_type  magicmode( mk5bdom_inputmode_type::empty );

        rte.get_input( magicmode );
        if( magicmode.mode.empty()==false ) {
            reply << "0 : " << format("%.3lf", boost::rational_cast<double>(clkfreq)) << " ;";
            return reply.str();
        }

        // No magic mode, carry on doing our hardware specific processing

        clkfreq  = opm.freq;
        clkfreq *= samplerate_type(9, 8);

        clkgen = clkfreq;
        if ( opm.submode == "64" ) {
            clkgen *= 2;
        }

        // need implementation of table
        // listed in Mark5ACommands.pdf under "playrate" command
        reply << "0 : " << opm.freq/1000000 << " : " << clkfreq/1000000 << " : " << clkgen/1000000 << " ;";
        return reply.str();
    }

    // if command, we require 'n argument
    if( (args.size()<2) || ((args[1] != "ext") && (args.size() < 3)) ) {
        reply << "8 : not enough arguments to command ;";
        return reply.str();
    }

    const string  clock_type( args[1] );
    const string  clock_val( OPTARG(2, args) );

    if ( clock_type == "ext" ) {
        // external, just program <0 (new since 2 Nov 2015)
        opm      = outputmode_type(outputmode_type::empty);
        opm.freq = -1;
    }
    else {
        // Convert to rational
        istringstream   iss( clock_val + (clock_val.find('/')==string::npos ? "/1" : "") );
        iss >> opm.freq;
        // Go to Hz
        opm.freq *= 1000000;
        if ( (clock_type == "clock") || (clock_type == "clockgen") ) {
            // need to strip the 9/8 parity bit multiplier
            // (divide by 9/8 = multiply by 8/9)
            opm.freq *= samplerate_type(8, 9);
            if ( (opm.mode == "vlba") || 
                 ((opm.mode == "st") && (opm.submode == "vlba")) ) {
                // and strip the the vlba header
                // which is 20 bytes over 2500 = 2/250 = 1/125
                opm.freq /= samplerate_type(126, 125)/*1.008*/;
            }
            if ( (clock_type == "clockgen") && (opm.submode == "64") ) {
                // and strip the frequency doubling
                opm.freq /= 2;
            }
        }
        else if ( clock_type != "data" ) {
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
