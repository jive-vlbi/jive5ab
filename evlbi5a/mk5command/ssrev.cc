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


// Display all version info we know about "SS_rev?"
// Only do it as query
string ssrev_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const S_DEVINFO&    devInfo( rte.xlrdev.devInfo() );
    const S_XLRSWREV&   swRev( rte.xlrdev.swRev() );

    // Start the reply
    reply << "!" << args[0] << (q?('?'):('=')) ;

    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Active transfer? Don't allow it then! (technically, I think
    // it *could* be done - just to be compatible with Mark5A/John Ball)
    INPROGRESS(rte, reply, rte.transfermode!=no_transfer)

    // Get all the versions!
    reply << " 0 : "
          << "BoardType " << devInfo.BoardType << " : "
          << "SerialNum " << devInfo.SerialNum << " : "
          << "ApiVersion " << swRev.ApiVersion << " : "
          << "ApiDateCode " << swRev.ApiDateCode << " : "
          << "FirmwareVersion " << swRev.FirmwareVersion << " : "
          << "FirmDateCode " << swRev.FirmDateCode << " : "
          << "MonitorVersion " << swRev.MonitorVersion << " : "
          << "XbarVersion " << swRev.XbarVersion << " : " 
          << "AtaVersion " << swRev.AtaVersion << " : "
          << "UAtaVersion " << swRev.UAtaVersion << " : "
          << "DriverVersion " << swRev.DriverVersion;
    if( rte.xlrdev.isAmazon() ) {
        const S_DBINFO& dbInfo( rte.xlrdev.dbInfo() );

        reply << " : "
              << "AMAZON : "
              << "SerialNum " << dbInfo.SerialNum << " : "
              << "PCBVersion " << dbInfo.PCBVersion << " : "
              << "PCBType " << dbInfo.PCBType << " : "
              << "PCBSubType " << dbInfo.PCBSubType << " : "
              << "FPGAConfig " << dbInfo.FPGAConfig << " : "
              << "FPGAConfigVersion " << dbInfo.FPGAConfigVersion << " : "
              << "NumChannels " << dbInfo.NumChannels;
    }
    reply << " ;";
    return reply.str();
}
