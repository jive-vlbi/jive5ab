// for the streamstor we must separate a 64bit bytenumber in 2*32bit words
#ifndef EVLBI5A_PLAYPOINTER_H
#define EVLBI5A_PLAYPOINTER_H

#include <iostream>


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

        // create from value, as long as it's interpretable as unsigned long long
        template <typename T>
        playpointer( const T& t ):
            AddrHi( data.parts[1] ), AddrLo( data.parts[0] ), Addr( data.fulladdr ) // (*)
        {
            unsigned long long v( t );
            data.fulladdr = (v & ~0x7);
        }

        // assignment -> implement it to make sure that our references
        // are not clobbered [we only copy the datavalue from other across,
        // we leave our own reference-datamembers as-is]
        const playpointer& operator=( const playpointer& other );

        // Assignment from any type that's interpretable as unsigned long long?
        template <typename T>
        const playpointer& operator=( const T& t) {
            unsigned long long  v( t );
            data.fulladdr = (v & ~0x7);
            return *this;
        }

        // arithmetic
        template <typename T>
        const playpointer& operator+=( const T& t ) {
            unsigned long long  v( t );
            data.fulladdr += v;
            return *this;
        }

        // references as datamembers... Brrr!
        // but in this case they're pretty usefull :)
        // The constructors will let them reference
        // the correct pieces of information.
        unsigned long&        AddrHi;
        unsigned long&        AddrLo;
        unsigned long long&   Addr;

    private:
        union {
            unsigned long      parts[2];
            unsigned long long fulladdr;
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


#endif
