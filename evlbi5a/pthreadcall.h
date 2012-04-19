// wrapper around those perky pthread_* calls that do not set errno and return -1 
//      but *return* an errno instead. 
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
//
// The macros PTHREAD_CALL and PTHREAD2_CALL execute the call and test
// for success. If unsuccesfull they throw an exception; the PTHREAD2_CALL()
// macro executes the <cleanup-code> just before actually throwing.
// Use:
//
// #include <pthreadcall.h>
//
// PTHREAD_CALL( ::pthread_create(&tid, ....) );
// PTHREAD2_CALL( ::pthread_create(...),
//                ::close(filedscriptor); delete [] array; );
#ifndef EVLBI5A_PTHREADCALL_H
#define EVLBI5A_PTHREADCALL_H

#include <sstream>
#include <exception>

// obviously...
#include <pthread.h>
// for ::strerror()
#include <string.h>

struct pthreadexception:
    public std::exception
{
    pthreadexception( const std::string& m ):
        message( m )
    {}
    
    virtual const char* what( void ) const throw() {
        return message.c_str();
    }
    virtual ~pthreadexception( void ) throw()
    {}

    const std::string  message;
};


#ifdef __GNUC__
#define PTCFUNC " in [" << __PRETTY_FUNCTION__ << "]"
#else
#define PTCFUNC ""
#endif

#define PTINFO(a) lclStreAmvar_q8 << a;

#define PTCALLLOCATION \
    std::string  fn_( __FILE__); int ln_(__LINE__);

#define PTHREAD2_CALL( a, b ) \
    do { int  teh_L0k4l = a;\
        if( teh_L0k4l!=0 ) {\
         PTCALLLOCATION;\
             std::ostringstream  lclStreAmvar_q8; \
            lclStreAmvar_q8 << fn_ << ":" << (ln_ - 2) << PTCFUNC << " " <<  #a << " fails - " << ::strerror(teh_L0k4l);\
            b;\
            throw pthreadexception(lclStreAmvar_q8.str());\
        }\
    } while(0)

#define PTHREAD_CALL( a ) \
    PTHREAD2_CALL( a, ; )

// This version does not throw, but 'return (void*)0;'
#define THRD2_CALL( a, b )\
    do {int teh_L0k4l_ = a;\
        if( teh_L0k4l_!=0 ) {\
            PTCALLLOCATION;\
            std::cerr << fn_ << ":" << (ln_ - 2) << PTCFUNC << " " << #a << " fails - " << ::strerror(teh_L0k4l_);\
            b;\
            return (void*)0;\
        }\
    } while( 0 );

#define THRD_CALL( a ) \
    THRD2_CALL( a, ; )

// this one is specific for pthread_mutex_trylock(): throws
// if not successfull because of error. So IF you get past
// this one you know you have either locked succesfully or
// the error was EBUSY/EDEADLK.
// If you want to save the returnvalue use this one as follows:
// int   rv;
// PTHREAD_TRYLOCK( (rv=::pthread_mutex_trylock(&mutex)) );
// return rv;
#define PTHREAD_TRYLOCK( a ) \
    do {int  the_l0c4l_rv = a;\
        if( the_l0c4l_rv!=0 && the_l0c4l_rv!=EBUSY && the_l0c4l_rv!=EDEADLK ) {\
            PTCALLLOCATION;\
            std::ostringstream  lclStreAmvar_q8;\
            lclStreAmvar_q8 << fn_ << ":" << (ln_-2) << PTCFUNC << " " <<  #a << " fails - " << ::strerror(the_l0c4l_rv);\
            throw pthreadexception(lclStreAmvar_q8.str());\
        }\
    } while(0)

// this one is specific for pthread_cond_timedwait(): throws
// if not successfull because of error. So IF you get past
// this one you know you have either waited succesfully or
// the error was ETIMEDOUT.
// If you want to save the returnvalue use this one as follows:
// int   rv;
// PTHREAD_TIMEDWAIT( (rv=::pthread_cond_timedwait(&cond, &mutex, &abstime)) );
// b is the code to execute before throwing an exception 
// (recommendation: unlock the mutex)
#define PTHREAD_TIMEDWAIT( a, b ) \
    do {int  the_l0c4l_rv = a;\
        if( the_l0c4l_rv!=0 && the_l0c4l_rv!=ETIMEDOUT ) {\
            PTCALLLOCATION;\
            std::ostringstream  lclStreAmvar_q8;\
            lclStreAmvar_q8 << fn_ << ":" << (ln_-2) << PTCFUNC << " " <<  #a << " fails - " << ::strerror(the_l0c4l_rv); \
            b;\
            throw pthreadexception(lclStreAmvar_q8.str());\
        }\
    } while(0)

#endif
