// utilities for dealing with DOT [D(ata) O(bserving) T(ime)]
// Copyright (C) 2007-2008 Harro Verkouter
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
#ifndef JIVE5A_DOTZOOI_H
#define JIVE5A_DOTZOOI_H

#include <map>
#include <string>
#include <timewrap.h>
#include <ezexcept.h>


// Declare a DOT-exception
DECLARE_EZEXCEPT(dotclock)


// We must keep track of the system-time-to-DOT-clock mapping.
// Fortunately there is only one mapping defined for the whole
// system as there is never more than one DOT clock running.
// We remember the localtime at the last 1PPS/dot_set command.
// (dot_set waits for the next 1PPS and sets the DOT at that
// time).
// If we need to start a recording, we compute delta-systime,
// add that to the DOT, add a second (for we must program the 
// time at the NEXT 1PPS) and then set the system orf!

// (re)set the mapping of local -> DOT time
void bind_dot_to_local( const pcint::timeval_type& dot,
                        const pcint::timeval_type& sys );

// Increment the DOT clock by 'nsec' seconds - can be
// positive or negative
void inc_dot(int nsec);

// get current time and convert it to DOT according to
// mapping. If no previous mapping defined then DOT==localtime.
// NOTE: this time may be way off and not syncronized to the
// 1PPS -> the user should make sure to issue a dot_set= before
// making a recording.
// You can pass in a local time but it defaults to now()
pcint::timeval_type local2dot( const pcint::timeval_type& lcl = pcint::timeval_type::now() );


// will parse time_text into result, time text should be of the form:
// <n>y<n>d<n>h<n>m<f>s, where values may be omitted from the big end
// returns the number of fields parsed
unsigned int parse_vex_time( std::string time_text, struct ::tm& result, unsigned int& microseconds );

// zero based seconds in year
unsigned int seconds_in_year(struct tm& tm);

#endif
