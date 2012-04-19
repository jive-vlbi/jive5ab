// convert BCD to numbers and numbers to BCD - little endian only
// Copyright (C) 2007-2011 Harro Verkouter
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
#ifndef JIVE5A_BCD_H
#define JIVE5A_BCD_H

#include <exception>
#include <typeinfo>
#include <sstream>
#include <math.h>
#include <errno.h>

// This construction allows us to keep everything inside a single .h file
// AND compile with "-W -Wall -Werror" and no "defined-but-unused"
// warnings-cum-errors (compiletime) or doubly-defined symbols (linktime).
template <typename T>
static void throw_a_bcd(const std::string& file, int line, const std::string& msg="") {
    struct exc : public std::exception {
        public:
            exc(const std::string& m) throw(): s(m) {}
            virtual const char* what(void) const throw() { return s.c_str(); }
            virtual ~exc() throw() {};
            std::string s;
    };
    std::ostringstream strm;
    strm << file << ":" << line << " [" << typeid(T).name() << "] " << msg;
    throw exc(strm.str());
}
#define THROW_A_BCD(type, msg) throw_a_bcd<type>(__FILE__, __LINE__, msg);

struct negative_argument { };
struct argument_too_big { };
struct not_a_BCD_digit { }; // 0 .. 9 are acceptable BCD digits

// Fill bcd with the BCD representation of n,
// filling BCD characters from the LSB -> MSB
// (so you can easily mask off the last N bytes if required)
template <typename Number, typename BCD>
void bcd(const Number n, BCD& bcd) {
    // assert that the number will fit in the BCD representation
    // we must take the log10 of Number and ceil that
    float              log10number = ::log10f((float)n);
    Number             copy_of_n( n );
    unsigned int       digit;
    const unsigned int n_bcd = sizeof(BCD)*2;

    // log10( <negative number> ) = NaN
    if( std::isnan(log10number) )
        THROW_A_BCD(negative_argument, "negative numbers cannot be represented in BCD format");
    // log10( 0 ) = -inf
    //   but 0 (zero) is perfectly representable in BCD ...
    if( std::isinf(log10number)!=0 )
        log10number = 0.0;

    // Now we can compute how many digits the number will require
    unsigned int n_digit     = (unsigned int)::ceil(log10number);

    // If ceil(log10) == log10 this means that we have exactly 10^x
    // ie we require an extra digit in the representation
    if( n_digit == (unsigned int)log10number )
        n_digit++;
    // Now, if the amount of BCD digits required isn't going fit
    // we throw up. Each byte in bcd can hold two (2) BCD coded
    // digits.
    if( n_digit>n_bcd )
        THROW_A_BCD(argument_too_big, "The number is too big too fit in the available BCD representation");

    // loop until no more digits left in 'n' (the copy we made thereof)
    // or we've used up all our BCD positions
    for(digit=0, bcd=BCD(0); copy_of_n && digit<n_bcd; ++digit, copy_of_n /= 10)
        bcd |= (BCD(copy_of_n%10)<<(digit*4));
}

template <typename BCD, typename Number>
void unbcd(const BCD bcd, Number& n) {
    bool                        shift = true;
    Number                      old_n, decoded_digit;
    unsigned int                digit;
    unsigned char               mask  = 0xf0;
    const unsigned int          n_bcd = (sizeof(BCD)*2);
    unsigned char const * const bytes = (unsigned char*)&bcd;

    // we want to start at the Most-Significant-Nibble
    // of the BCD representation since then our polyexpansion is much
    // simpler
    for(digit=0, n=Number(0); ((old_n=n)==n) && digit<n_bcd; digit++, mask=(unsigned char)(~mask), shift=!shift) {
        if( (decoded_digit=Number(((bytes[(n_bcd-digit-1)/2]&mask)>>(shift?4:0))))>9 )
            THROW_A_BCD(not_a_BCD_digit, "Only digits 0..9 are considered valid BCD digits");
        n = (n*10) + decoded_digit;
        if( n<old_n )
            THROW_A_BCD(argument_too_big, "The BCD-encoded value was too big to fit into the destination");
    }
    return;
}

// decode an array of bytes (holding 2 BCD digits each) into
// an array of individual digits. Caller is responsible that digits points
// at an array of at least ndigit elements
template <typename Number>
void unbcd(unsigned char const* bcd, Number* digits, const unsigned int ndigit) {
    bool          shift = true;
    unsigned char mask  = 0xf0;

    for(unsigned int i=0; i<ndigit; i++, mask=~mask, shift=!shift, (((i&0x1)==0)?bcd++:bcd))
        if( (digits[i] = ((*bcd)&mask)>>(shift?4:0))>9 )
            THROW_A_BCD(not_a_BCD_digit, "Only digits 0..9 are considered valid BCD digits");
    return;
}

// decode all of the BCD digits found in bcd into an array of the individual
// digits. Caller is responsible to make sure that the array pointed to by
// digits can hold all the digits.
template <typename BCD, typename Number>
void unbcd(const BCD bcd, Number* digits) {
    const unsigned int nbcd = 2*sizeof(BCD);
    BCD                mask = (((BCD)0xf) << ((nbcd-1)*4));
    
    for(unsigned int i=0; i<nbcd; i++, mask>>=4)
        if( (digits[i] = (bcd&mask)>>((nbcd - i - 1)*4))>9 )
            THROW_A_BCD(not_a_BCD_digit, "Only digits 0..9 are considered valid BCD digits");
    return;
}

// Inline UNBCD "packed" BCD digits into an array of individual digits.
template <size_t>
struct size_check { };

template <>
struct size_check<(size_t)1> {
    typedef unsigned int  packed_datatype_is_not_1byte_wide;
};
#define UNBCD(packed, unpacked, ndigits) \
    do { \
        size_check<sizeof(*packed)>::packed_datatype_is_not_1byte_wide i = 0, j = 0;\
        while( ndigits>1 ) { \
            unpacked[i+1] = packed[j  ]&0xf;\
            unpacked[i  ] = packed[j  ]>>4;\
            i+=2; j++; ndigits-=2; \
        } \
        if( ndigits ) \
            unpacked[i++] = packed[j]>>4;\
    } while( 0 );


#endif
