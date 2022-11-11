// implementation
// Copyright (C) 2007-2008 Harro Verkouter
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
#include <evlbidebug.h>
#include <iostream>
#include <limits>

// Yah we rly need to lock during outputting stuff
// to the screen ...
#include <pthread.h>

// for ::strerror()
#include <string.h>
#include <threadutil.h>

static int             dbglev_val    = 1;
// if msglevel>fnthres_val level => functionnames are printed in DEBUG()
static int             fnthres_val   = 5; 
static pthread_mutex_t evlbidebug_cerr_lock = PTHREAD_MUTEX_INITIALIZER;

int dbglev_fn( void ) {
    return dbglev_val;
}

int dbglev_fn( int n ) {
    int rv = dbglev_val;
    if( n < maxdbglev_fn() )
        dbglev_val = n;
    return rv;
}

int maxdbglev_fn( void ) {
    static const int max_dbg_level = std::numeric_limits<int>::max() - 1;
    return max_dbg_level;
}

int fnthres_fn( void ) {
    return fnthres_val;
}

int fnthres_fn( int n ) {
    int rv = fnthres_val;
    if( n<maxdbglev_fn() )
        fnthres_val = n;
    return rv;
}

void do_cerr_lock( void ) {
    int rv;
    if( (rv=::pthread_mutex_lock(&evlbidebug_cerr_lock))!=0 ) {
        std::cerr << "do_cerr_lock() failed - " << evlbi5a::strerror(rv) << std::endl;
    }
    //std::cerr << "{" << ::pthread_self() << "}" << std::endl;
}
void do_cerr_unlock( void ) {
    int rv;
    //std::cerr << "{" << ::pthread_self() << "}" << std::endl;
    if( (rv=::pthread_mutex_unlock(&evlbidebug_cerr_lock))!=0 ) {
        std::cerr << "do_cerr_unlock() failed - " << evlbi5a::strerror(rv) << std::endl;
    }
}
