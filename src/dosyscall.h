// 'wrapper' around calling systemcalls.
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
// In case of a failed condition, it will throw and will
// automatically add location details as well as the call that failed and
// the system error message (as indicated by errno).
//
// Defines:
//      ASSERT_<cond>( <expression> )  
//                  assertion macros which will throw if
//                  the <expression> does not meet <condition>
//      ASSERT2_<cond>( <expression>, <cleanupcode> )
//                  id. as above but will execute <cleanupcode> immediately 
//                  before throwing
#ifndef EVLBI5A_DOSYSCALL_H
#define EVLBI5A_DOSYSCALL_H

#include <iostream>
#include <sstream>
#include <string>
#include <exception>
#include <errorqueue.h>


// the constructor of this baby captures 'errno' and the associated message
struct lastsyserror_type {
    lastsyserror_type();

    int         sys_errno;
    std::string sys_errormessage;
};

// an exception of this type is thrown
struct syscallexception :
    public std::exception
{
    syscallexception( const std::string& s );

    virtual const char* what() const throw();
    virtual ~syscallexception() throw();

    const std::string msg;
};

// if errno == 0, don't show anything
std::ostream& operator<<( std::ostream& os, const lastsyserror_type& lse );



// Set up the defines to make it all readable (ahem) and usable
#define SYSCALLLOCATION \
    std::string  fn_( __FILE__); int ln_(__LINE__);

#define SYSCALLSTUFF(fubarvar) \
    lastsyserror_type lse; std::ostringstream lclSvar_0a;\
    lclSvar_0a << fn_ << "@" << ln_ << " [" << fubarvar << "] fails " << lse;

// SCINFO [short for SysCallInfo]:
// can be used to add extra info to the errormessage. Use as (one of) the
// entries in the ASSERT2_*() macros: eg:
//
// int       fd;
// string    proto;
// 
// <... open file and store fd ...>
//
// ASSERT2_NZERO( getprotobyname(proto.c_str()),    // see if this works
//                ::close(fd); SCINFO("proto.c_str()="<<proto.c_str()) ); // if not, execute this
//
#define SCINFO(a) \
    lclSvar_0a << a;

// If you want to save the returnvalue of the function 
// that can be easily done via:
//
//  int fd;
//
//  ASSERT_POS( (fd=open("<some>/<path>/<to>/<file>", O_RDONLY)) );
//  read(fd, <buffer>, <size>);
//
// NOTE: You should use ()'s around the assignment...


// Generic condition; if expression !(a) evaluates to true
// (ie: (a) evaluates to false, ie 'condition not met')
// then throw up
//
// The ASSERT2_* defines take *two* arguments; the 2nd expression will be evaluated/executed
// just before the exception is thrown. It is for cleanup code:
//
// int     fd;
// char*   sptr;
//
// // open a file
// ASSERT_POS( (fd=open("/some/file", O_RDONLY)) );
// // alloc memory, close file in case of error
// ASSERT2_NZERO( (sptr=(char*)malloc(hugeval)), close(fd) );
//
#define ASSERT2_COND(a, b) \
    do {\
        SYSCALLLOCATION;\
        if( !(a) ) { \
            SYSCALLSTUFF(#a);\
            b;\
            throw syscallexception( lclSvar_0a.str() ); \
        } \
    } while( 0 );
// w/o cleanup is just "with cleanup" where the cleanup is a nop
#define ASSERT_COND(a) \
    ASSERT2_COND(a, ;)


//
// Now define shortcuts for most often used conditions
// 

// For systemcalls that should return 0 on success
#define ASSERT2_ZERO(a, b) \
    do {\
        SYSCALLLOCATION;\
        if( (a)!=0 ) { \
            SYSCALLSTUFF(#a);\
            b; \
            throw syscallexception( lclSvar_0a.str() ); \
        } \
    } while( 0 );

#define ASSERT_ZERO(a) \
    ASSERT2_ZERO(a, ;)

// For functions that should NOT return zero
// (note: you can also stick functions in here that
//  return a pointer. Long live C++! ;))
#define ASSERT2_NZERO(a, b) \
    do {\
        SYSCALLLOCATION;\
        if( (a)==0 ) { \
            SYSCALLSTUFF(#a);\
            b; \
            throw syscallexception( lclSvar_0a.str() ); \
        } \
    } while( 0 );
#define ASSERT_NZERO(a) \
    ASSERT2_NZERO(a, ;)

// functions that should not return a negative number
// Note: 0 is not an error in this case (eg semget(2))
#define ASSERT2_POS(a, b) \
    do {\
        SYSCALLLOCATION;\
        if( (a)<0 ) { \
            SYSCALLSTUFF(#a);\
            b; \
            throw syscallexception( lclSvar_0a.str() ); \
        } \
    } while( 0 );
#define ASSERT_POS(a) \
    ASSERT2_POS(a, ;)


#endif // includeguard
