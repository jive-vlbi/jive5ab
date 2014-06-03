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
    ostringstream          reply;
    mk5bdom_inputmode_type curipm( mk5bdom_inputmode_type::empty );
    mk5bdom_inputmode_type ipm( mk5bdom_inputmode_type::empty );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Query is possible always, command only when doing nothing at all
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer))

    // Get a copy of the current input mode 
    rte.get_input( curipm );

    if( qry ) {
        double rate = curipm.clockfreq /*rte.trackbitrate()*/;
    
        // We detect 'magic mode' by looking at the
        // second parameter "ntrack" of the input mode.
        // If that one is empty - we have a one-valued
        // mode, which is, by definition, the 'magic mode'.
        // All valid Mark5* modes have TWO values
        //  "mark4:64", "tvg+3:0xff", "ext:0xff" &cet
        if( curipm.ntrack.empty() )
            reply << "0 : " << format("%.3lf", rate*1.0e6) << " ;";
        else {
            //rate /= 1000000;
            if( rte.trackformat()==fmt_mark5b )
                reply << "0: " << rate << " : int : " << rate << " ;";
            else
                reply << "0 : " << (unsigned int)rate << " : " << (unsigned int)rate << " : " << (unsigned int)rate << " ;";
        }
        return reply.str();
    }

    // if command, we require 'n argument
    // for now, we discard the first argument but just look at the frequency
    if( args.size()<3 ) {
        reply << "8 : not enough arguments to command ;";
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
        const string frequency_arg( OPTARG(2, args) );

        ASSERT_COND(frequency_arg.empty()==false);
        
        ipm.clockfreq = ::strtod(frequency_arg.c_str(), 0);

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

        // If there is a frequency given, inspect it and transform it
        // to a 'k' value [and see if that _can_ be done!]
        int      k;
        string   warning;
        double   f_req, f_closest;

        f_req     = ::strtod(frequency_arg.c_str(), 0);
        EZASSERT( f_req>=0.0, cmdexception );

        // can only do 2,4,8,16,32,64 MHz
        // cf IOBoard.c:
        // (0.5 - 1.0 = -0.5; the 0.5 gives roundoff)
        //k         = (int)(::log(f_req)/M_LN2 - 0.5);
        // HV's own rendition:
        k         = (int)::round( ::log(f_req)/M_LN2 ) - 1;
        f_closest = ::exp((k + 1) * M_LN2);
        // Check if in range [0<= k <= 5] AND
        // requested f close to what we can support
        EZASSERT2( (k>=0 && k<=5), cmdexception,
                   EZINFO(" Requested frequency " << f_req << " <2 or >64 is not allowed") );
        EZASSERT2( (::fabs(f_closest - f_req)<0.01), cmdexception,
                   EZINFO(" Requested frequency " << f_req << " is not a power of 2") );

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
        ipm.clockfreq = f_closest;

        // Send to 'hardware'
        rte.set_input( ipm );
        //rte.set_trackbitrate( (1 << (k+1-decimation)) * (double)1.0e6 );
        DEBUG(2, "clock_set[mk5c]: Setting clockfreq to " << format("%.5lf", rte.trackbitrate()) << " [" << (unsigned int)rte.trackbitrate() << "]" << endl);
        reply << " 0 ;";
    } else {
        EZASSERT2(false, cmdexception, EZINFO("command is neither play_rate nor clock_set"));
    }
    return reply.str();
}

