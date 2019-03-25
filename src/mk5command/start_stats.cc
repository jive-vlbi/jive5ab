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


string start_stats_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); 

    reply << "!" << args[0] << (q?('?'):('='));

    // When are we allowed to execute?
    // Query can execute always, command only when disks are idle
    INPROGRESS(rte, reply, !q && (diskunavail(ctm) || todisk(ctm) || fromdisk(ctm)))

    // units are in 15ns

    if ( q ) {
        reply << " 0";
        vector< ULONG > ds = rte.xlrdev.get_drive_stats( );
        for ( unsigned int i = 0; i < ds.size(); i++ ) {
            reply << " : " << (ds[i] / 1e9 * 15) << "s";
        }
        reply << " ;";
        return reply.str();
    }

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot set statistics during transfers ;";
        return reply.str();
    }

    vector<ULONG> to_use;
    if ( args.size() == xlrdevice::drive_stats_length + 1 ) {
        char* eocptr;
        double parsed;
        for ( unsigned int i = 0; i < xlrdevice::drive_stats_length; i++ ) {
            parsed = ::strtod(args[i+1].c_str(), &eocptr);
            ASSERT2_COND( !(fabs(parsed) <= 0 && eocptr==args[i+1].c_str()) && (*eocptr=='\0' || *eocptr=='s') && !((parsed>=HUGE_VAL || parsed<=-HUGE_VAL) && errno==ERANGE),
                          SCINFO("Failed to parse a time from '" << args[i+1] << "'") );
            to_use.push_back( (ULONG)round(parsed * 1e9 / 15) );
        }
    }
    else if ( !((args.size() == 1) || ((args.size() == 2) && args[1].empty())) ) { // an empty string is parsed as an argument, so check that
        reply << " 8 : " << args[0] << " requires 0 or " << xlrdevice::drive_stats_length << " arguments, " << (args.size() - 1) << " given ;";
        return reply.str();
    }
    
    rte.xlrdev.set_drive_stats( to_use );

    reply << " 0 ;";

    return reply.str();
}
