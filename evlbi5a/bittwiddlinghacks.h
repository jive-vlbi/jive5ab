// Helpful bit-manipulationtricks from Bit Twiddling Hacks
#ifndef JIVE5A_BITTWIDDLINGHACKS_H
#define JIVE5A_BITTWIDDLINGHACKS_H

#include <stdint.h> // for [u]int<N>_t  types

// 'http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan'
//
// Counting bits set, Brian Kernighan's way
//
// unsigned int v; // count the number of bits set in v
// unsigned int c; // c accumulates the total bits set in v
// for (c = 0; v; c++)
// {
//   v &= v - 1; // clear the least significant bit set
// }
//
// Brian Kernighan's method goes through as many iterations as there are set
// bits. So if we have a 32-bit word with only the high bit set, then it
// will only go once through the loop.
// Published in 1988, the C Programming Language 2nd Ed. (by Brian W.
// Kernighan and Dennis M. Ritchie) mentions this in exercise 2-9. On April
// 19, 2006 Don Knuth pointed out to me that this method "was first published
// by Peter Wegner in CACM 3 (1960), 322. (Also discovered independently by
// Derrick Lehmer and published in 1964 in a book edited by Beckenbach.)"
template <typename T>
inline unsigned int count_bits(T v) {
    unsigned int c = 0;
    while( v ) {
        c++;
        v &= (v-1);
    }
    return c;
}

// Found on http://en.wikipedia.org/wiki/Hamming_weight
// Another bitcounting method specialized for 64bit ints
// (which are the ones we tend to have. cool).
// Constant time irrespective of number-of-bits set.
// The generic implementation loses when > 17 bits are set
// (or thereabouts) since this algo uses 17 instructions.
// The generic at least as many times as bits are set.
template <>
inline unsigned int count_bits(uint64_t v) {
    static const uint64_t m1  = ((uint64_t)0x55555555 << 32) + 0x55555555;
    static const uint64_t m2  = ((uint64_t)0x33333333 << 32) + 0x33333333;
    static const uint64_t m4  = ((uint64_t)0x0f0f0f0f << 32) + 0x0f0f0f0f;
//    static const uint64_t m8  = 0x00ff00ff00ff00ffull;
//    static const uint64_t m16 = 0x0000ffff0000ffffull;
//    static const uint64_t m32 = 0x00000000ffffffffull;

    v -= (v >> 1) & m1;
    v  = (v & m2) + ((v >> 2) & m2);
    v  = (v + (v >> 4)) & m4;
    v += v >>  8;
    v += v >> 16;
    v += v >> 32;
    return (unsigned int)(v & 0x7f);
}

// Count the number of 'edges' or transitions from 0->1 and 1->0 when
// inspecting the bits in a word from left-to-right.
//
// Each transition gets counted so an input of "1101" (binary)
// yields '2': one '1->0' transition and one '0->1'.
//    const T      otz( ((v >> 1) & ~v) << 1 );
//    const T      zto( (v << 1) & ~v );
// We put '1's in the positions where, in <v>, there was a '1' followed
// by a '0' at the right-hand-side. Secondly, we put '1's at those positions
// in <v> where there was a '0' followed by a '1' towards the
// right-hand-side. They are mutually exclusive so we can easily bitwise-or
// them together and count the number of bits set in the result.
template <typename T>
unsigned int count_edges(const T v) {
    // force compiler to use same type as given in the argument.
    // Otherwise it expands smaller types to bigr types?! WTF!
    const T otz(((v>>1)&~v)<<1);
    const T zto((v<<1)&~v);
    return count_bits( otz|zto );
}

#endif
