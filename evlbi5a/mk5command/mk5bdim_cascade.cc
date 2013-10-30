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


string mk5bdim_cascade_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream       reply;

    // This part of the reply we can already form
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        const bool  cascade    = *rte.ioboard[mk5breg::DIM_CASC];
        const bool  en_vsi_out = *rte.ioboard[mk5breg::DIM_EN_VSI_OUT];

        reply << "0 : ";
        if( cascade && en_vsi_out )
            reply << "On";
        else if( !(cascade || en_vsi_out) )
            reply << "Off";
        else
            reply << "?";
        reply << " ;";
        return reply.str();
    }

    // Ok must be command
    const string casc( OPTARG(1, args) );

    if( casc.empty() ) {
        reply << "8 : Cascade must have an argument, none supplied ;";
    } else if( ::tolower(casc)=="on" || casc=="1" ) {
        rte.ioboard[mk5breg::DIM_CASC]       = true;
        rte.ioboard[mk5breg::DIM_EN_VSI_OUT] = true;
        reply << "0 ;";
    } else if( ::tolower(casc)=="off" || casc=="0" ) {
        rte.ioboard[mk5breg::DIM_CASC]       = false;
        rte.ioboard[mk5breg::DIM_EN_VSI_OUT] = false;
        reply << "0 ;";
    } else {
        reply << "8 : Cascade must 1, On, 0 or Off ;";
    }
    return reply.str();
}
