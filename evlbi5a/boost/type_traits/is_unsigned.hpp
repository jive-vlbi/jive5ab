
//  (C) Copyright Steve Cleary, Beman Dawes, Howard Hinnant & John Maddock 2000.
//  Use, modification and distribution are subject to the Boost Software License,
//  Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt).
//
//  05 Jan 2016 - This file added manually by Harro Verkouter (verkouter@jive.eu)
//                There was no is_unsigned predicate in the version of boost 
//                that we entertain in our code. It's a mild variation on
//                is_integral.hpp so decided to copy + edit
//
//  See http://www.boost.org/libs/type_traits for most recent version including documentation.

#ifndef BOOST_TT_IS_UNSIGNED_HPP_INCLUDED
#define BOOST_TT_IS_UNSIGNED_HPP_INCLUDED

#include <boost/config.hpp>

// should be the last #include
#include <boost/type_traits/detail/bool_trait_def.hpp>

namespace boost {

//* is a type T an [cv-qualified-] unsigned integral type described in the standard (3.9.1p3)
// as an extension we include long long, as this is likely to be added to the
// standard at a later date
#if defined( __CODEGEARC__ )
BOOST_TT_AUX_BOOL_TRAIT_DEF1(is_unsigned,T,__is_unsigned(T))
#else
BOOST_TT_AUX_BOOL_TRAIT_DEF1(is_unsigned,T,false)

BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned char,true)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned short,true)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned int,true)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned long,true)

// Same set of integral types as in boost/type_traits/integral_promotion.hpp.
// Please, keep in sync. -- Alexander Nasonov
#if (defined(BOOST_MSVC) && (BOOST_MSVC < 1300)) \
    || (defined(BOOST_INTEL_CXX_VERSION) && defined(_MSC_VER) && (BOOST_INTEL_CXX_VERSION <= 600)) \
    || (defined(__BORLANDC__) && (__BORLANDC__ == 0x600) && (_MSC_VER < 1300))
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned __int8,true)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned __int16,true)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned __int32,true)
#ifdef __BORLANDC__
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned __int64,true)
#endif
#endif

# if defined(BOOST_HAS_LONG_LONG)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned, ::boost::ulong_long_type,true)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned, ::boost::long_long_type,true)
#elif defined(BOOST_HAS_MS_INT64)
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,unsigned __int64,true)
#endif
        
#ifdef BOOST_HAS_INT128
BOOST_TT_AUX_BOOL_TRAIT_CV_SPEC1(is_unsigned,boost::uint128_type,true)
#endif

#endif  // non-CodeGear implementation

} // namespace boost

#include <boost/type_traits/detail/bool_trait_undef.hpp>

#endif // BOOST_TT_IS_UNSIGNED_HPP_INCLUDED
