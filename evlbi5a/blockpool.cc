// implementation of the blockpool thingies
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
#include <blockpool.h>
#include <stdint.h>
#include <iostream>
#include <atomic.h>
#include <evlbidebug.h>

#include <string.h>

#define CIRCNEXT(cur, sz)   ((cur+1)%sz)
#define CIRCPREV(cur, sz)   CIRCNEXT((cur+sz-2), sz)
#define CIRCDIST(b , e, sz) (((b>e)?(sz-b+e):(e-b))%sz)

using std::cout;
using std::endl;


DEFINE_EZEXCEPT(pool_error)
DEFINE_EZEXCEPT(blockpool_error)

// a single pool consists of both memory
// and an array of counters
// NOTE: we allocate 16 bytes extra because some of the 
// SSE-assembly (sse_dechannelizer*) routines make a habit
// of reading sixteen bytes past the end of the block they're
// processing. If we happen to give the last block in a pool 
// to one of them routines it may or may not crash.
// 16 bytes overhead for a whole pool is acceptable, especially
// if it prevents crash!
pool_type::pool_type(unsigned int bs, unsigned int nb):
    next_alloc(0), use_cnt( new refcount_type[nb] ),
    memory( new unsigned char [bs * nb + 16] ), nblock(nb),
    block_size(bs)
{ 
    uint64_t    bs64( bs ), nb64( nb ); 
    EZASSERT2(nblock>0 && block_size>0,
              pool_error,
              EZINFO("both block_size (" << block_size << ") and nblock (" << nblock << ") should be >0") );
    // Detect overflow of unsigned int 32-bit maximum ...
    // (at some point someone allocated 16 x 256MB + 16 bytes > 4GB
    //  thus causing an overflow ...)
    EZASSERT2((nb64*bs64)<=(uint64_t)UINT_MAX,
              pool_error,
              EZINFO("nblock x blocksize > UINT_MAX! [" << nb << " x " << bs << " > " << UINT_MAX));
    ::memset(use_cnt, 0x0, nblock * sizeof(refcount_type));
}

// return empty/default block if none available here
block pool_type::get( void ) {
    // do we have a free blocks?
    unsigned int    previous_next = next_alloc;
    refcount_type*  c = 0;
    unsigned char*  m = 0;

    // keep on cycling until we hit either a free block
    // or the previous "last_alloc" again, then we give up
    do {
        // this one available?
        if( ::atomic_try_set(&use_cnt[next_alloc], 1, 0) ) {
            c = &use_cnt[next_alloc];
            m = &memory[next_alloc*block_size];
        }
        next_alloc = CIRCNEXT(next_alloc, nblock);
    } while( !c && next_alloc!=previous_next );
    // if we were succesfull in allocat0ring a block ...
#if 0
    if( c&&m ) {
        DEBUG(3, "pool[" << (void*)this << "]: allocated block @" << (void*)m << ", cnt @" << (void*)c << " [" << block_size << "]" << endl);
    } else {
        DEBUG(3, "pool[" << (void*)this << "]: no free block of size " << block_size << endl);
    }
#endif
    return ((c && m)?block(m, block_size, c):block());
}

void pool_type::show_usecnt( void ) const {
    cout << "pool_type[" << (void*)this << " (" << block_size << ")]/";
    for( unsigned int i=0; i<nblock; i++)
        cout << use_cnt[i] << " ";
    cout << endl;
    return;
}

pool_type::~pool_type() {
    const unsigned int maxcount = 100;
    unsigned int       count = maxcount;
    // Let's wait for everyone to release their blocks
    do {
        unsigned int i;
        for(i=0; i<nblock; i++)
            if( use_cnt[i] )
                break;
        if( i==nblock )
            break;
        // sleep for 10ms
        ::usleep(10000);
    } while( --count );

    if( count == 0 ) {
        DEBUG(-1, "pool[" << (void*)this << "]::~pool() -  not all use counts went to zero!!!!!" << endl);
    }
    else if( count!=maxcount ) {
        DEBUG(2, "pool[" << (void*)this << "]::~pool() -  all use counts at zero, counts left " << count << endl);
    }

    delete [] use_cnt;
    delete [] memory;
}


// blockpool preallocates memory in pools of
// size nblock_p_chunk blocks of bs bytes
//
// It starts with one pool and adds more
// as necessary
blockpool_type::blockpool_type(unsigned int bs, unsigned int nb):
    blocksize(bs), nblock_p_pool(nb)
{
    EZASSERT2(blocksize>0 && nblock_p_pool>0, blockpool_error, 
              EZINFO("both blocksize (" << blocksize << ") and nblock_p_pool (" <<
                     nblock_p_pool << ") must be >0") );
    // start with one pool
    curpool = pools.insert(pools.end(), new pool_type(blocksize, nblock_p_pool));
}

// get  a fresh block
block blockpool_type::get( void ) {
    // oh dear. someone wants a block
    block                rv;
    pool_pointer_pointer oldcurpool = curpool;

    // first: loop over all pools we manage to see if someone
    // has a free blocks
    do {
        if( !((rv=(*curpool)->get()).empty()) )
            break;
        curpool++;
        if( curpool==pools.end() )
            curpool=pools.begin();
    } while( curpool!=oldcurpool );

    // if the 'rv' block is still invalid,
    // we went round the block w/o finding a free block
    // in the pool
    if( rv.empty() ) {
        // I guess it's safe to assume allocation from a freshly created
        // pool should always succeed ...
        curpool = pools.insert(pools.end(), new pool_type(blocksize, nblock_p_pool));
        rv      = (*curpool)->get();
    }
    return rv;
}

void blockpool_type::show_usecnt( void ) const {
    for(const_pool_pointer_pointer p=pools.begin(); p!=pools.end(); p++)
        (*p)->show_usecnt();
}

blockpool_type::~blockpool_type() {
    for(pool_pointer_pointer p=pools.begin(); p!=pools.end(); p++)
        delete (*p);
}
