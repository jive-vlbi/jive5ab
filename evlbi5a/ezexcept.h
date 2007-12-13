// use this to define/declare exceptions of your own type
// they all take "const std::string&" as c'tor arg and are
// derived off std::exception.
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
        eZ_Stroom_yw << eZ_Fijl_9q << ":" << eZ_Lijn_gH << " - " << msg;\
        throw xept(eZ_Stroom_yw.str());\
    } while( 0 );

// And this is to go in your .cc file
#define DEFINE_EZEXCEPT(xept) \
    xept::xept( const std::string& m ):\
        __m( m )\
    {}; \
    const char* xept::what( void ) const throw() {\
        return __m.c_str();\
    };\
    xept::~xept() throw() \
    {};


#endif
