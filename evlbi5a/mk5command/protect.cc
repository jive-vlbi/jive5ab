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


string protect_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    reply << "!" << args[0] << (q?('?'):('='));

    // Protect query only unavailable during conditioning,
    // protect command only available when doing nothing with the disks
    INPROGRESS(rte, reply, (q && ctm==condition) || (!q && streamstorbusy(ctm)))

    if ( q ) {
        if ( rte.xlrdev.bankMode() == SS_BANKMODE_DISABLED ) {
            reply << " 6 : cannot determine protect in non-bank mode ;";
            return reply.str();
        }
        S_BANKSTATUS bs[2];
        XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_A, &bs[0]) );
        XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_B, &bs[1]) );
        for (unsigned int bank = 0; bank < 2; bank++) {
            if (bs[bank].Selected) {
                reply << " 0 : " << (bs[bank].WriteProtected ? "on" : "off") << " ;";
                return reply.str();
            }
        }
        reply << " 6 : no bank selected ;";
    }
    else {
        if ( args.size() < 2 ) {
            reply << " 8 : must have argument ;";
            return reply.str();
        }
        
        if ( args[1] == "on" ) {
            // In this case it is good to set the protect count to zero;
            // even if the firmware failed execution, at least we know
            // that our code will not allow any further execution of
            // commands that could clobber the disk pack
            rte.protected_count = 0;
            XLRCALL( ::XLRSetWriteProtect(rte.xlrdev.sshandle()) );
        }
        else if ( args[1] == "off" ) {
            // Here, OTOH, first setting the write protect flag to off
            // and then the firmware failing to actually clear the write
            // protect will leave us in a borked state.
            // i.e. "protect=off" will return "!protect = 4 : failed;" 
            // and yet the next (destructive) command will be executed!
            // That should ne'er have happened
            XLRCALL( ::XLRClearWriteProtect(rte.xlrdev.sshandle()) );
            rte.protected_count = 2;
        }
        else {
            reply << " 8 : argument must be 'on' or 'off' ;";
            return reply.str();
        }

        reply << " 0 ;";
    }
    return reply.str();
}
