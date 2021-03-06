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
#include <playpointer.h>

using namespace std;

// the exception
DEFINE_EZEXCEPT(playpointerexception)

// default c'tor gives '0'
playpointer::playpointer() :
    AddrHi( data.parts[1] ), AddrLo( data.parts[0] ), Addr( data.fulladdr ) // (*)
{
    data.fulladdr = 0;
}


// copy. Be sure to copy over only the datavalue, our references
// should refer to our *own* private parts
playpointer::playpointer( const playpointer& other ):
    AddrHi( data.parts[1] ), AddrLo( data.parts[0] ), Addr( data.fulladdr ) // (*)
{
    data.fulladdr = other.data.fulladdr;
}

// create from value. does round to multiple of eight
playpointer::playpointer( const uint64_t& t ):
    AddrHi( data.parts[1] ), AddrLo( data.parts[0] ), Addr( data.fulladdr ) // (*)
{
    data.fulladdr = (t & (uint64_t)(~0x7));
}



// assignment -> implement it to make sure that our references
// are not clobbered [we only copy the datavalue from other across,
// we leave our own reference-datamembers as-is]
const playpointer& playpointer::operator=( const playpointer& other ) {
    if( this!=&other ) {
        data.fulladdr = other.data.fulladdr;
    }
    return *this;
}

int64_t operator-( const playpointer& x, const playpointer& y ) {
    return (int64_t)x.Addr - (int64_t)y.Addr;
}

// be able to compare playpointer objects
bool operator<(const playpointer& l, const playpointer& r) {
    return (l.Addr<r.Addr);
}

bool operator<=(const playpointer& l, const playpointer& r) {
    return !(r<l);
}
bool operator==(const playpointer& l, const playpointer& r) {
    return !(l<r || r<l);
}
bool operator>(const playpointer& l, const playpointer& r) {
    return r<l;
}
bool operator>=(const playpointer& l, const playpointer& r) {
    return !(l<r); 
}

// show in HRF
ostream& operator<<(ostream& os, const playpointer& pp) {
    return os << pp.Addr;
}
