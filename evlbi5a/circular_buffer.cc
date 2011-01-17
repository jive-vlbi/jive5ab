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
#include <circular_buffer.h>

// for ::memcpy()
#include <string.h> 


#define MAX(a,b) ((a<b)?(b):(a))
#define MIN(a,b) ((a<b)?(a):(b))


// empty circular buffer
circular_buffer::circular_buffer() :
	capacity( 0 ), nbytes( 0 ), bytes( 0 ),
	read_ptr( 0 ), write_ptr( 0 )
{}

// circular buffer for n bytes. also works with n==0 bytes
circular_buffer::circular_buffer( unsigned int n ):
	capacity( n ), nbytes( 0 ), bytes( new unsigned char[capacity+1] ),
	read_ptr( 0 ), write_ptr( 0 )
{}
// completely copy the others' state. 
circular_buffer::circular_buffer(const circular_buffer& other):
	capacity( other.capacity ), nbytes( other.nbytes ), bytes( new unsigned char[capacity+1] ),
	read_ptr( other.read_ptr ), write_ptr( other.write_ptr ) {
		::memcpy( bytes, other.bytes, capacity );
}

// ...
unsigned int circular_buffer::size( void ) const {
	return nbytes;
}

// reset the circular buffer to empty
void circular_buffer::clear( void ) {
	read_ptr = write_ptr = nbytes = 0;
	return;
}

// seems a bit odd popping a single byte like this but
// then the pop algorithm only needs to be implemented once
unsigned char circular_buffer::pop( void ) {
	unsigned char rv;
	this->pop(&rv, 1);
	return rv;
}

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

// read 'n' bytes into 'b'. throws up if you request more than
// is available. there is, after all, the "size()" method which
// tells you how many you could've read.
void circular_buffer::pop(unsigned char* b, unsigned n) {
	if( n>nbytes )
		throw not_enough_bytes_in_buffer();
	// good. we KNOW we can satisfy the request
	unsigned int  read_to_end( MIN((capacity-read_ptr), n) );
	unsigned int  read_from_begin( MAX(0, (n-read_to_end)) );

	::memcpy(b, bytes+read_ptr, read_to_end);
	// can do this unconditionally since if we didn't have
	// to read from the beginning => read_from_begin==0
	// => the memcpy turns into a nop
	::memcpy(b+read_to_end, bytes, read_from_begin);
	// if we read *past* the end of the linear buffer we must
	// reposition ourselves at the beginning
	read_ptr  = (((read_ptr+=n)>capacity)?(read_from_begin):(read_ptr));
	nbytes   -= n;
	return;
}

// append one byte to the circular buffer
//   may look like a somewhat add construct for adding one
//   byte but it ascertains that it works for ALL capacities
//   (including capacity==0) transparently
void circular_buffer::push(unsigned char b) {
	this->push(&b, 1);
	return;
}

// Append the buffer of 'n' bytes located at 'b' to this
// circular buffer. In reality this means it will drop
// the leading (n-capacity) bytes (if n>capacity that is)
void circular_buffer::push(const unsigned char* const ptr, unsigned int n) {
	const unsigned int skip( (n<capacity)?(0):(n-capacity) );
	const unsigned int ncpy( n - skip );
	// if skip!=0 => replace whole circular buffer with last 'capacity'
	//               bytes from ptr
	if( skip ) 
		write_ptr = read_ptr = nbytes = 0;
	// we can already update to the new size "so far".
	// the new value is used to discriminate between
	// overwriting part of the bufferspace or not
	// and it will get its final new value at the end of this fn
	nbytes += ncpy;
	// overwrite part of the buffer
	// 1. how many bytes are available to the end of the buffer
	//    and how many do we need to write?
	const unsigned int n_to_end( MIN((capacity-write_ptr), ncpy) );
	// 2. do we need to copy at the beginning?
	const unsigned int n_from_begin( MAX(0, (ncpy-n_to_end)) );

	::memcpy(bytes+write_ptr, ptr+skip, n_to_end);
	::memcpy(bytes, ptr+skip+n_to_end, n_from_begin);

	// given that we've done the analysis of where the
	// ncpy bytes should go, it's fairly easy to reposition the write_ptr
	write_ptr = (((write_ptr+=ncpy)>capacity)?(n_from_begin):(write_ptr));
	// if the write_ptr 'overtook' the read_ptr we have
	// to reposition the read_ptr such that it points at
	// the start of the last 'capacity' bytes (which, incidentally,
	// is the new write_ptr ;))
	read_ptr  = ((nbytes>capacity)?(write_ptr):(read_ptr));
	// need to update 'nbytes' - truncate it to
	// 'capacity' if it would seem to indicate
	// we have more available
	nbytes = MIN(capacity, nbytes);
}

// provide random access to the bytes in the buffer
unsigned char circular_buffer::operator[](unsigned int idx) const {
	if( idx>=nbytes )
		throw not_enough_bytes_in_buffer();
	// compute the linear index of the requested byte
	const unsigned int  lidx( ((idx+=read_ptr)>=capacity)?(idx-capacity):(idx) );
	return bytes[lidx];
}

circular_buffer::~circular_buffer() {
	// we can unconditionally do this; either it's NULL or it points
	// at something WE allocated ourselves.
	delete [] bytes;
}
