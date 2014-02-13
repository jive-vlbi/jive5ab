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
#include <ezexcept.h>
#include <iostream>

using namespace std;

DECLARE_EZEXCEPT(disk_pack_configuration_exception)
DEFINE_EZEXCEPT(disk_pack_configuration_exception)

string get_stats_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    XLRCODE(SSHANDLE      ss = rte.xlrdev.sshandle());
    S_DRIVESTATS  stats[XLR_MAXBINS];
    static per_runtime<unsigned int> current_drive_number;
    
    reply << "!" << args[0] << (q?('?'):('='));

    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // The query may only execute if the disks are available
    // and the disks are not busy
    INPROGRESS(rte, reply, diskunavail(rte.transfermode) || streamstorbusy(rte.transfermode))

    reply << " 0";
    
    const S_DEVINFO devinfo = rte.xlrdev.devInfo();

    if ( (devinfo.TotalCapacity == 0) || 
         (devinfo.NumBuses == 0) ||
         (devinfo.NumDrives == 0) ) {
        reply << " 6 : no disk pack mounted ;";
        return reply.str();
    }

    // drives are counted according to get_stats documentation:
    // 0 = 0M, 1 = 0S, 2 = 1M, 3 = 1S, ..., 14 = 7M, 15 = 7S
    
    // for all valid pack configurations, 
    // either all buses with a master have slaves or none do
    const unsigned int drives_per_bus = devinfo.NumDrives / devinfo.NumBuses;
    EZASSERT2( (devinfo.NumDrives % devinfo.NumBuses == 0) && 
               ((drives_per_bus == 1) || (drives_per_bus == 2)),
               disk_pack_configuration_exception,
               EZINFO("invalid disk pack configuration (" << devinfo.NumBuses << " buses, " << devinfo.NumDrives << " drives)") );
    const unsigned int drive_step = 3 - drives_per_bus;

    if ( current_drive_number.find(&rte) == current_drive_number.end() ) {
        // make sure we start at 0
        current_drive_number[&rte] = 15;
    }
    
    unsigned int drive_to_use = ( current_drive_number[&rte] + drive_step ) 
        / drive_step * drive_step;
    if ( drive_to_use >= devinfo.NumBuses * 2 ) {
        drive_to_use = 0;
    }

    current_drive_number[&rte] = drive_to_use;
    
    reply << " : " << drive_to_use;

    XLRCODE( unsigned int bus = drive_to_use / 2 );
    XLRCODE( unsigned int master_slave = (drive_to_use % 2 ? XLR_SLAVE_DRIVE : XLR_MASTER_DRIVE) );
    XLRCALL( ::XLRGetDriveStats( ss, bus, master_slave, stats ) );
    for (unsigned int i = 0; i < XLR_MAXBINS; i++) {
        reply << " : " << stats[i].count;
    }
    reply << " : " << XLRCODE( ::XLRDiskRepBlkCount( ss, bus, master_slave ) << ) " ;";

    return reply.str();
}

