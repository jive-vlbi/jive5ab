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


string recover_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('='));

    if ( q ) {
        // command set doesn't say it's command only, 
        // neither what it does as a query, so just reply
        reply << " 0 ;";
        return reply.str();
    }
    
    if ( args.size() < 2 ) {
        reply << " 8 : missing recover mode parameter ;";
        return reply.str();
    }

    char* eptr;
    long int mode = ::strtol(args[1].c_str(), &eptr, 0);
    if ( (mode < 0) || (mode > 2) ) {
        reply << " 8 : mode (" << mode << ") out of range [0, 2] ;";
        return reply.str();
    }

    if ( mode == 0 ) {
        rte.xlrdev.recover( SS_RECOVER_POWERFAIL );
    }
    else if ( mode == 1 ) {
        rte.xlrdev.recover( SS_RECOVER_OVERWRITE );
    }
    else {
        rte.xlrdev.recover( SS_RECOVER_UNERASE );
    }

    reply << " 0 : " << mode << " ;";
    return reply.str();
}
