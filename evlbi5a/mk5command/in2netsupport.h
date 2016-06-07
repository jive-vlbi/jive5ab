// obviously, these numbers are chosen completely at random ...
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
#ifndef JIVE5AB_IN2NETSUPPORT_H
#define JIVE5AB_IN2NETSUPPORT_H

#include <mk5_exception.h>
#include <xlrdefines.h>
#include <xlrdevice.h>
#include <mk5command/mk5.h>

enum hwtype { mark5a = 666, mark5b = 42, mark5c = 37 };

// Some support functionality for the templated functions
CHANNELTYPE inputchannel(hwtype hw);

template <unsigned int Blah>
struct in2netbase {
    static CHANNELTYPE inputchannel( void ) {
        return ::inputchannel((hwtype)Blah);
    }
};


// Abstract out the phases of an in2{net|fork} command into setup, start,
// pause, resume and stop. If you instantiate an actual "in2net_transfer"
// for a type which is not specialized below (as it will be for "mark5a" and
// "mark5b" (see the enum just above this text)) you will have exceptions
// thrown when trying to access any of them.
template <unsigned int _Blah>
struct in2net_transfer: public in2netbase<_Blah> {
    static void setup(runtime&) {
        throw cmdexception("in2net_transfer::setup not defined for this hardware!");
    }
    static void start(runtime&) {
        throw cmdexception("in2net_transfer::start not defined for this hardware!");
    }
    static void pause(runtime&) {
        throw cmdexception("in2net_transfer::pause not defined for this hardware!");
    }
    static void resume(runtime&) {
        throw cmdexception("in2net_transfer::resume not defined for this hardware!");
    }
    static void stop(runtime&) {
        throw cmdexception("in2net_transfer::stop not defined for this hardware!");
    }
};

// Now make specializations which do the Right Thing (tm) for the indicated
// hardware. 


// For the old Mark5A and Mark5A+
template <>
struct in2net_transfer<mark5a>: public in2netbase<mark5a> {
    static void setup(runtime& rte) {
        // switch off clock
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2,"setup: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << std::endl);
        notclock = 1;
        DEBUG(2,"setup: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << std::endl);
    }

    // start/resume the recordclock. for Mark5A they are the same
    static void start(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2, "in2net_transfer<mark5a>=on: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << std::endl);
        notclock = 0;
        suspendf  = 0;
        DEBUG(2, "in2net_transfer<mark5a>=on: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << std::endl);
    }
    static void resume(runtime& rte) {
        start(rte);
    }

    static void pause(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2, "in2net_transfer<mark5a>=pause: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << std::endl);
        notclock = 1;
        DEBUG(2, "in2net_transfer<mark5a>=pause: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << std::endl);
    }
    static void stop(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];

        // turn off clock
        DEBUG(2, "in2net_transfer<mark5a>=stop: notclock: " << hex_t(*notclock) << std::endl);
        notclock = 1;
        DEBUG(2, "in2net_transfer<mark5a>=stop: notclock: " << hex_t(*notclock) << std::endl);
    }
};

// For Mark5B/DIM and Mark5B+/DIM
template <>
struct in2net_transfer<mark5b> : public in2netbase<mark5b> {
    static void setup(runtime&) {
        DEBUG(2, "in2net_transfer<mark5b>=setup" << std::endl);
    }
    static void start(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=start" << std::endl);
        start_mk5b_dfhg( rte );
    }
    // start/resume the recordclock
    static void resume(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=resume" << std::endl);
        // Good. Unpause the DIM. Will restart datatransfer on next disk frame
        rte.ioboard[ mk5breg::DIM_PAUSE ] = 0;
    }

    static void pause(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=pause" << std::endl);
        // Good. Pause the DIM. Stops the data flow on the next disk frame
        rte.ioboard[ mk5breg::DIM_PAUSE ] = 1;
    }
    static void stop(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=stop" << std::endl);
        rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;
    }
};

// the 5c
template <>
struct in2net_transfer<mark5c>: public in2netbase<mark5c> {

    static void setup(runtime& /*rte*/) {
        DEBUG(2, "in2net_transfer<mark5c>=setup" << std::endl);
    }
    // Depending on the actual transfer mode we call
    // the appropriate Record/Append function.
    // When "fork"'ing we do Append, otherwise Record
    static void start(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5c>=start" << std::endl);
        EZASSERT(rte.transfermode!=no_transfer, Error_Code_6_Exception);
        if( isfork(rte.transfermode) ) {
            XLRCALL( ::XLRAppend(rte.xlrdev.sshandle()) );
        } else {
            XLRCALL( ::XLRRecord(rte.xlrdev.sshandle(), XLR_WRAP_DISABLE, 1) );
        }
    }
    // start/resume the recordclock
    static void resume(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5c>=resume" << std::endl);
        EZASSERT(rte.transfermode!=no_transfer, Error_Code_6_Exception);
        if( isfork(rte.transfermode) ) {
            XLRCALL( ::XLRAppend(rte.xlrdev.sshandle()) );
        } else {
            XLRCALL( ::XLRRecord(rte.xlrdev.sshandle(), XLR_WRAP_DISABLE, 1) );
        }
    }

    static void pause(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5c>=pause" << std::endl);
        EZASSERT(rte.transfermode!=no_transfer, Error_Code_6_Exception);
        XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
    }
    static void stop(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5c>=stop" << std::endl);
        EZASSERT(rte.transfermode!=no_transfer, Error_Code_6_Exception);
        XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
    }
};


// VDIF output-size computing policies.
// The idea is that there may be different mechanisms (constraints) to
// transform a requested VDIF frame size into an actual vdif frame size,
// given a particular input frame size.
typedef unsigned int (*vdif_computer)(unsigned int req_vdif, unsigned int framesz);

// This policy asserts that the given req_vdif either integer divides the
// framesize OR, if req_vdif == -1, then returns the input framesz.
// It will also assert that it is a *legal* vdif frame size
unsigned int size_is_request(unsigned int, unsigned int);

// This policy will assume that the req_vdif is a *hint* - the *maximum*
// vdif frame size (e.g. to fit it into a single network packet).
// It will automatically compute the largest legal vdif frame size that
// will fit into req_vdif
unsigned int size_is_hint(unsigned int, unsigned int);


#endif
