// Copyright (C) 2007-2015 Harro Verkouter
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
#include <version.h>
#include <iostream>
#include <chain.h>

using namespace std;


////////////////////////////////////////////////////////////////////
//
//  The actual reset command implementation
//
////////////////////////////////////////////////////////////////////
string reset_vbs_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream           reply;

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    if ( q ) {
        reply << "2 : only implemented as command ;";
        return reply.str();
    }

    // Assert that there *is* at least an argument!
    EZASSERT2( OPTARG(1, args).empty()==false, Error_Code_6_Exception,
               EZINFO(args[0] << " needs at least one argument") );

    // First handle the "reset=abort"
    if ( args[1] == "abort" ) {
        // in case of error, set transfer to no transfer
        // the idea is: it is better to be in an unknown state that might work
        // than in a state that is known but useless
        if ( rte.transfermode == disk2net ||
             rte.transfermode == disk2file ||
             rte.transfermode == disk2etransfer
             ) {
            try {
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
                rte.transfermode = no_transfer;
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
                rte.transfermode = no_transfer;
            }
        }
        else {
            reply << "6 : nothing running to abort ;";
        }
    } else {
        reply << "6 : " << args[1] << " does not apply to reset for FlexBuff/Mark6 ;";
    }
    return reply.str();
}
