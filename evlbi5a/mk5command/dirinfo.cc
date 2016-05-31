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


string dir_info_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << ((qry)?('?'):('='));

    if( !qry ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // dir info should only be available if the disks are available
    INPROGRESS(rte, reply, streamstorbusy(rte.transfermode))

    long             page_size = ::sysconf(_SC_PAGESIZE);
    uint64_t         totalCapacity( 0 ); // In system pages
    uint64_t         length( 0 ); // current recording length in bytes?
    const S_BANKMODE curbm = rte.xlrdev.bankMode();

    // Handle bank mode and non-bank mode transparently
    if( curbm==SS_BANKMODE_NORMAL ) {
        // Ok, we're in BankA/BankB mode
        unsigned int active_index;
        S_BANKSTATUS bs[2];

        XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_A, &bs[0]) );
        XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_B, &bs[1]) );

        if( bs[0].State==STATE_READY && bs[0].Selected ) {
            active_index = 0;
        }
        else if ( bs[1].State==STATE_READY && bs[1].Selected ) {
            active_index = 1;
        }
        else {
            reply << " 6 : no active bank ;";
            return reply.str();
        }
        length        = bs[active_index].Length;
        totalCapacity = bs[active_index].TotalCapacity;
    } else if( curbm==SS_BANKMODE_DISABLED ) {
        // Deal with non-bank mode
        S_DEVINFO   devInfo;

        XLRCALL( ::XLRGetDeviceInfo(GETSSHANDLE(rte), &devInfo) );

        // where to get recorded length from?
        do_xlr_lock();
        length        = ::XLRGetLength(GETSSHANDLE(rte));
        do_xlr_unlock();
        totalCapacity = devInfo.TotalCapacity;
    } else {
        reply << " 6 : in unrecognized bank mode ; ";
        return reply.str();
    }

    reply << " 0 : " << rte.xlrdev.nScans() << " : " << length << " : " << (totalCapacity * (uint64_t)page_size) << " ;";

    return reply.str();
}
