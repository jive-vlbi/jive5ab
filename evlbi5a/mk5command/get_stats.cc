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


string get_stats_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    XLRCODE(SSHANDLE      ss = rte.xlrdev.sshandle());
    S_DRIVESTATS  stats[XLR_MAXBINS];
    static per_runtime<unsigned int> current_drive_number;
    
    reply << "!" << args[0] << (q?('?'):('='));

    if (rte.transfermode != no_transfer) {
        reply << " 6 : cannot retrieve statistics during transfers ;";
        return reply.str();
    }

    reply << " 0";
    
    unsigned int drive_to_use = current_drive_number[&rte];
    if (drive_to_use + 1 >= 2 * rte.xlrdev.devInfo().NumBuses) {
        current_drive_number[&rte] = 0;
    }
    else {
        current_drive_number[&rte] = drive_to_use + 1;
    }
    
    reply << " : " << drive_to_use;
    XLRCODE( unsigned int bus = drive_to_use/2 );
    XLRCODE( unsigned int master_slave = (drive_to_use % 2 ? XLR_SLAVE_DRIVE : XLR_MASTER_DRIVE) );
    XLRCALL( ::XLRGetDriveStats( ss, bus, master_slave, stats ) );
    for (unsigned int i = 0; i < XLR_MAXBINS; i++) {
        reply << " : " << stats[i].count;
    }
    reply << " : " << XLRCODE( ::XLRDiskRepBlkCount( ss, bus, master_slave ) << ) " ;";

    return reply.str();
}

