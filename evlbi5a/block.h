// Very thin wrapper around a struct iov_block
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

typedef uint32_t  refcount_type;

DECLARE_EZEXCEPT(block_error)

// pool_type is the struct that will do the allocation
// of usable blocks for us. This file does not depend
// on the implementation/API of pool_type *at all*
// it's just for the friendship stuff
struct pool_type;

struct block {
    friend struct pool_type;

    public:
        // empty block: iov_len == 0 and iov_base == 0
        block();

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
        block(void* base, size_t sz, refcount_type* refcnt);
};

#endif
