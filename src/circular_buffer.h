// simple circular buffer with semantics for packet reordering statistics
// Copyright (C) 2007-2010 Harro Verkouter
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
//
// author: Harro Verkouter 
#ifndef JIVE5A_CIRCULARBUFFER_H
#define JIVE5A_CIRCULARBUFFER_H

#include <exception>
#include <iostream>

#include <stdlib.h>

template <typename T>
struct not_enough_elements_in_buffer:
	public std::exception
{};

#if 0
template <typename T>
struct invalid_use_of_dangerous_function_you_ignoramus:
	public std::exception
{};
#endif

#define CBMAX(a,b) ((a<b)?(b):(a))
#define CBMIN(a,b) ((a<b)?(a):(b))

// circular buffer for any number of elements (including 0)
// It is NOT threadsafe
// Erasing elements whilst iterating over the buff
// will corrupt your loop - don't do it
template <typename Element>
struct circular_buffer {
        // fully functional circular buffer of size 0
        circular_buffer():
            capacity( 0 ), nelements( 0 ), elements( 0 ),
            read_ptr( 0 ), write_ptr( 0 )
        {}

        // circular buffer for n elements. also works with n==0 elements
        circular_buffer( unsigned int n ):
            capacity( n ), nelements( 0 ), elements( new Element[capacity+1] ),
            read_ptr( 0 ), write_ptr( 0 )
        {}

        // completely copy the others' state, including
		// elements cached sofar
        circular_buffer(const circular_buffer& other):
            capacity( other.capacity ), nelements( other.nelements ), elements( new Element[capacity+1] ),
            read_ptr( other.read_ptr ), write_ptr( other.write_ptr ) 
        {
            for(unsigned int i=0; i<capacity; i++)
                elements[i] = other.elements[i];
        }

        // the amount of elements available in the buffer
        unsigned int size( void ) const {
            return nelements;
        }

		// clear the buffer; make it empty
		void clear( void ) {
            read_ptr = write_ptr = nelements = 0;
        }

		// pop one element. throws an exception if there is none
        Element pop( void ) {
            Element tmp;
            this->pop(&tmp, 1);
            return tmp;
        }

        // read 'n' elements into 'b'. throws up if you request more than
        // is available. there is, after all, the "size()" method which
        // tells you how many you could've read.
        void pop(Element* b, unsigned int n) {
            if( n>nelements )
                throw not_enough_elements_in_buffer<Element>();
            // good. we KNOW we can satisfy the request
            unsigned int  read_to_end( CBMIN((capacity-read_ptr), n) );
            unsigned int  read_from_begin( CBMAX(0, (n-read_to_end)) );

            for(unsigned int i=0; i<read_to_end; i++)
                b[i] = elements[read_ptr+i];

            // can do this unconditionally since if we didn't have
            // to read from the beginning => read_from_begin==0
            // => the memcpy turns into a nop
            for(unsigned int i=0; i<read_from_begin; i++)
                b[read_to_end+i] = elements[i];

            // if we read *past* the end of the linear buffer we must
            // reposition ourselves at the beginning
            read_ptr   = (((read_ptr+=n)>capacity)?(read_from_begin):(read_ptr));
            nelements -= n;
            return;
        }
#if 0
        // This is a dangerous one ... it returns a pointer
        // *into* the circular buffer BUT it alters the
        // readpointer nontheless. Important:
        //   * YOU must make sure that you're done with this
        //     data before it gets overwritten
        //   * IF the pop() you're doing would point outside
        //     the buffer it will throw. If the pop() starts
        //     _exactly_ at the end, then it wraps back to
        //     the beginning of the buffer instead.
        //     As such, the ONLY acceptable
        //     usecase is when an integral amount of pop()s
        //     fit into the actual circular buffer size.
        //     Assuming you only use this if you control
        //     both the size of the buffer and the size of the
        //     pop()s. The push()es can be any size.
        // The main use of this is to allow efficient
        // accumulation of unknown amounts of bytes into
        // a circular buffer and pop only a fixed amount
        // of bytes at a time.
        // Only use this if you know what you're doing.
        unsigned char* pop( unsigned int n );
#endif
        // append a copy of the element to the circular buffer
        void push(const Element& b) {
            this->push(&b, 1);
        }

        // Append the buffer of 'n' bytes located at 'b' to this
        // circular buffer. In reality this means it will drop
        // the leading (n-capacity) bytes (if n>capacity that is)
        void push(const Element* const ptr, unsigned int n) {
            const unsigned int skip( (n<capacity)?(0):(n-capacity) );
            const unsigned int ncpy( n - skip );

            // if skip!=0 => replace whole circular buffer with last 'capacity'
            //               bytes from ptr
            if( skip ) 
                write_ptr = read_ptr = nelements = 0;
            // we can already update to the new size "so far".
            // the new value is used to discriminate between
            // overwriting part of the bufferspace or not
            // and it will get its final new value at the end of this fn
            nelements += ncpy;

            // overwrite part of the buffer
            // 1. how many bytes are available to the end of the buffer
            //    and how many do we need to write?
            const unsigned int n_to_end( CBMIN((capacity-write_ptr), ncpy) );
            // 2. do we need to copy at the beginning?
            const unsigned int n_from_begin( CBMAX(0, (ncpy-n_to_end)) );

            for(unsigned int i=0; i<n_to_end; i++)
                elements[write_ptr+i] = ptr[skip+i];
            for(unsigned int i=0; i<n_from_begin; i++)
                elements[i] = ptr[skip+n_to_end+i];

            // given that we've done the analysis of where the
            // ncpy bytes should go, it's fairly easy to reposition the write_ptr
            write_ptr = (((write_ptr+=ncpy)>capacity)?(n_from_begin):(write_ptr));

            // if the write_ptr 'overtook' the read_ptr we have
            // to reposition the read_ptr such that it points at
            // the start of the last 'capacity' bytes (which, incidentally,
            // is the new write_ptr ;))
            read_ptr  = ((nelements>capacity)?(write_ptr):(read_ptr));

            // need to update 'nbytes' - truncate it to
            // 'capacity' if it would seem to indicate
            // we have more available
            nelements = CBMIN(capacity, nelements);
        }

		// random access to individual elements. WILL throw
		// if you're indexing outside allowable range
        // Adressing using a negative value indexes from the end.
        //
        // NOTE: this will obviously fail when there are more
        //       than 2G elements (2^31 - 1); the unsigned <->
        //       signed conversion will break
		const Element& operator[]( int idx ) const {
            unsigned int absidx = (unsigned int)::abs(idx);

            if( (idx>=0 && absidx>=nelements) || (idx<0 && absidx>nelements) )
                throw not_enough_elements_in_buffer<Element>();
            // compute the linear index of the requested byte
            const unsigned int  lidx( (idx>=0)?(((absidx+=read_ptr)>=capacity)?(absidx-capacity):(absidx)):
                                              ((absidx>write_ptr)?(capacity-absidx+write_ptr):(write_ptr-absidx)) );
            return elements[lidx];
        }

		// clean up
        ~circular_buffer() {
            delete [] elements;
        }

    private:
		// variables for keeping track of state
        const unsigned int    capacity;  // how many elements can be hold
        unsigned int          nelements; // how many elements are actually held
        Element*              elements;  // the actual elements
        unsigned int          read_ptr;  // ptr to actual read position 
        unsigned int          write_ptr; // ptr to actual write position
};

#if 0
// *cough* now this izza nasty one. use it at own
// risk, use it well or exceptions may be all over you
unsigned char* circular_buffer::pop(unsigned int n) {
	if( n>nbytes )
		throw not_enough_bytes_in_buffer();
	// good. we KNOW we can satisfy the request
    // Now, the only valid values of read_to_end
    // or 0 and n. All other values of read_to_end
    // are throwage.
    // If read_to_end == 0 we reset the read_ptr 
    // to the start of the block
	const unsigned int  read_to_end( MIN((capacity-read_ptr), n) );

    if( !(read_to_end==0 || read_to_end==n) )
        throw invalid_use_of_dangerous_function_you_ignoramus();

    // Since we have ascertained ourselves that all
    // seems to be well we can now do the right thing.
    // If read_to_end==0 => we left off exactly at
    //                      the end of the buffer.
    //                      The block we *can* read
    //                      is at the beginning of the
    //                      buffer; we have wrapped.
    //    read_to_end==n => We can read the next
    //                      block right from where
    //                      we left off
    // If we now already compute the new read_ptr,
    // we can compute the return-value of the function
    // by simply subtracting n from the new read_ptr.
    // Also account for having taken n bytes out of the buffer
    read_ptr  = ((read_to_end==0)?(n):(read_ptr+n));
    nbytes   -= n;

    return bytes + (read_ptr-n);
}
#endif

#endif
