// implementation
#include <playpointer.h>

using namespace std;


// default c'tor gives '0'
playpointer::playpointer() :
    AddrHi( data.parts[1] ), AddrLo( data.parts[0] ), Addr( data.fulladdr ) // (*)
{
    data.fulladdr = 0ULL;
}


// copy. Be sure to copy over only the datavalue, our references
// should refer to our *own* private parts
playpointer::playpointer( const playpointer& other ):
    AddrHi( data.parts[1] ), AddrLo( data.parts[0] ), Addr( data.fulladdr ) // (*)
{
    data.fulladdr = other.data.fulladdr;
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
