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


// set/qre the debuglevel
string debuglevel_fn(bool qry, const vector<string>& args, runtime&) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";
    if( qry ) {
        reply << "0 : " << dbglev_fn() << " : " <<  fnthres_fn() << " ;";
        return reply.str();
    }
    // if command, we must have an argument
    if( args.size()<2 ) {
        reply << " 8 : Command must have argument ;";
        return reply.str();
    }

    // (attempt to) parse the new debuglevel  
    // from the argument. No checks against the value
    // are done as all values are acceptable (<0, 0, >0)
    int    lev;
    string s;

    if( (s=OPTARG(1, args)).empty()==false ) {
        ASSERT_COND( (::sscanf(s.c_str(), "%d", &lev)==1) );
        // and install the new value
        dbglev_fn( lev );
    }
    if( (s=OPTARG(2, args)).empty()==false ) {
        ASSERT_COND( (::sscanf(s.c_str(), "%d", &lev)==1) );
        // and install the new value
        fnthres_fn( lev );
    }
    reply << " 0 ;";

    return reply.str();
}
