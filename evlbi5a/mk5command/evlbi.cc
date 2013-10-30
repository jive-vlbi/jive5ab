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


string evlbi_fn(bool q, const vector<string>& args, runtime& rte ) {
    string        fmt("total : %t : loss : %l (%L) : out-of-order : %o (%O) : extent : %R");
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) << " 0 : ";
    if( !q ) {
        unsigned int                   n;
        ostringstream                  usrfmt;
        vector<string>::const_iterator vs = args.begin();
        if( vs!=args.end() )
            vs++;
        for( n=0; vs!=args.end(); n++, vs++ )
            usrfmt << (n?" : ":"") << *vs;
        fmt = usrfmt.str();
    }
    reply << fmt_evlbistats(rte.evlbi_stats, fmt.c_str()) << " ;";
    return reply.str();
}
