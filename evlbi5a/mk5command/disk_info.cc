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


string disk_info_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    XLRCODE(SSHANDLE ss = rte.xlrdev.sshandle());
    S_DRIVEINFO   drive_info;
    static const unsigned int max_string_size = max(XLR_MAX_DRIVESERIAL, XLR_MAX_DRIVENAME) + 1;
    vector<char>  serial_mem(max_string_size);
    char*         serial = &serial_mem[0];
    
    serial[max_string_size - 1] = '\0'; // make sure all serials are null terminated

    // Form inital part of the reply
    reply << "!" << args[0] << (q?('?'):('=')) ;

    // Only available as query
    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Check if we're allowed to execute; we can only execute if
    // the disks are not unavailable
    INPROGRESS(rte, reply, streamstorbusy(rte.transfermode));

    // Ok, carry on.
    reply << " 0";

    vector<unsigned int> master_slave;
    master_slave.push_back(XLR_MASTER_DRIVE);
    master_slave.push_back(XLR_SLAVE_DRIVE);

    S_DEVINFO dev_info;
    rte.xlrdev.copyDevInfo(dev_info);
    
    for (unsigned int bus = 0; bus < dev_info.NumBuses; bus++) {
        for (vector<unsigned int>::const_iterator ms = master_slave.begin();
             ms != master_slave.end();
             ms++) {
            try {
                XLRCALL( ::XLRGetDriveInfo( ss, bus, *ms, &drive_info ) );
                if ( args[0] == "disk_serial" )  {
                    strncpy( serial, drive_info.Serial, max_string_size );
                    reply << " : " << strip(serial);
                }
                else if ( args[0] == "disk_model" ) {
                    strncpy( serial, drive_info.Model, max_string_size );
                    reply << " : " << strip(serial);
                }
                else { // disk_size
                    reply << " : " << ((uint64_t)drive_info.Capacity * 512ull); 
                }
            }
            catch ( ... ) {
                reply << " : ";
            }
        }
    }
    reply << " ;";

    return reply.str();
}
