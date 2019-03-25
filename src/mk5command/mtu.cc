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


// mtu function
string mtu_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));

    // Query is possible always, command only when nothing is happening
    INPROGRESS(rte, oss, !(q || rte.transfermode==no_transfer))

    if( q ) {
        oss << " 0 : " << np.get_mtu() << " ;";
        return oss.str();
    }

    // command better have an argument otherwise 
    // it don't mean nothing
    if( args.size()>=2 && args[1].size() ) {
        char*              eocptr;
        unsigned long int  m;

        errno = 0;
        m     = ::strtoul(args[1].c_str(), &eocptr, 0);
        EZASSERT2(eocptr!=args[1].c_str() && *eocptr=='\0' &&
                     errno!=EINVAL && errno!=ERANGE && m>=64 && m<=9000,
                  cmdexception, EZINFO("invalid value for the MTU given"))

        np.set_mtu( m );
        oss << " 0 ;";
    } else {
        oss << " 8 : Missing argument to command ;";
    }
    return oss.str();
}
