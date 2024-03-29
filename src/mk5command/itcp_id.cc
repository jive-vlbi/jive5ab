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


string itcp_id_fn(bool q,  const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    // Query is always allowed, command only when no network transfer is
    // running [ie we're "busy" if such a transfer IS running
    INPROGRESS(rte, reply, !q && (fromnet(ctm) || tonet(ctm)))

    if ( q ) {
        reply << "0 : " << rte.itcp_id;
    }
    else {
        rte.itcp_id = OPTARG(1, args);
        reply << "0";
    }

    reply << " ;";
    return reply.str();
}

