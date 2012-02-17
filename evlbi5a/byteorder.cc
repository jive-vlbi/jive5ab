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
#include <byteorder.h>
#include <map>
#include <sstream>

using namespace std;

byteorder_t getHostByteOrder( void ) {
    const int     sz( sizeof(unsigned int) );

    typedef union _test {
        unsigned int    l;
        unsigned char   c[ sz ];
    } test;

    test         t;
    byteorder_t  retval( unknownOrder );

    // set the 'unsigned int' member to 1 so we can
    // test in which byte the one ends up
    t.l = 0x1;

    // Let's figure out what kinda host this is
    if( (t.c[(sz-1)]==0x1) ) {
        // the '0x1' ended up in the high-order-byte
        retval = bigEndian;
    } else if( t.c[0]==0x1 ) {
        // the '0x1' ended up in the lowest order byte
        retval = littleEndian;
    }
    return retval;
}

//
// The swap-stuff
//
// NOTE NOTE NOTE NOTE
//
// These implementations are definitely not
// the fastest around but (hopefully) the easiest
// to read and portable...
//

// For 2-byte quantities there's little overhead in
// always using a copy.. presumably testing whether
// or not to use a temporary takes as long
// as copying the stuff across... :)
//
// Anyway... all of these 'zwabbers' are hopefully "smart"
// enough to figure out wether or not it is an
// in-place swap (ie use a temporary) or not..
//
// Update - the zwabbers used to be smart enough
//          to detect even overlapping swaps (src-area
//          and dst-area partially overlapping). However,
//          this seems very unlikely to be able to happen...
//          [can't think of a case where this could be
//           useful/desired]. In short: *that* specific
//          smartness was removed. Still (attempts to) detect
//          and support in-place swaps.
//
// Update 2:
//   HV: 03/09/2004 - Removed *all* smartness from these things.
//                    It's taken care of at a different level.
//
void zwabber<(uint64_t)2>::operator()( void* dst, const void* src ) const {
    unsigned char*         dp = static_cast<unsigned char*>(dst);
    const unsigned char*   cp = static_cast<const unsigned char*>(src);

    dp[0] = cp[1];
    dp[1] = cp[0];
    return;
}

void zwabber<4>::operator()( void* dst, const void* src ) const {
    unsigned char*         dp = static_cast<unsigned char*>(dst);
    const unsigned char*   cp = static_cast<const unsigned char*>(src);

    dp[0] = cp[3];
    dp[1] = cp[2];
    dp[2] = cp[1];
    dp[3] = cp[0];

    return;	
}

// 8-byte quantities
void zwabber<8>::operator()( void* dst, const void* src ) const {
    unsigned char*         dp = static_cast<unsigned char*>(dst);
    const unsigned char*   cp = static_cast<const unsigned char*>(src);
    dp[0] = cp[7];
    dp[1] = cp[6];
    dp[2] = cp[5];
    dp[3] = cp[4];
    dp[4] = cp[3];
    dp[5] = cp[2];
    dp[6] = cp[1];
    dp[7] = cp[0];

    return;	
}

// Need specializations for the mover stuff.
// Could possibly be more intelligent than
// this...
void mover<2>::operator()( void* dst, const void* src ) const {
    ::memmove(dst, src, 2 );
}
void mover<4>::operator()( void* dst, const void* src ) const {
    ::memmove(dst, src, 4 );
}
void mover<8>::operator()( void* dst, const void* src ) const {
    ::memmove(dst, src, 8 );
}


// the zwab operator()
void zwab::func( void* dst, const void* src,
        uint64_t n ) const
{
    switch( n ) {
        case 2:
            zwabber<2>()(dst, src);
            break;

        case 4:
            zwabber<4>()(dst, src);
            break;

        case 8:
            zwabber<8>()(dst, src);
            break;

        default:
            ostringstream s;
            s << "zwab::operator()(void*, const void*, " << n << ")/Unsupported "
                << "number of bytes to swap!";
            throw( s.str().c_str() );
    }
}

// and the move operator()
void move::func( void* dst, const void* src,
        uint64_t n ) const
{
    // if src and dst are the same (and we are the mover!!)
    // there's really nothing to be done...
    if( dst==src )
        return;

    switch( n ) {
        case 2:
            mover<2>()(dst, src);
            break;

        case 4:
            mover<4>()(dst, src);
            break;

        case 8:
            mover<8>()(dst, src);
            break;

        default:
            ostringstream s;
            s << "move::operator()(void*, const void*, " << n << ")/Unsupported "
                << "number of bytes to move!";
            throw( s.str().c_str() );
    }
}

// The endian_converter thingy
endian_converter::endian_converter() {}

endian_converter::endian_converter( byteorder_t dst, byteorder_t src ) {
    if( dst==mimicHost )
        dst = getHostByteOrder();
    if( src==mimicHost )
        src = getHostByteOrder();
    if( src==unknownOrder || dst==unknownOrder )
        throw( "endian_converter(dst, src)/ Either dst||src == unknownOrder" );

    if( src==dst )
        myac = new move();
    else
        myac = new zwab();
}

endian_converter::~endian_converter() {}

template <>
void endian_converter::operator()<char>( char& ) const
{}

template <>
void endian_converter::operator()<char>( char& d, const char& s ) const {
    (&d!=&s) && (d=s);
    return;
}

template <>
void endian_converter::operator()<char>( char* d, const char* s, unsigned int n ) const {
    (d!=s) && ::memmove(d, s, n);
    return;
}

template <>
void endian_converter::operator()<unsigned char>( unsigned char& ) const
{}

template <>
void endian_converter::operator()<unsigned char>( unsigned char& d, const unsigned char& s ) const {
    (&d!=&s) && (d=s);
    return;
}

template <>
void endian_converter::operator()<unsigned char>( unsigned char* d, const unsigned char* s, unsigned int n ) const {
    (d!=s) && ::memmove(d, s, n);
    return;
}

ostream& operator<<( ostream& os, const byteorder_t& o ) {
    switch( o ) {
        case bigEndian:
            os << "BE";
            break;

        case littleEndian:
            os << "LE";
            break;

        case mimicHost:
            os << "MimicHost[" << getHostByteOrder() << "]";
            break;

        case unknownOrder:
            os << "??";
            break;

        default:
            os << "<INVALID BYTE ORDER>";
            break;
    }
    return os;
}
