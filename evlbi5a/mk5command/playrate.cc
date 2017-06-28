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
#include <ezexcept.h>
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

    if ( clock_type == "ext" ) {
        // external, just program <0 (new since 2 Nov 2015)
        opm      = outputmode_type(outputmode_type::empty);
        opm.freq = -1;
    }
    else {
        // Make sure a value was given
        const std::string   freq_s( OPTARG(2, args) );
        EZASSERT2(freq_s.size()>0, cmdexception, EZINFO("Missing clock frequency in play_rate command"));

        // Convert to rational
        // HV: 26 Jun 2017 - Due to discussion about 'int' argument to
        //                   "clock_set=..." (where the FS sends '16.000')
        //                   realized that at "play_rate=" we should
        //                   accept floats!
        //                   Short fix: multiply by 10^6 in 'characters'
        //                   first and /then/ convert to rational.
        //                   If decimal point is found, remove it, count
        //                   how many digits follow, append 6 -
        //                   #-of-fractional digits and that'll be the
        //                   string we send to the rational converter.
        //
        //                   Note: replace the characters between "." and "/"
        //                         in the input such that we can support:
        //                            0.125/3 (1/8 Mbits per 3 seconds)
        string            clock_val( freq_s + (freq_s.find('/')==string::npos ? "/1" : "") );
        string::size_type dot   = clock_val.find('.');

        // If there was a decimal point, erase it (but we remember it was there!)
        if( dot!=string::npos )
            clock_val.erase(dot, 1);

        // Now we find the slash (we've made sure there *is* one)
        string::size_type slash = clock_val.find('/');

        // Number of zeroes to insert/append. 
        // Knowing there *is* a slash in the input, makes processing a lot easier
        const string::size_type nZeroes = 6 - (dot==std::string::npos ? 0 : slash - dot);

        EZASSERT2(nZeroes<=6, cmdexception, EZINFO("There can be at most 6 fractional digits"));

        // Now insert those zeroes before the slash and we're almost done
        clock_val.insert(slash, nZeroes, '0');

        // Erase leading zeroes (0.125/1 => 0125000/1 => 125000/1)
        const string::size_type nonZero = clock_val.find_first_not_of('0');
        if( nonZero!=0 )
            clock_val.erase(clock_val.begin(), clock_val.begin()+nonZero);

        // NOW we can (attempt to) convert it to rational
        istringstream   iss( clock_val );
        iss >> opm.freq;
        // Go to Hz this is not necessary anymore - we've done that in the string-processing bit
        //opm.freq *= 1000000;
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
