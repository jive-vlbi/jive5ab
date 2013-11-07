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


string replaced_blks_fn(bool q, const vector<string>& args, runtime& XLRCODE(rte) ) {
    ostringstream reply;
    XLRCODE(SSHANDLE ss = rte.xlrdev.sshandle());
    
    reply << "!" << args[0] << (q?('?'):('='));

    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Command should be allowed to execute if streamstor not busy
    INPROGRESS(rte, reply, streamstorbusy(rte.transfermode))

    reply << " 0";
    for ( unsigned int disk = 0; disk < 8; disk++) {
        XLRCODE(unsigned int bus = disk/2);
        XLRCODE(unsigned int master_slave = disk % 2 ? XLR_SLAVE_DRIVE : XLR_MASTER_DRIVE);
        XLRCODE(reply << " : " << ::XLRDiskRepBlkCount( ss, bus, master_slave ) ); 
    }
    do_xlr_lock();
    XLRCODE(reply << " : " << ::XLRTotalRepBlkCount( ss ) << " ;");
    do_xlr_unlock();

    return reply.str();
}

