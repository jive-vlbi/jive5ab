// implementation of struct block
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
#include <block.h>
#include <atomic.h>


DEFINE_EZEXCEPT(block_error)


refcount_type block::dummy_counter = 1;


block::block():
    iov_base( 0 ), iov_len( 0 ), refcountptr(&dummy_counter)
{}

block::block(const block& other) {
    ::atomic_inc(other.refcountptr);
    iov_base    = other.iov_base;
    iov_len     = other.iov_len;
    refcountptr = other.refcountptr;
}

const block& block::operator=(const block& other) {
    if( this==&other )
        return *this;

    ::atomic_inc(other.refcountptr);
    iov_base  = other.iov_base;
    iov_len   = other.iov_len;
    ::atomic_dec(refcountptr);
    refcountptr = other.refcountptr;
    return *this;
}

block block::sub(unsigned int offset, unsigned int length) const {
    // if this isn't going to fit might as well
    // crash-and-burn.
    // Oh. Do not check for 4GB overflow. Dont
    // do that yet. Then move to 64bit sizes & offsets
    EZASSERT2(offset+length<=iov_len,
              block_error,
              EZINFO("request for slice is out of bounds - offset:" << offset 
                     << " + length:" << length << " >= iov_len:" << iov_len));
    // We *know* the slice can be made so we're definitely
    // going to get an extra reference to whatever we're 
    // referring to
    ::atomic_inc(refcountptr);
    return block((unsigned char*)iov_base+offset, length, refcountptr);
}

block::block(void* base, size_t sz, refcount_type* refcnt):
    iov_base( base ), iov_len( sz ), refcountptr(refcnt)
{}

bool block::empty( void ) const {
    return (iov_base==0 && iov_len==0);
}

block::~block() {
    ::atomic_dec(refcountptr);
}
