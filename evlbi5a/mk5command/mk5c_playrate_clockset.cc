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

using namespace std;


// Equivalents of playrate / clock_set but for generic PCs /Mark5C
// (which don't have an actual ioboard installed).
// But sometimes you must be able to specify the trackbitrate.
string mk5c_playrate_clockset_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream              reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Query is possible always, command only when doing nothing at all
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer))

    if( qry ) {
        const double rate = rte.trackbitrate()/1.0e6;
        reply << "0 : " << rate << " : " << rate << " : " << rate << " ;";
        return reply.str();
    }

    // if command, we require 'n argument
    // for now, we discard the first argument but just look at the frequency
    if( args.size()<3 ) {
        reply << "8 : not enough arguments to command ;";
        return reply.str();
    }

    // Depending on wether it was "play_rate = "
    // or "clock_set = " we do Stuff (tm)

    // play_rate = <ignored_for_now> : <freq>
    if( args[0]=="play_rate" ) {
        double       requested_frequency;
        const string frequency_arg( OPTARG(2, args) );

        ASSERT_COND(frequency_arg.empty()==false);
            
        requested_frequency = ::strtod(frequency_arg.c_str(), 0);
        ASSERT_COND(requested_frequency>0.0 && requested_frequency<=64.0);
        rte.set_trackbitrate( requested_frequency*1.0e6 );
        DEBUG(2, "play_rate[mk5c]: Setting clockfreq to " << rte.trackbitrate() << endl);
        reply << " 0 ;";
    } else if( args[0]=="clock_set" ) {
        const string clocksource( OPTARG(2, args) );
        const string frequency_arg( OPTARG(1, args) );

        // Verify we recognize the clock-source
        ASSERT2_COND( clocksource=="int"||clocksource=="ext",
                      SCINFO(" clock-source '" << clocksource << "' unknown, use int or ext") );
        ASSERT_COND(frequency_arg.empty()==false);

        // If there is a frequency given, inspect it and transform it
        // to a 'k' value [and see if that _can_ be done!]
        int      k;
        string   warning;
        double   f_req, f_closest;

        f_req     = ::strtod(frequency_arg.c_str(), 0);
        ASSERT_COND( (f_req>=0.0) );

        // can only do 2,4,8,16,32,64 MHz
        // cf IOBoard.c:
        // (0.5 - 1.0 = -0.5; the 0.5 gives roundoff)
        //k         = (int)(::log(f_req)/M_LN2 - 0.5);
        // HV's own rendition:
        k         = (int)::round( ::log(f_req)/M_LN2 ) - 1;
        f_closest = ::exp((k + 1) * M_LN2);
        // Check if in range [0<= k <= 5] AND
        // requested f close to what we can support
        ASSERT2_COND( (k>=0 && k<=5),
                      SCINFO(" Requested frequency " << f_req << " <2 or >64 is not allowed") );
        ASSERT2_COND( (::fabs(f_closest - f_req)<0.01),
                      SCINFO(" Requested frequency " << f_req << " is not a power of 2") );

        // Now it's safe to set the actual frequency
        // HV: 06 jan 2014 - this double multiplication yields
        //                   15999999.999999998137 if you input
        //                   "clock_set=16". It will DISPLAY
        //                   as "16000000.000000"(*) but "(unsigned int)..."
        //                   will actually yield 15999999 ....
        //                   Setting a *slightly* incorrect trackbitrate
        //                   which will yield a *slightly* wrong framerate
        //                   in the header searching/time stamp decoding,
        //                   making the decoding fail on the boundary
        //                   condition - the last frame number of a second is
        //                   considered to be invalid. 
        //
        //                   In order to fix this we do not use f_closest
        //                   but "(1<<(k+1)) * 1.0e6" because the freq is
        //                   2 ** (k+1)
        //
        //                   (*) until you tell it to print > 9 decimal
        //                   places
        rte.set_trackbitrate( (1 << (k+1)) * (double)1.0e6 );
        DEBUG(2, "clock_set[mk5c]: Setting clockfreq to " << format("%.5lf", rte.trackbitrate()) << " [" << (unsigned int)rte.trackbitrate() << "]" << endl);
        reply << " 0 ;";
    } else {
        ASSERT2_COND(false, SCINFO("command is neither play_rate nor clock_set"));
    }
    return reply.str();
}
