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


// turns on/off automatic switching to other bank when a disk is complete
string bank_switch_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << ((qry)?('?'):('='));

    const S_BANKMODE curbm = rte.xlrdev.bankMode();
    if ( qry ) {
        if ( curbm == SS_BANKMODE_NORMAL ) {
            reply << " 0 : off ;";
        }
        else if ( curbm == SS_BANKMODE_AUTO_ON_FULL ) {
            reply << " 0 : on ;";
        }
        else {
            reply << " 6 : not in bank mode ;";
        }
    }
    else {
        if ( args.size() < 2 ) {
            reply << " 8 : no mode parameter;";
        }
        else {
            if ( args[1] == "on" ) {
                if ( curbm != SS_BANKMODE_AUTO_ON_FULL ) {
                    rte.xlrdev.setBankMode( SS_BANKMODE_AUTO_ON_FULL );
                }
                reply << " 0 ;";
            }
            else if ( args[1] == "off" ) {
                if ( curbm != SS_BANKMODE_NORMAL ) {
                    rte.xlrdev.setBankMode( SS_BANKMODE_NORMAL );
                }
                reply << " 0 ;";
            }
            else {
                reply << " 8 : mode parameters should be 'on' or 'off' ;";
            }
        }
    }

    return reply.str();
}
