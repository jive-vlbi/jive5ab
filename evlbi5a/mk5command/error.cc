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


string error_fn(bool q, const vector<string>& args, runtime& ) {
    error_type    error( pop_error() );
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    // This query can always execute

    if( !q ) {
        reply << " 2 : only available as query ;";
    } else {
        reply << " 0 : ";
        if( error )
           reply << error.number;
        reply << " : " ;
        if( error )
            reply << error.message;
        reply << " : ";
        if( error ) 
           reply << pcint::timeval_type(error.time);
        reply << " : ";
        // If the same error occurred multiple times, append last time and #
        // of occurrences
        if( error && error.occurrences>1 ) 
           reply << pcint::timeval_type(error.time_last);
        reply << " : ";
        if( error && error.occurrences>1 ) 
           reply << error.occurrences;
        reply << " ;";
    }
    
    return reply.str();
}
