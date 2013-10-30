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
#include <limits.h>
#include <iostream>

using namespace std;


string track_set_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('='));

    if ( q ) {
        reply << " 0 : " << register2track(*rte.ioboard[ mk5areg::ChASelect ])
              << " : " << register2track(*rte.ioboard[ mk5areg::ChBSelect ]) << " ;";
        return reply.str();
    }

    if ( args.size() < 3 ) {
        reply << " 8 : track_set requires 2 arguments ;";
        return reply.str();
    }
    
    mk5areg::regtype::base_type tracks[2];
    tracks[0] = rte.ioboard[ mk5areg::ChASelect ];
    tracks[1] = rte.ioboard[ mk5areg::ChBSelect ];
    unsigned long int parsed;
    char* eocptr;
    for ( unsigned int i = 0; i < 2; i++ ) {
        if ( args[i+1] == "inc" ) {
            tracks[i] = (tracks[i] + 1) % 64;
        }
        else if ( args[i+1] == "dec" ) {
            if ( tracks[i] == 0 ) {
                tracks[i] = 63;
            }
            else {
                tracks[i]--;
            }
        }
        else {
            errno  = 0;
            parsed = ::strtoul(args[i+1].c_str(), &eocptr, 0);
            ASSERT2_COND( (parsed!=ULONG_MAX || errno!=ERANGE) &&
                          (parsed!=0         || eocptr!=args[i+1].c_str()) &&
                          parsed <= std::numeric_limits<unsigned int>::max(),
                          SCINFO("failed to parse track from '" << args[i+1] << "'") );
            tracks[i] = track2register( parsed );
        }
    }
    rte.ioboard[ mk5areg::ChASelect ] = tracks[0];
    rte.ioboard[ mk5areg::ChBSelect ] = tracks[1];
    
    reply << " 0 ;";
    return reply.str();
}
