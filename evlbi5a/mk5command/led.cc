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


// This one works both on Mk5B/DIM and Mk5B/DOM
// (because it looks at the h/w and does "The Right Thing (tm)"
string led_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream                reply;
    ioboard_type::iobflags_type  hw = rte.ioboard.hardware();
    ioboard_type::mk5bregpointer led0;
    ioboard_type::mk5bregpointer led1;
    
    reply << "!" << args[0] << (q?('?'):('='));

    // Both query and command may execute always

    // only check mk5b flag. it *could* be possible that
    // only the mk5b flag is set and neither of dim/dom ...
    // the ioboard.cc code should make sure that this
    // does NOT occur for best operation
    if( !(hw&ioboard_type::mk5b_flag) ) {
        reply << " 6 : This is not a Mk5B ;";
        return reply.str();
    }
    // Ok, depending on dim or dom, let the registers for led0/1
    // point at the correct location
    if( hw&ioboard_type::dim_flag ) {
        led0 = rte.ioboard[mk5breg::DIM_LED0];
        led1 = rte.ioboard[mk5breg::DIM_LED1];
    } else {
        led0 = rte.ioboard[mk5breg::DOM_LED0];
        led1 = rte.ioboard[mk5breg::DOM_LED1];
    }

    if( q ) {
        mk5breg::led_colour            l0, l1;

        l0 = (mk5breg::led_colour)*led0;
        l1 = (mk5breg::led_colour)*led1;
        reply << " 0 : " << l0 << " : " << l1 << " ;";
        return reply.str();
    }

    // for DOM we must first enable the leds?
    if( hw&ioboard_type::dom_flag )
        rte.ioboard[mk5breg::DOM_LEDENABLE] = 1;

    if( args.size()>=2 && args[1].size() ) {
        led0 = text2colour(args[1]);
    }
    if( args.size()>=3 && args[2].size() ) {
        led1 = text2colour(args[2]);
    }
    reply << " 0 ; ";
    return reply.str();
}
