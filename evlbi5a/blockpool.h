// provide reference counted blocks out of automatically growing pools
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
#ifndef JIVE5A_BLOCKPOOL_H
#define JIVE5A_BLOCKPOOL_H
#include <list>
#include <block.h>
#include <ezexcept.h>

DECLARE_EZEXCEPT(pool_error)
DECLARE_EZEXCEPT(blockpool_error)

// a single pool consists of both memory
// and an array of counters
struct pool_type {
    // yes, I know. struct members are public by default.
    // however, this'un has private parts so to make it 
    // obvious which are pub and which are priv ...
    public:
        // memory allocated but NOT initialized to any
        // particular value
        pool_type(unsigned int bs, unsigned int nb);

        // return empty/default block if none available here
        block get( void );

        void show_usecnt( void ) const;

        ~pool_type();

    private:
        unsigned int       next_alloc;
        refcount_type*     use_cnt;
        unsigned char*     memory;
        const unsigned int nblock;
        const unsigned int block_size;

        // do not support default creation
        // nor copy/assignment
        pool_type();
        pool_type(const pool_type&);
        const pool_type& operator=(const pool_type&);
};

// blockpool preallocates memory in pools of
// size nblock_p_chunk blocks of bs bytes
//
// It starts with one pool and adds more
// as necessary
struct blockpool_type {
    public:
        // create a poolmanager which will create more pools when
        // they seem to run out of reusable block
        // The pools that are created do their allocation
        // for nb blocks of size bs
        blockpool_type(unsigned int bs, unsigned int nb);

        // get  a fresh block
        block get( void );

        void show_usecnt( void ) const;

        ~blockpool_type();

    private:
        typedef std::list<pool_type*>     pool_list;
        typedef pool_list::iterator       pool_pointer_pointer;
        typedef pool_list::const_iterator const_pool_pointer_pointer;

        // the pool's properties
        pool_list             pools;
        const unsigned int    blocksize;
        const unsigned int    nblock_p_pool;
        pool_pointer_pointer  curpool;
};

#endif
