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


string layout_fn(bool q,  const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    if ( !q ) {
        reply << "6 : " << args[0] << " only implemented as query, use reset=erase:<layout> to force a new layout ;";
        return reply.str();
    }

    // Query only allowed if disks available
    INPROGRESS(rte, reply, diskunavail(rte.transfermode))

    reply << "0 : " << rte.xlrdev.userDirLayoutName() << " ;";
    return reply.str();
}
