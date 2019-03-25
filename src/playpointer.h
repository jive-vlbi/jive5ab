// for the streamstor we must separate a 64bit bytenumber in 2*32bit words
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
#ifndef EVLBI5A_PLAYPOINTER_H
#define EVLBI5A_PLAYPOINTER_H

#include <iostream>

#include <stdint.h> // for [u]int<N>_t  types

#include <ezexcept.h>

DECLARE_EZEXCEPT(playpointerexception)

// Union of 1 64bit and 2*32bit values
// *could* eventually smarten this up
// to automagically detect MSB/LSB ordering
// (so .high and .low actually point at the 
//  right part)
// The lines with (*) in the comment are the affected lines
// (see the .cc file)
//
// The values are automatically *truncated* to an
// integral multiple of 8 since that is required
// by the streamstor. Might as well enforce it in here...
struct playpointer {
    public:
        // default c'tor gives '0'
        playpointer();

        // copy. Be sure to copy over only the datavalue, our references
        // should refer to our *own* private parts
        playpointer( const playpointer& other );

        // create from value, as long as it's interpretable as uint64_t
        // does round to multiple of eight!
        playpointer( const uint64_t& t );
        /*
        template <typename T>
        playpointer( const T& t ):
            AddrHi( data.parts[1] ), AddrLo( data.parts[0] ), Addr( data.fulladdr ) // (*)
        {
            uint64_t v = (uint64_t)t;
            data.fulladdr = (v & ~0x7);
        }*/

        // assignment -> implement it to make sure that our references
        // are not clobbered [we only copy the datavalue from other across,
        // we leave our own reference-datamembers as-is]
        const playpointer& operator=( const playpointer& other );

        // Assignment from any type that's interpretable as uint64_t?
        template <typename T>
        const playpointer& operator=( const T& t) {
            uint64_t  v( t );
            data.fulladdr = (v & (uint64_t)(~0x7));
            return *this;
        }

        // arithmetic
        template <typename T>
        const playpointer& operator+=( const T& t ) {
            uint64_t  v( t );
            data.fulladdr += (v & (uint64_t)(~0x7));
            return *this;
        }

        template <typename T>
        const playpointer& operator-=( const T& t ) {
            uint64_t  v( t );
            if ( data.fulladdr < v ) {
                THROW_EZEXCEPT(playpointerexception, "playpointer subtraction would result in a negative playpointer");
            }
            data.fulladdr -= (v & (uint64_t)(~0x7));
            return *this;
        }

        // references as datamembers... Brrr!
        // but in this case they're pretty usefull :)
        // The constructors will let them reference
        // the correct pieces of information.
        uint32_t&   AddrHi;
        uint32_t&   AddrLo;
        uint64_t&   Addr;

    private:
        union {
            uint32_t parts[2];
            uint64_t fulladdr;
        } data;
};

// be able to compare playpointer objects
// only the '<' is rly implemented, the other comparisons
// are constructed from using this relation
bool operator<(const playpointer& l, const playpointer& r);
bool operator<=(const playpointer& l, const playpointer& r);
bool operator==(const playpointer& l, const playpointer& r);
bool operator>(const playpointer& l, const playpointer& r);
bool operator>=(const playpointer& l, const playpointer& r);

// show in HRF
std::ostream& operator<<(std::ostream& os, const playpointer& pp);

// diff
int64_t operator-( const playpointer& x, const playpointer& y );


#endif
