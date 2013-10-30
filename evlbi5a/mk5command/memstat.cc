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


string memstat_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream                   reply;

    // This part of the reply we can already form
    reply << "!" << args[0] << ((q)?('?'):('=')) << " ";

    if( !q ) {
        reply << " 2 : query only ;";
        return reply.str();
    }
    reply << " 0 : " << rte.get_memory_status() << " ;";
    return reply.str();
}

