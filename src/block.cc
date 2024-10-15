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
#include <stdlib.h>


DEFINE_EZEXCEPT(block_error)



// If we in C++11 happyland we do things differently
#if __cplusplus >= 201103L

refcount_type block::dummy_counter;

bool block::init_dummy( void ) {
    static bool did_init{ false };
    if( did_init )
        return did_init;
    std::atomic_init(&dummy_counter, (uint32_t)1);
    did_init = true;
    return did_init;
}
bool d = block::init_dummy();

// INC() and DEC() only have to work on std::atomic<>
#define INC(a) (*a)++
#define DEC(a) (*a)--

#else // not in C++11 happyland

refcount_type block::dummy_counter = 1;

// This only works on Intel x86 CPUs but easily avoided by compiling w/ C++11 support
#if B2B==32
    #define INC(a) \
    __asm__ __volatile__ ( "movl %0, %%eax; lock; incl (%%eax)" : : "m"(a) : "eax", "memory" );
    #define DEC(a) \
    __asm__ __volatile__ ( "movl %0, %%eax; lock; decl (%%eax)" : : "m"(a) : "eax", "memory" );
#endif

#if B2B==64
    #define INC(a) \
    __asm__ __volatile__ ( "movq %0, %%rax; lock; incl (%%rax)" : : "m"(a) : "rax", "memory" );
    #define DEC(a) \
    __asm__ __volatile__ ( "movq %0, %%rax; lock; decl (%%rax)" : : "m"(a) : "rax", "memory" );
#endif

#endif

block::block():
    iov_base( 0 ), iov_len( 0 ), myMemory( false ), refcountptr(&dummy_counter)
{}

block::block(size_t sz):
    iov_len( sz ), myMemory( true ), 
    refcountptr( (refcount_type*)::malloc(sizeof(refcount_type) + iov_len) )
{
    // malloc space for the block and the refcounter in one go
    EZASSERT2(refcountptr, block_error, EZINFO("Failed to malloc " << iov_len << " bytes"));
    iov_base     = (void*)(((unsigned char*)refcountptr) + sizeof(refcount_type));
// If we in C++11 happyland we do things differently
#if __cplusplus >= 201103L
    std::atomic_init(refcountptr, 1);
#else
    *refcountptr = 1;
#endif
}

block::block(const block& other) {
    INC(other.refcountptr);
    iov_base    = other.iov_base;
    iov_len     = other.iov_len;
    refcountptr = other.refcountptr;
    myMemory    = other.myMemory;
}

block::iterator block::begin( void ) {
    return const_cast<block*>(this);
}

block::const_iterator block::begin( void ) const {
    return this;
}

block::iterator block::end( void ) {
    return const_cast<block*>(this+1);
}

block::const_iterator block::end( void ) const {
    return this+1;
}

const block& block::operator=(const block& other) {
    if( this==&other )
        return *this;

    INC(other.refcountptr);
    iov_base  = other.iov_base;
    iov_len   = other.iov_len;
    DEC(refcountptr);
    if( myMemory && *refcountptr==0 )
        ::free( (void*)refcountptr );
    refcountptr = other.refcountptr;
    myMemory    = other.myMemory;
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
    INC(refcountptr);
    return block((unsigned char*)iov_base+offset, length, refcountptr, myMemory);
}

block::block(void* base, size_t sz, refcount_type* refcnt, bool myMem):
    iov_base( base ), iov_len( sz ), myMemory( myMem ), refcountptr(refcnt)
{}

bool block::empty( void ) const {
    return (iov_base==0 && iov_len==0);
}

block::~block() {
    DEC(refcountptr);
    if( myMemory && *refcountptr==0 )
        ::free( (void*)refcountptr );
}
