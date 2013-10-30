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


// net_port function
string net_port_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));
    if( q ) {
        oss << " 0 : " << np.get_port() << " ;";
        return oss.str();
    }
 
    // only allow command when no transfer is running
    if( rte.transfermode!=no_transfer ) {
        oss << " 6 : Not allowed to change during transfer ;";
        return oss.str();
    } 

    // command better have an argument otherwise 
    // it don't mean nothing
    if( args.size()>=2 && args[1].size() ) {
        unsigned int  m = (unsigned int)::strtol(args[1].c_str(), 0, 0);

        np.set_port( m );
        oss << " 0 ;";
    } else {
        oss << " 8 : Missing argument to command ;";
    }
    return oss.str();
}

