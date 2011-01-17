// implements a simple circular buffer (with memcpy/memcmp defined!)
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
// Helps decide wether or not to (potentially) drop
// this chunk. By making sure we do not drop chunks with
// (partial) headerinformation we make sure that the
// correlator has a (much) better chance of staying 
// synchronized in lieu of packet-dropping.
//
// author: Harro Verkouter 
#ifndef JIVE5A_CIRCULARBUFFER_H
#define JIVE5A_CIRCULARBUFFER_H

#include <exception>

struct not_enough_bytes_in_buffer:
	public std::exception
{};
struct invalid_use_of_dangerous_function_you_ignoramus:
	public std::exception
{};

// circular buffer for any number of bytes (including 0)
// the unit of storage is "byte" (one 'unsigned char').
struct circular_buffer {
        // fully functional circular buffer of size 0
        circular_buffer() ;
        // circular buffer for n bytes. also works with n==0 bytes
        circular_buffer( unsigned int n );
        // completely copy the others' state, including
		// bytes cached sofar
        circular_buffer(const circular_buffer& other);

        // the amount of bytes available in the buffer
        unsigned int size( void ) const;

		// clear the buffer; make it empty
		void clear( void );

		// pop one byte. throws an exception if there is no
		// byte available
        unsigned char pop( void );

        // read 'n' bytes into 'b'. throws up if you request more than
        // is available. there is, after all, the "size()" method which
        // tells you how many you could've read.
        void pop(unsigned char* b, unsigned int n);

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

        // append one byte to the circular buffer
        void push(unsigned char b);

        // Append the buffer of 'n' bytes located at 'b' to this
        // circular buffer. In reality this means it will drop
        // the leading (n-capacity) bytes (if n>capacity that is)
        void push(const unsigned char* const ptr, unsigned int n);

		// random access to individual bytes. WILL throw
		// if you're indexing outside allowable range
		unsigned char operator[]( unsigned int idx ) const;

		// clean up
        ~circular_buffer();

    private:
		// variables for keeping track of state
        const unsigned int    capacity;  // how many bytes can be hold
        unsigned int          nbytes;    // how many bytes are actually held
        unsigned char*        bytes;     // the actual bytes
        unsigned int          read_ptr;  // ptr to actual read position 
        unsigned int          write_ptr; // ptr to actual write position
};


#endif
