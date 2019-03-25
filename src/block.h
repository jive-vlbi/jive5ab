// Very thin wrapper around a struct iov_block. Also defines blocklist_type
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
#ifndef JIVE5A_BLOCK_H
#define JIVE5A_BLOCK_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <ezexcept.h>
#include <list>

typedef uint32_t  refcount_type;

DECLARE_EZEXCEPT(block_error)

// In order to make the thread functions accept both containers-of-blocks as
// well as individual blocks we define a 'stl style type API' to which the block
// and container-of-blocks must adhere to.
// IF any type implements that API then threadfunctions can be used on
// that type immediately.
//
// The type 'T' must implement the following:
//
//   typedef T::iterator, typedef T::const_iterator
//
//   [const_]iterator  .begin()
//   [const_]iterator  .end()
//
//   * (T::[const_]iterator) must be '[const ] block&'
//     (ie dereferencing an iterator must yield a block)
//
//  
// So if can make a block look like 1-element container we should
// be good to go!
// All STL containers (std::list, std::vector, ...) already have this
// API/properties so they can be used at will


// pool_type is the struct that will do the allocation
// of usable blocks for us. This file does not depend
// on the implementation/API of pool_type *at all*
// it's just for the friendship stuff
struct pool_type;

struct block {
    friend struct pool_type;

    public:
        // Implementation of the 'stl style type API'
        typedef block*        iterator;
        typedef block const * const_iterator;

        iterator       begin( void );
        const_iterator begin( void ) const;
        iterator       end( void );
        const_iterator end( void ) const;

        // empty block: iov_len == 0 and iov_base == 0
        block();

        // block of requested size. Note that the memory
        // is now managed by the block itself. If refcount
        // drops to zero the mem is returned to the O/S
        // iov_base = pointer to sz number of bytes
        // iov_len  = sz
        // refcnt   = 1
        block( size_t sz );

        // because we're now reference counting we
        // MUST implement copy + assignment
        block(const block& other);
        const block& operator=(const block& other);

        // create a slice of this block -
        // adds another reference to the underlying storage.
        // This only works reliably for blocks/offsets << 4GB.
        // Then again, with 32bit offsets/sizes you won't go
        // much further than that won't you
        block sub(unsigned int offset, unsigned int length) const;

        // convenience function that return true iff the
        // block is empty by what we take to mean empty,
        // ie iov_base==0 AND iov_len==0
        bool empty( void ) const;

        void*   iov_base;
        size_t  iov_len;

        // Also, since we're doing refcountin'
        // we need a proper destructor
        ~block();

    private:
        // do we manage the memory?
        bool           myMemory;

        // The pointer-to-the-refcounter we keep private
        refcount_type* refcountptr;

        // default blocks inc/dec this refcounter
        static refcount_type dummy_counter;

        // this c'tor is private. only blockpool can construct valid bloks
        // NOTE NOTE NOTE NOTE NOTE NOTE
        //    this constructor ASSUMES that the refcount is
        //    already set to (at least) 1!!!!
        // initialized block:
        // point at sz bytes starting from base
        block(void* base, size_t sz, refcount_type* refcnt, bool myMem = false);
};

// Sometimes it is handy to be able to pass a list of blocks in one go
// Because it is a std::list<> it automatically implements the
// 'stl style type API' ...
typedef std::list<block> blocklist_type;


struct miniblocklist_type {
    // Implementation of the 'stl style type API'
    typedef block*        iterator;
    typedef block const * const_iterator;

    miniblocklist_type()
    {}
    miniblocklist_type(const block& b0, const block& b1) {
        arr[0] = b0; arr[1] = b1;
    }
    block   arr[2];

    iterator       begin( void ) {
        return &arr[0];
    }
    const_iterator begin( void ) const {
        return &arr[0];
    }
    iterator       end( void ) {
        return &arr[2];
    }
    const_iterator end( void ) const {
        return &arr[2];
    }
};

#endif
