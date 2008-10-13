// available thread-functions
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
#ifndef JIVE5A_THREADFNS_H
#define JIVE5A_THREADFNS_H

#include <map>
#include <string>
#include <runtime.h>

// usually the name of the threadfunctions is enough
// info as to what it (supposedly) does.

void* disk2mem( void* );
void* fifo2mem( void* );
void* net2mem( void* );

void* mem2streamstor( void* );
void* mem2net_udp( void* );
void* mem2net_tcp( void* );

// for UDP helper thread we have a choice of methods.
// the user can set which one he/she wishes to use
// The code should look in this map if the requested
// helper is configured/available
typedef std::map<std::string, void* (*)(void*)> udphelper_maptype;

// Get the map of "name" => threadfunction, which contains
// the currently available udphelpers
const udphelper_maptype& udphelper_map( void );


// threadargument for the delayed_play_fn
struct dplay_args {
    double   rot;
    runtime* rteptr;

    dplay_args();
};
void* delayed_play_fn( void* dplay_args_ptr );

#endif
