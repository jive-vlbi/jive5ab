// Byte-order related stuff / taken from my own PCInt project.
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
#ifndef JIVE5A_BYTEORDER_H
#define JIVE5A_BYTEORDER_H

#include <iostream>
#include <countedpointer.h>

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS 1
#endif
#include <stdint.h> // for [u]int<N>_t  types


typedef enum _byteorder_t {
    // the (for us) important byte-orderings.
    // mimicHost is supposed to be able to be used
    // as a place-holder for the .. well, you can
    // figure that out for yourself.. :)
    bigEndian, littleEndian, mimicHost, unknownOrder
} byteorder_t;
// display it in HumanReadableFormat
std::ostream& operator<<( std::ostream& os, const byteorder_t& o );

// retrieve the hostByteOrder
// Can be:
//   - bigEndian [eg Sun/SPARC,HP]
//   - littleEndian [intel x86]
//   - unknownOrder [ ... ] machines that we cannot determine it for...
//
// NEVER returns mimicHost (can end up in infinite loop otherwise)
byteorder_t getHostByteOrder( void );

// zwabber swaps a number of bytes [the template parameter]
// Default implementation just ain't there.
// (partial) template specializations for recognized- 
// byte-size items exist. Hence, if the usr
// requests an item of unrecognized size to be 
// swapped, it will result in a compile/link error...
template <uint64_t>
struct zwabber { };

// mover just moves a number of bytes.
// see above for recognized byte-size-items discussion etc.
template <uint64_t>
struct mover { };

// This struct acts as an abstract baseclass for accessors. 
// This thang features a virtual private method
// which does the actual hard work.
struct accessor {
    // define the non-virtual interface, takes care of typesafety
    template <typename T>
    void operator()( T& s, const T& t ) const {
        // be smart about in-place processing or not
        // [in-place requires a temporary]
        if( &s==&t ) {
            T   tmp( t );
            this->func((void*)&s, (const void*)&tmp, sizeof(T));
        } else {
            this->func((void*)&s, (const void*)&t, sizeof(T));
        }
        return;
    }

    // process multiple items
    template <typename T>
    void operator()( T* d, const T* s, unsigned int n ) const {
        // cd = current destination
        // cs = current source
        if( d==s ) {
            // if doing an array in-place, only use one tmp-variable
            T        tmp;
            T*       cd = d;
            const T* cs = s;

            while( n-- ) {
                tmp = *cs++;
                this->func(cd++, &tmp, sizeof(T));
            }
        } else {
            T*       cd = d;
            const T* cs = s;

            while( n-- )
                this->func(cd++, cs++, sizeof(T));
        }
        return;
    }

    virtual ~accessor() {}

    private:
        // the actual hard work will be delegated to this
        // type-unsafe function!
        virtual void func( void* dst, const void* src,
                           uint64_t n ) const = 0;
};

struct zwab:
    public accessor
{
    virtual ~zwab() {}
    private:
    virtual void func( void* dst, const void* src,
            uint64_t n ) const;
};

struct move:
    public accessor
{
    virtual ~move() {}
    private:
    virtual void func( void* dst, const void* src,
            uint64_t n ) const;
};

// This is the converter thingy that actually presents the 
// type-safe interface to the user based on desired 
// source+destination byteorder
struct endian_converter {
    public:
        // default c'tor 
        endian_converter();

        // configure for a particular combination of
        // src- and dst-byteorder
        endian_converter( byteorder_t dst, byteorder_t src=mimicHost );

        // define the interface and methods for the converter

        // in-place conversion
        template <typename T>
        void operator()( T& d ) const {
            if( !myac )
                throw( "endian_converter::operator()(d) called on default object" );
            (*myac)(d, d);
            return;
        }

        // operate on a single item
        template <typename T>
        void operator()( T& d, const T& s ) const {
            if( !myac )
                throw( "endian_converter::operator()(d, s) called on default object" );
            (*myac)(d, s);
            return;
        }

        // operate on a number of items
        template <typename T>
        void operator()( T* d, const T* s, unsigned int n ) const {
            if( !myac )
                throw( "endian_converter::operator()(d, s, n) called on default object" );
            (*myac)(d, s, n);
            return;
        }


        // destroy allocated resources
        ~endian_converter();

    private:
        countedpointer<accessor>   myac;
};

// byte-sized items shouldn't be zwabbed ...
// but the must be accepted by the system...
template <>
void endian_converter::operator()<char>( char& d ) const;

template <>
void endian_converter::operator()<char>( char& d, const char& s ) const;

template <>
void endian_converter::operator()<char>( char* d, const char* s, unsigned int n ) const;

template <>
void endian_converter::operator()<unsigned char>( unsigned char& d ) const;

template <>
void endian_converter::operator()<unsigned char>( unsigned char& d, const unsigned char& s ) const;

template <>
void endian_converter::operator()<unsigned char>( unsigned char* d, const unsigned char* s, unsigned int n ) const;


// specializations must be seen by the compiler at compile-time,
// that's why they're listed here
template <>
struct zwabber<(uint64_t)2> {
    void operator()( void* dst, const void* src ) const;
};

template <>
struct mover<(uint64_t)2> {
    void operator()( void* dst, const void* src ) const;
};

// id. for four-byte-sized items
template <>
struct zwabber<(uint64_t)4> {
    void operator()( void* dst, const void* src ) const;
};

template <>
struct mover<(uint64_t)4> {
    void operator()( void* dst, const void* src ) const;
};

// once more for eight-byte-sized items
template <>
struct zwabber<(uint64_t)8> {
    void operator()( void* dst, const void* src ) const;
};

template <>
struct mover<(uint64_t)8> {
    void operator()( void* dst, const void* src ) const;
};
#endif
