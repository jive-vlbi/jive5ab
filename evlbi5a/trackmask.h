// compress a chunk of memory by throwing away a constant set of bits from
// every 64 bit word
// Copyright (C) 2009-2010 Bob Eldering (re-implementation by Harro Verkouter)
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
// Author:  Bob Eldering - eldering@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo

#ifndef TRACK_MASK_H
#define TRACK_MASK_H

// include everything we really need
#include <vector>
#include <list>
#include <set>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>

// for abs(3) and system(3)
#include <stdlib.h>

#include <stdint.h> // for [u]int<N>_t  types

// dynamic library loading
#include <dlfcn.h>

// jive5a utilities
#include <hex.h>
#include <bin.h>
#include <dosyscall.h>
#include <evlbidebug.h>
#include <variable_type.h>
#include <bittwiddlinghacks.h>

// These define our basic datatype for the compression
typedef uint64_t               data_type;
extern const std::string       data_type_name;
extern const data_type         trackmask_empty;
extern const data_type         trackmask_full;

// Forward 
struct solution_type;
struct step_type;

// The compression consists of series of bitwise operations, steps.
// This describes one step in such a series
// A step moves the bits with '1' in <mask_from> to another position
// in another word by shifting by <s> bits. 
struct step_type {
    // default constructed objects of this type.
    step_type();

    // define operator bool(): it returns true if this
    // step actually moves some bits. false otherwise
    operator bool() const;

    // Generate a human-readable summary of this step (uncompilable code)
    std::string decompress_code( void ) const;
    std::string compress_code( void ) const;

    // These variants generate compilable C code.
    // The methods take data from an input variable and move bits to the
    // outputvariable. Caller's call to decide what they refer to.
    std::string compress_code(const variable_type& source, const variable_type& dest) const;
    std::string decompress_code(const variable_type& source, const variable_type& dest) const;
    std::string decompress_code(const variable_type& source, const variable_type& dest,
                                const variable_type& tmpdest, const data_type mask,
                                const int signmagdistance) const;


    int          shift;
    bool         inc_dst;
    bool         dec_src;
    data_type    mask_from;
    data_type    mask_to;
    unsigned int bits_moved;

    // constructor for make_step: specifically create with
    // masks and shift
    step_type(data_type mask_f, data_type mask_t, int s);

    // constructor for solution_type:
    // copy the bitmasks & shifts, the solutiontype fills in the
    // dst_inc and src_dec [since it keeps track of wether or
    // not the source - after applying this step - is empty
    // and/or the desitionation is full]
    step_type(const step_type& other, bool src, bool dst);
};
std::ostream& operator<<(std::ostream& os, const step_type& s);

// the container we use to store the steps
typedef std::vector<step_type>  steps_type;


// 
// A solution is a series of steps such that:
//   given a bitmask M, with at least one and at most number-of-bits-datatype
//   less one '1's,
//
// copy M into the accumulator A. now fill the positions in A which have a '0'
// with bits from M that have a '1' by shift+bitwise-or'ing (*).
// the sequence ends when no more '1's remain in M and at the same time A
// consists of only '1's. If M is empty but A is not all '1's yet, refill M
// with the original mask and continue.
//
// (*) the algorithm attempts to minimize the number of steps it needs in
// order to move the bits from M to A. One such algorithm just picked the
// maximum amount of bits it could transfer at each step after having tried
// all (-nbit+1 -> nbit) possible shifts of M wrt A and checking which bits
// of the (shifted) M can be transferred to A, eg:
//
// Start:
//   M   11010
//   A   11010  => ~A = 00101
// 
//   possibilities:                                             (1)
//       // M<<4 yields 00000 so no point checking this
//       M<<3 = 10000 => (M<<3 & ~A) = 00000  moves 0 bits
//       M<<2 = 01000 => (M<<2 & ~A) = 00000  moves 0 bits
//       M<<1 = 10100 => (M<<2 & ~A) = 00100  moves 1 bit
//       M    = 11010 =>  M & ~A     = 00000  moves 0 bits
//       M>>1 = 01101 => (M>>1 & ~A) = 00101  moves 2 bits (#)
//       M>>2 = 00110 => (M>>2 & ~A) = 00100  moves 1 bit
//       M>>3 = 00011 => (M>>3 & ~A) = 00001  moves 1 bit
//
// So the algorithm would choose option (#) since that moves
// two bits from M into A. The outcome of (M>>1 & ~A) indicates the
// positions in A where the two bits of M are AFTER they have been
// moved by a righ-shift of 1 (">> 1").
// We must also know whence these bits came in M.
// Fortunately we know that the shift in option (#) was ">> 1",
// which means that the original positions of those bits was "<< 1".
// So option (#) would take bits 01010 [outcome of (#) left-shifted by one]
// of M and move them into position 00101 of A by bitwise-or'ing.
//
//  init:       A = 11010    M = 11010
//  apply (#):  
//                * take bits 01010 from M:    tmp = M & 01010    [=01010]
//                * move to desitination:      tmp >> 1           [=00101]
//                * add those to A:            A |= tmp           [=11111]
//                * remove the bits from M:    M &= ~tmp          [=10000]
//  result:     A = 11111    M = 10000
//
//  So we're not done yet. A consists of all '1's but M is not all '0's at
//  the same time. So the algorithm continues by resetting A to the original
//  value of M (11010) and restarts the search as shown under (1).
//  Repeat until stopcondition met.
struct solution_type {
    // show a HRF summary on a stream
    friend std::ostream& operator<<(std::ostream&, const solution_type&);


    // empty solution
    solution_type();
    // This one will only yield complete()==true when
    // the steps have gone through a full cycle, meaning
    // the input consists of only '0's AND the output consists
    // of only '1's
    solution_type(data_type bitstokeep);
    // This solution will yield complete()==true when
    // all bits from <bitstomove> have been moved into
    // the word <intoword> (dropping the additional
    // <intoword>-must-consist-of-only-1s constraint).
    // Typically used for searching partial solutions.
    solution_type(data_type bitstomove, data_type intoword);

    // return the current state of the solution
    inline data_type  source( void ) const {
        return mask_in;
    }

    inline data_type  dest( void ) const {
        return mask_out;
    }

    // Is the solution empty? If there are no steps in this solution we take
    // this to mean it is not a solution to anything, even if the in/out
    // (aka source/dest) masks are NOT at their default value. Fact of the
    // matter is that "no steps" => "it don't do nuttin'" ...
    inline operator bool( void ) const {
        return (steps.size()>0);
    }

    // the compressionfactor: number of outputwords over total number of
    // inputwords.
    inline double compressionfactor( void ) const {
        return (cycle()==0 || !complete())?(0.0):((double)n_dstinc/cycle());
    }

    // compute the compressed size if a block of size 'inputsize' words is
    // compressed by this very solution.
    // Note: units here are words WORDS! Each one of this is of size
    // "sizeof(data_type)" bytes. 
    unsigned int outputsize( unsigned int insize ) const;
    unsigned int inputsize( unsigned int outsize ) const;

    // tells us wether this solution is complete.
    // complete meaning: there are no bits left
    // in the input mask (mask_in) because that means
    // that ALL bits that were set in mask_in
    // have been transferred to other positions.
    // Once a solution is complete it refuses to add more
    // steps.
    bool complete( void ) const;

    // one line of summary about this solution
    std::string summary( void ) const;

    // determine the quality of this solution.
    // lower number means higher quality
    inline int quality( void ) const {
        return q_value;
    }
    void sanitize( void );

    // determine if one solution is logically equivalent to another
    bool operator==(const solution_type& other) const;

    void add_step( const step_type& s );

    solution_type operator+(const step_type& s) const;


    // for code-generation purposes
    //   iterators to the actual steps, the mask of bits to
    //   keep and the size of the cycle and the "natural" compressed 
    //   size - given "cycle()" words of input it will yield
    //   "compressed_cycle()" of output words
    data_type                  mask( void ) const;
    unsigned int               cycle( void ) const;
    unsigned int               compressed_cycle( void ) const;

    // effing iterat0rs. gawd. if there's ANYTHING which is teh suxx0rz of
    // c++ then iterators are it. GVD!
    inline steps_type::iterator               begin( void )  {return steps.begin(); };
    inline steps_type::iterator               end( void )    {return steps.end();   };
    inline steps_type::reverse_iterator       rbegin( void ) {return steps.rbegin();};
    inline steps_type::reverse_iterator       rend( void )   {return steps.rend();  };
    inline steps_type::const_iterator         begin( void ) const  {return steps.begin(); };
    inline steps_type::const_iterator         end( void ) const    {return steps.end();   };
    inline steps_type::const_reverse_iterator rbegin( void ) const {return steps.rbegin();};
    inline steps_type::const_reverse_iterator rend( void ) const   {return steps.rend();  };


    private:
        // a solution goes in a number of steps from mask-in to mask-out
        int         q_value;
        bool        full_cycle;
        data_type   mask_in;
        data_type   mask_out;
        data_type   trackmask;
        steps_type  steps;

        // bookkeeping for solution quality
        unsigned int n_dstinc;
        unsigned int n_srcdec;
        unsigned int n_bits_moved;

        int         compute_quality( void ) const;

};
std::ostream& operator<<(std::ostream& os, const solution_type& s);
bool operator<(const solution_type& l, const solution_type& r);

// how do we aggregate solutions?
typedef std::set<solution_type> solutions_t;

// Entry point for compressionsolver. Give it a bitmask with 1s in the
// bitpositions you want to keep.
// Attempts to return a solution for just that.
solution_type solve(data_type trackmask);
solution_type solve(const solution_type& in, unsigned int niter);

// generate compress/decompress code using the solution to compress
// numwords of data_type. The generated sourcecode could be compiled
// and loaded into the running executable. Yummie.
std::string generate_code(const solution_type& solution, const unsigned int numwords,
                          const bool cmprem, const int signmagdistance);


// this is a struct meant for bookeeping rather than for other means - it
// acts as high-level interface tying all "implementation details" together.
struct compressor_type {
    // Functionprototype for compress and decompress functions
    typedef data_type* (*fptr_type)(data_type*);

    // unload any existing compressionfunctions and return to empty state
    // [compression/decompression will do nothing]
    compressor_type();

    // Constructing a compressortype will find a solution, generate the code
    // for it, compile it and load it (if it's different from what is
    // already loaded). It does not return. *If* it returns everything went
    // well. Otherwise it throws an exception.
    compressor_type(const data_type trackmask, const unsigned int numwords,
                    const int signmagdistance);
    // also, if you already *had* a solution - you can generate+load the
    // code for those too.
    compressor_type(const solution_type& solution, const unsigned int numwords,
                    const bool cmprem, const int signmagdistance);

    // delegate to the loaded functions or crash.
    data_type* compress(data_type* p) const;
    data_type* decompress(data_type* p) const;


    private:
        void                do_it(const solution_type& solution, const unsigned int numwords,
                                  const bool cmprem, const int signmagdistance);

        // told you: the bookkeeping stuff
        static void*        handle;
        static data_type    lastmask;
        static unsigned int blocksize;
        static fptr_type    compress_fn;
        static fptr_type    decompress_fn;
        static int          lastsignmagdistance;

        static data_type*   do_nothing(data_type* p);
};


#endif
