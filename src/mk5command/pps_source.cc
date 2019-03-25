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


// The 1PPS source command for Mk5B/DIM
string pps_source_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // Pulse-per-second register value in HumanReadableFormat
    static const string pps_hrf[4] = { "none", "altA", "altB", "vsi" };
    const unsigned int  npps( sizeof(pps_hrf)/sizeof(pps_hrf[0]) );
    // variables
    unsigned int                 selpps;
    ostringstream                oss;
    const transfer_type          ctm( rte.transfermode );
    ioboard_type::mk5bregpointer pps = rte.ioboard[ mk5breg::DIM_SELPP ];

    oss << "!" << args[0] << (qry?('?'):('='));

    // Query is possible always, command only when the i/o board is not busy
    INPROGRESS(rte, oss, !qry && (fromio(ctm) || toio(ctm)))

    if( qry ) {
        oss << " 0 : " << pps_hrf[ *pps ] << " ;";
        return oss.str();
    }
    // It was a command. We must have (at least) one argument [the first, actually]
    // and it must be non-empty at that!
    if( args.size()<2 || args[1].empty() ) {
        oss << " 8 : Missing argument to command ;";
        return oss.str();
    }
    // See if we recognize the pps string
    for( selpps=0; selpps<npps; ++selpps )
        if( ::strcasecmp(args[1].c_str(), pps_hrf[selpps].c_str())==0 )
            break;
    if( selpps==npps ) {
        oss << " 6 : Unknown PPS source '" << args[1] << "' ;";
    } else {
        // write the new PPS source into the hardware
        pps = selpps;
        oss << " 0 ;";
    }
    return oss.str();
}
