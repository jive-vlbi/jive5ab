// 'implementation' of the functions found in dotzooi.h
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
#include <dotzooi.h>
#include <evlbidebug.h>

// We only keep delta(DOT, system) as that's all we need to know.
// by default it's zero, no difference :)
static pcint::timediff  delta_dot_system;

// map system and a dot time to each other
// this amount, currently, to not much more than
// storing the offset ;)
void bind_dot_to_local( const pcint::timeval_type& dot,
                        const pcint::timeval_type& sys ) {
    delta_dot_system = dot - sys;
    DEBUG(3 , "bind_dot_to_locl:\n" <<
              "  dot=" << dot << "\n"
              "  sys=" << sys << "\n"
              " => delta=" << delta_dot_system << "\n" );
    return;
}

pcint::timeval_type local2dot( const pcint::timeval_type& lcl ) {
    DEBUG(3, "local2dot(" << lcl << "), adding " << delta_dot_system << std::endl);
    return (lcl + delta_dot_system);
}
