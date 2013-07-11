// Implementation of the errorqueue
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
//
#include <errorqueue.h>
#include <timewrap.h>
#include <iostream>
#include <queue>
#include <exception>
#include <string>
#include <sstream>

#include <errno.h>
#include <string.h>
#include <pthread.h>

using namespace std;

// We need our own exception
struct error_queue_exception:
    public exception
{
    error_queue_exception(const string& m):
        msg( "[error_queue_exception] "+m )
    {}
    virtual const char* what( void ) const throw() {
        return msg.c_str();
    }
    virtual ~error_queue_exception( void ) throw () {}
    const string msg;
};


// The actual queue of errors + a mutex for protection
typedef queue<error_type> error_queue_type;
static error_queue_type   error_queue;
static pthread_mutex_t    queue_mtx = PTHREAD_MUTEX_INITIALIZER;


namespace lcl0 {
    struct scoped_lock_type {
        scoped_lock_type(pthread_mutex_t* mtx, const char* f, int l) throw() :
            line( l ), file( f ), mtxPtr( mtx )
        {
            int   r;
            if( !mtxPtr ) {
                ostringstream oss;
                oss << file << "@" << line << " "
                    << "Creating scoped lock type with NULL pointer!" << endl;
                throw ::error_queue_exception( oss.str() );
            }
            if( (r=::pthread_mutex_lock(mtxPtr))!=0 ) {
                ostringstream oss;
                oss << file << "@" << line << " "
                    << "Failed to lock mutex - " << ::strerror(r) << endl;
                throw ::error_queue_exception( oss.str() );
            }
        }

        ~scoped_lock_type() throw() {
            int r;
            if( (r=::pthread_mutex_unlock(mtxPtr))!=0 ) {
                ostringstream oss;
                oss << file << "@" << line << " "
                    << "Failed to unlock mutex - " << ::strerror(r) << endl;
                throw ::error_queue_exception( oss.str() );
            }
        }

        int                 line;
        const char*         file;
        pthread_mutex_t*    mtxPtr;
    };
}

#define LCK   lcl0::scoped_lock_type  slt_0_f00b4r(&queue_mtx, __FILE__, __LINE__)


// Note: we cannot use the ASSERT* macros because then we could end up in an
// infinite loop. The ASSERT* macros will attempt to push on this queue ...
#define ERR(fn, call) do {\
        ostringstream oss; \
        oss << __FILE__ << "@" << __LINE__ << " " << fn << "/" << call \
            << " fails - " << ::strerror(errno); \
        throw ::error_queue_exception(oss.str()); \
    } while( 0 );

struct timeval timestamp( void ) {
    struct timeval  rv;
    if( ::gettimeofday(&rv, 0)==-1 )
        ERR("timestamp()", "gettimeofday()")
    return rv;
}



error_type::error_type():
    number( 0 ), time( timestamp() )
{}

error_type::error_type(int n, const string& m):
    number( n ), message( m ), time( timestamp() )
{
    if( number==0 ) {
        ostringstream oss;
        oss << "error_type::error_type(" << n << ", " << message << ") - invalid to construct "
            << "with error number '0'";
        throw ::error_queue_exception(oss.str());
    }
}

error_type::error_type( const error_type& other ):
    number( other.number ), message( other.message ), time( other.time )
{}

error_type:: operator bool( void ) const {
    return number!=0;
}

ostream& operator<<(ostream& os, const error_type& eo) {
    os << "[error_type] " << eo.number << " - " << eo.message
       << " " << pcint::timeval_type(eo.time);
    return os;
}


void push_error( const error_type& et ) {
    LCK;
    error_queue.push( et );
    return;
}

error_type peek_error( void ) {
    LCK;
    if( error_queue.empty()==false )
        return error_queue.front();
    return error_type();
}

error_type pop_error( void ) {
    LCK;
    if( error_queue.empty()==false ) {
        error_type  et( error_queue.front() );
        error_queue.pop();
        return et;
    }
    return error_type();
}
