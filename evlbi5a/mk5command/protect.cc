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
        // According to Conduant SDK docs, XLRGetDirectory() will return
        // useful information about the current recording, irrespective
        // of the mode/partition that the device is currently running in
        S_DIR   dirInfo;

        XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &dirInfo) );
        reply << " 0 : " << (dirInfo.WriteProtected ? "on" : "off") << " ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        reply << " 8 : must have argument ;";
        return reply.str();
    }

    string warning;
    try {
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
            // HV: 21 Oct 2016 Weeeeelllllll ... Conduant have fucked up.
            //                 SDK9.4 firmware behaviour is now that
            //                 XLRClearWriteProtect() may just fail under
            //                 circumstances that it did not use to fail under.
            //                 So this will basically prevent our users from
            //                 e.g. erasing or vsn'ing a disk pack.
            //                 So go back to the old, 'wrong' situation:
            //                 protect=off may fail but the next command will be
            //                 executed nonetheless.
            rte.protected_count = 2;
            XLRCALL( ::XLRClearWriteProtect(rte.xlrdev.sshandle()) );
        }
        else {
            reply << " 8 : argument must be 'on' or 'off' ;";
            return reply.str();
        }
    }
    catch( const xlrexception& xlr ) {
        warning = string(" : ") + xlr.what();
    }

    reply << " 0" << warning << " ;";
    return reply.str();
}
