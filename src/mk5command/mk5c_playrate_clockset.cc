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
#include <cmath>

using namespace std;


// Equivalents of playrate / clock_set but for generic PCs /Mark5C
// (which don't have an actual ioboard installed).
// But sometimes you must be able to specify the trackbitrate.
string mk5c_playrate_clockset_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream          reply;
    mk5bdom_inputmode_type curipm( mk5bdom_inputmode_type::empty );
    mk5bdom_inputmode_type ipm( mk5bdom_inputmode_type::empty );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Query is possible always, command only when doing nothing at all
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer))

    // Get a copy of the current input mode 
    rte.get_input( curipm );

    if( qry ) {
        samplerate_type  rate = curipm.clockfreq/1000000;
    
        // We detect 'magic mode' by looking at the
        // second parameter "ntrack" of the input mode.
        // If that one is empty - we have a one-valued
        // mode, which is, by definition, the 'magic mode'.
        // All valid Mark5* modes have TWO values
        //  "mark4:64", "tvg+3:0xff", "ext:0xff" &cet
        if( curipm.ntrack.empty() )
            reply << "0 : " << format("%.3lf", boost::rational_cast<double>(rate)) << " ;";
        else {
            if( rte.trackformat()==fmt_mark5b )
                reply << "0: " << rate << " : int : " << rate << " ;";
            else
                reply << "0 : " << rate << " : " << rate << " : " << rate << " ;";
        }
        return reply.str();
    }

    // If we are in 'magic mode' [mode was set using single string
    // "MKIV1_4-1024-16-2" &cet] do not accept "play_rate" or "clock_set"
    // commands. The system must first be programmed using a 'real'
    // hardware mode ["mark4:64", "ext:0xff" etc]
    if( curipm.mode.empty()==false && curipm.ntrack.empty()==true ) {
        reply << "6 : system in 'magic mode', to put the system back send appropriate Mark5A or Mark5B hardware mode command first ;";
        return reply.str();
    }

    // Depending on wether it was "play_rate = "
    // or "clock_set = " we do Stuff (tm)


    // play_rate = <ignored_for_now> : <freq>
    if( args[0]=="play_rate" ) {
        const string  clock_type( OPTARG(1, args) );

        EZASSERT2(clock_type.empty()==false, cmdexception, EZINFO("play_rate command requires a clock reference argument"));

        if ( clock_type == "ext" ) {
            // external, just program <0 (new since 2 Nov 2015)
            ipm.clockfreq = -1;
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

            // Convert to rational
            istringstream   iss( clock_val + (clock_val.find('/')==string::npos ? "/1" : "") );

            iss >> ipm.clockfreq;
            // Go to MHz 28 Jun 2017: No! Now already done in string processing
            //ipm.clockfreq *= 1000000;
            if ( (clock_type == "clock") || (clock_type == "clockgen") ) {
                // need to strip the 9/8 parity bit multiplier
                // (divide by 9/8 = multiply by 8/9)
                ipm.clockfreq *= samplerate_type(8, 9);
                if ( (curipm.mode == "vlba") || 
                     ((curipm.mode == "st") && (curipm.ntrack == "vlba")) ) {
                    // and strip the the vlba header
                    // which is 20 bytes over 2500 = 2/250 = 1/125
                    ipm.clockfreq /= samplerate_type(126, 125)/*1.008*/;
                }
                if ( (clock_type == "clockgen") && (curipm.ntrack == "64") ) {
                    // and strip the frequency doubling
                    ipm.clockfreq /= 2;
                }
            }
            else if ( clock_type != "data" ) {
                reply << " 8 : reference must be data, clock, clockgen or ext ;";
                return reply.str();
            }
        }
        // Send new play rate to 'hardware'
        rte.set_input( ipm );
        DEBUG(2, "play_rate[mk5c]: Setting clockfreq to " << rte.trackbitrate() << endl);
        reply << " 0 ;";
    } else if( args[0]=="clock_set" ) {
        const string clocksource( OPTARG(2, args) );
        const string frequency_arg( OPTARG(1, args) );
    
        // Verify we recognize the clock-source
        EZASSERT2( clocksource=="int"||clocksource=="ext", cmdexception,
                   EZINFO(" clock-source '" << clocksource << "' unknown, use int or ext") );
        EZASSERT(frequency_arg.empty()==false, cmdexception);

        istringstream iss( frequency_arg + (frequency_arg.find('/')==string::npos ? "/1" : "") );

        iss >> ipm.clockfreq;
        // Go to MHz
        ipm.clockfreq *= 1000000;

        // Send to 'hardware'
        rte.set_input( ipm );
        DEBUG(2, "clock_set[mk5c]: Setting clockfreq to " << rte.trackbitrate() << endl);
        reply << " 0 ;";
    } else {
        EZASSERT2(false, cmdexception, EZINFO("command is neither play_rate nor clock_set"));
    }
    return reply.str();
}

