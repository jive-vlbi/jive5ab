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

string disk_state_mask_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    reply << "!" << args[0] << ((qry)?('?'):('='));

    // would seem that query should be possible as long as disks available,
    // command only when disks not busy [when disks unavailble, neither 
    // qry nor command is possible, obviously]
    INPROGRESS(rte, reply, diskunavail(ctm) || (!qry && (todisk(ctm) || fromdisk(ctm))))

    const runtime::disk_state_flags flags[] = { runtime::erase_flag, runtime::play_flag, runtime::record_flag };
    const size_t num_flags = sizeof(flags)/sizeof(flags[0]);

    if ( qry ) {
        reply << " 0";
        for ( size_t i = 0; i < num_flags; i++ ) {
            reply << " : " << ( (rte.disk_state_mask & flags[i]) ? "1" : "0");
        }
        reply << " ;";
        return reply.str();
    }

    // handle command
    if ( args.size() != (num_flags + 1) ) {
        reply << " 8 : command required " << num_flags << " arguments ;";
        return reply.str();
    }

    unsigned int new_state_mask = 0;
    for ( size_t i = 0; i < num_flags; i++ ) {
        if ( args[i+1] == "1" ) {
            new_state_mask |= flags[i];
        }
        else if ( args[i+1] != "0" ) {
            reply << " 8 : all arguments should be either 0 or 1 ;";
            return reply.str();
        }
    }
    rte.disk_state_mask = new_state_mask;

    reply << " 0";
    for ( size_t i = 0; i < num_flags; i++ ) {
        reply << " : " << args[i+1];
    }
    reply << " ;";
    
    return reply.str();
}
