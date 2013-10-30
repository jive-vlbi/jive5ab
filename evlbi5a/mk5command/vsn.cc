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
#include <regex.h>
#include <iostream>

using namespace std;


string vsn_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    char          label[XLR_LABEL_LENGTH + 1];
    XLRCODE(SSHANDLE ss = rte.xlrdev.sshandle());

    label[XLR_LABEL_LENGTH] = '\0';

    reply << "!" << args[0] << (q?('?'):('=')) ;

    XLRCALL( ::XLRGetLabel( ss, label) );
    
    pair<string, string> vsn_state = disk_states::split_vsn_state(string(label));
    if ( q ) {
        reply << " 0 : " << vsn_state.first;

        // check for matching serial numbers
        vector<S_DRIVEINFO> cached;
        try {
            cached = rte.xlrdev.getStoredDriveInfo();
        }
        catch ( userdir_enosys& ) {
            reply << " : Unknown ;";
            return reply.str();
        }
        
        S_DRIVEINFO drive_info;
        for ( size_t i = 0; i < cached.size(); i++) {
            UINT bus = i / 2;
            UINT master_slave = (i % 2 ? XLR_SLAVE_DRIVE : XLR_MASTER_DRIVE);
            string serial;
            try {
                XLRCALL( ::XLRGetDriveInfo( ss, bus, master_slave, &drive_info ) );
                serial = drive_info.Serial;
            }
            catch ( ... ) {
                // use empty string
            }
            if ( serial != cached[i].Serial ) {
                reply << " : Fail : " << (bus * 2 + master_slave) << " : " << cached[i].Serial << " : " << serial << " : Disk serial number mismatch ;";
                return reply.str();
            }
        }

        reply << " : OK ;";
        return reply.str();
    }
    // must be command
    
    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot write VSN while doing " << rte.transfermode << " ;";
        return reply.str();
    }

    if ( rte.protected_count == 0 ) {
        reply << " 6 : need an immediate preceding protect=off ;";
        return reply.str();
    }
    // ok, we're allowed to write VSN

    if ( args.size() < 2 ) {
        reply << " 8 : commands requires an argument ;";
        return reply.str();
    }

    // check VSN format rules: 
    // (1) total length = 8
    // (2) [A-Z]{2,6}(+|-)[0-9]^{1,5}
    if ( args[1].size() != 8 ) {
        reply << " 8 : VSN must be 8 characters long ;";
        return reply.str();
    }
    
    string regex_format = "[A-Za-z]\\{2,6\\}\\(-\\|\\+\\)[0-9]\\{1,5\\}";
    regex_t regex;
    int regex_error;
    char regex_error_buffer[1024];
    ASSERT2_COND( (regex_error = ::regcomp(&regex, regex_format.c_str(), 0)) == 0, ::regerror(regex_error, &regex, &regex_error_buffer[0], sizeof(regex_error_buffer)); SCINFO( "regex compilation returned error: '" << regex_error_buffer << "'") );
    regex_error = ::regexec(&regex, args[1].c_str(), 0, NULL, 0);
    if ( regex_error != 0 ) {
        ::regerror(regex_error, &regex, &regex_error_buffer[0], sizeof(regex_error_buffer));
        reply << " 8 : " << args[1] << " does not match the regular expression [A-Z]{2,6}(+|-)[0-9]{1,5} ;";
        return reply.str();
    }
    
    // compute the capacity and maximum data rate, to be appended to vsn
    S_DEVINFO     dev_info;
    S_DRIVEINFO   drive_info;

    XLRCALL( ::XLRGetDeviceInfo( ss, &dev_info ) );

    vector<unsigned int> master_slave;
    master_slave.push_back(XLR_MASTER_DRIVE);
    master_slave.push_back(XLR_SLAVE_DRIVE);
    
    unsigned int number_of_disks = 0;
    UINT minimum_capacity = std::numeric_limits<UINT>::max(); // in 512 bytes
    for (unsigned int bus = 0; bus < dev_info.NumBuses; bus++) {
        for (vector<unsigned int>::const_iterator ms = master_slave.begin();
             ms != master_slave.end();
             ms++) {
            try {
                XLRCALL( ::XLRGetDriveInfo( ss, bus, *ms, &drive_info ) );
                number_of_disks++;
                if ( drive_info.Capacity < minimum_capacity ) {
                    minimum_capacity = drive_info.Capacity;
                }
            }
            catch ( ... ) {
                DEBUG( -1, "Failed to get drive info for drive " << bus << (*ms==XLR_MASTER_DRIVE?" master":" slave") << endl);
            }
        }
    }

    // construct the extended vsn, which is
    // (1) the given label
    // (2) "/" Capacity, computed as the minumum capacity over all disk rounded down to a multiple of 10GB times the number of disks
    // (3) "/" Maximum rate, computed as 128Mbps times number of disks
    // (4) '\036' (record separator) disk state
    ostringstream extended_vsn;
    const uint64_t capacity_round = 10000000000ull; // 10GB
    extended_vsn << toupper(args[1]) << "/" << (((uint64_t)minimum_capacity * 512ull)/capacity_round*capacity_round * number_of_disks / 1000000000) << "/" << (number_of_disks * 128) << '\036' << vsn_state.second;

    rte.xlrdev.write_vsn( extended_vsn.str() );

    reply << " 0 ;";
    return reply.str();
}
