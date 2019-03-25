// use this to define/declare exceptions of your own type
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
// All exceptions take "const std::string&" as c'tor arg and are
// derived off of std::exception.
//
// Usagage:
//   in .h file:
//   #include <ezexcept.h>
//   DECLARE_EZEXCEPT(Whatchamacallem);
// 
//   in .cc
//   DEFINE_EZEXCEPT(Whatchamacallem);
//   ...
///  if( usr_fscked_up )
//     throw Whatchamacallem("Usr fscked up!");
#ifndef JIVE5A_EZEXCEPT_H
#define JIVE5A_EZEXCEPT_H

#include <exception>
#include <string>
#include <sstream>

// Declare the prototype of the exception
#define DECLARE_EZEXCEPT(xept) \
    struct xept:\
        public std::exception\
    {\
        xept( const std::string& m = std::string() );\
        virtual const char* what( void ) const throw();\
        virtual ~xept() throw();\
        const std::string __m;\
    };

// For the xtra-lzy ppl: throws + prefixed with
// FILE:LINE
#define THROW_EZEXCEPT(xept, msg) \
    do { int eZ_Lijn_gH = __LINE__; \
         const std::string eZ_Fijl_9q( __FILE__ );\
         std::ostringstream eZ_Stroom_yw;\
        eZ_Stroom_yw << eZ_Fijl_9q << "@" << eZ_Lijn_gH << " - " << msg;\
        throw xept(eZ_Stroom_yw.str());\
    } while( 0 );

// And this is to go in your .cc file
#define DEFINE_EZEXCEPT(xept) \
    xept::xept( const std::string& m ):\
        __m( m )\
    {} \
    const char* xept::what( void ) const throw() {\
        return __m.c_str();\
    }\
    xept::~xept() throw() \
    {}


// Set up the defines to make it all readable (ahem) and usable
#define EZCALLLOCATION \
    std::string  ez_fn_( __FILE__); int ez_ln_(__LINE__);

#define EZCALLSTUFF(fubarvar) \
    std::ostringstream ezlclSvar_0a;\
    ezlclSvar_0a << ez_fn_ << "@" << ez_ln_ << " assertion [" << fubarvar << "] fails ";

// EZINFO 
// can be used to add extra info to the errormessage. Use as (one of) the
// entries in the EZASSERT2_*() macros: eg:
//   EZASSERT2(idx<length,
//             EZINFO("idx " << idx << ">= length " << length));
#define EZINFO(a) \
    ezlclSvar_0a << a;

// The actual assertions:
// The throw 'e' if 'a' is not met, executing 'b' before throwing


// generic assertion "a" 
// [w/o cleanup is just "with cleanup" where the cleanup is a nop]
#define EZASSERT2(a, e, b) \
    do {\
        if( !(a) ) { \
            EZCALLLOCATION;\
            EZCALLSTUFF(#a);\
            b;\
            throw e( ezlclSvar_0a.str() ); \
        } \
    } while( 0 );
#define EZASSERT(a, e) \
    EZASSERT2(a, e, ;)

// assert "a==0"
// [w/o cleanup is just "with cleanup" where the cleanup is a nop]
#define EZASSERT2_ZERO(a, e, b) \
    do {\
        if( !((a)==0) ) { \
            EZCALLLOCATION;\
            EZCALLSTUFF(#a);\
            b;\
            throw e( ezlclSvar_0a.str() ); \
        } \
    } while( 0 );
#define EZASSERT_ZERO(a, e) \
    EZASSERT2_ZERO(a, e, ;)

// assert "a!=0"
// [w/o cleanup is just "with cleanup" where the cleanup is a nop]
#define EZASSERT2_NZERO(a, e, b) \
    do {\
        if( !((a)!=0) ) { \
            EZCALLLOCATION;\
            EZCALLSTUFF(#a);\
            b;\
            throw e( ezlclSvar_0a.str() ); \
        } \
    } while( 0 );
#define EZASSERT_NZERO(a, e) \
    EZASSERT2_NZERO(a, e, ;)

// assert "a>=0" [a is non-negative]
// [w/o cleanup is just "with cleanup" where the cleanup is a nop]
#define EZASSERT2_POS(a, e, b) \
    do {\
        if( !((a)>=0) ) { \
            EZCALLLOCATION;\
            EZCALLSTUFF(#a);\
            b;\
            throw e( ezlclSvar_0a.str() ); \
        } \
    } while( 0 );
#define EZASSERT_POS(a, e) \
    EZASSERT2_POS(a, e, ;)


#endif
