// support (easy) printing of values/arrays of values in hex on c++ ostreams
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
//
// example:
// #include <hex.h>
// int   i = 0xabcd;
// cout << hex_t(i) << endl;
// would show:
// 0x0000abcd
#ifndef HEX_H
#define HEX_H

#include <iostream>
#include <iomanip>
#include <exception>

// for ::snprintf()
#include <stdio.h>

namespace hex_lcl {
	// this exception will be thrown
	// if the byteorder of the current
	// system cannot be recognized
	struct unrecognized_byteorder :
		public std::exception
	{
        unrecognized_byteorder();

        virtual const char* what( void) const throw();
        virtual ~unrecognized_byteorder() throw();
	};

	//  What's the machine's endianness?
	enum _endian_t {
		big=1, little=2
	};
	struct botest_t {
		enum _endian_t  endianness;
		
		union {
			unsigned int   i;
			unsigned char  c[ sizeof(unsigned int) ];
		} __t;

        botest_t();

		private:
			// prohibit any of these
			botest_t( const botest_t& );
			const botest_t& operator=( const botest_t& );
	};
	static botest_t    bo;

	//  Define a 'byte-iterator'
	typedef unsigned char       byte;
			
	class _iter_t {
		public:
			// default c'tor - creates invalid
			// iterator
			_iter_t();
			// create well defined iterator
			_iter_t(const byte* p, ptrdiff_t inc);
	
			// define pre-/postfix increment
			const _iter_t& operator++( void );
			const _iter_t& operator++( int );
						
			// de-reference
			const byte& operator*( void ) const;

			// and compare
			bool operator==( const _iter_t& other ) const;
			bool operator!=( const _iter_t& other ) const;

		private:
			const byte* cur;
			ptrdiff_t   inc;
	};

	//  now a typespecific byte-accessor
	//  Will set up itself such that
	//  if you iterate from
	//  'begin()' to 'end()'
	//  you iterate over the bytes of
	//  the variable, from MostSignificantByte
	//  to LeastSignificantByte.
	template <typename T>
	class byter_t {
		public:
			// typedefs a-la STL
			// only we support only the const versions
			typedef hex_lcl::_iter_t const_iterator;

			// construct from const ref to a variable
			// pointers point to a (local) copy of the variable.
			//
			// Note that IF the underlying type of T
			// is of type "pointer-to-something"
			// then the *pointer* value is printed
			// in hex, NOT the item being pointed
			// at!!!!
			//
			// This could happen in the following
			// situation:
			//
			// const char*   hello = "Hello";
			//
			// // this will print the value
			// // of the pointer 'hello' ie
			// // the starting adress where the 
			// // characters 'H', 'e' etc.
			// // are stored.
			// cout << hex_t(hello) << endl;
			//
			// // if you want to print the
			// // *characters* in hex, 
			// // (ie the contents of whatever
			// //  'hello' points at) use 
			// // the following:
			// cout << hex_t(hello, 5) << endl;
			//
			byter_t( T const & t ):
				datum( 0 ), first(0), last(0),
				inc(0), sz( 0 )
			{
				// Just create a copy and 
				// iterate over *that*
				datum = new T(t);
				this->attach((const byte*)datum);
			}

			// create from pointer to variable;
			// we 'monitor' the location
			byter_t( T* const t ):
				datum( 0 ), first(0), last(0),
				inc(0), sz( 0 )
			{
				this->attach((const byte*)t);
			}

			// copy c'tor
			byter_t( const byter_t<T>& other ) :
				datum( 0 ), first( 0 ), last( 0 ),
				inc( other.inc ), sz( other.sz )
			{
				// NOTE: we can already takeover
				// 'inc' and 'sz' since they
				// are not variable dependant
				// but only *type* dependant.
				// The other thingies (datum,
				// first, last) are specific
				// to some variable that we're
				// 'attached' to
				// 
				// depending on whether or not
				// 'other.datum' contains a value or
				// not we do stuff
				// NOTE: 'datum' only contains a non-zero
				// value iff it was POD (see c'tor)
				if( other.datum ) {
					// T looks like POD.
					// Just create a copy and 
					// iterate over *that*
					datum = new T( *other.datum );
					this->attach((const byte*)datum);
				} else {
					// 'other' just "monitored"
					// some area in memory, we will
					// do so as well
					first = other.first;
					last  = other.last;
				}
			}

				
			// STL iterable container interface
			const_iterator  begin( void ) const {
				return const_iterator(first, inc);
			}
			const_iterator  end( void ) const {
				// do not support increment on 'end()'?
				return const_iterator(last, 0);
			}

			size_t size( void ) const {
				return sz;
			}
			~byter_t() {
				delete datum;
			}

		private:
			T*            datum;
			const byte*   first;
			const byte*   last;
			ptrdiff_t     inc;
			size_t        sz;

			// assume 'p' points
			// to start of variable
			// of type T. Fill
			// in 'first', 'last'
			// and 'incr' depending
			// on byteorder of the machine
			void attach( const byte* ptr ) {
				sz = sizeof(T);
				// fill in variables depending
				// on byteorder
				if( bo.endianness==big ) {
					first  = ptr;
					last   = ptr + sz;
					inc   = +1;
				} else if( bo.endianness==little ) {
					first  = ptr + sz -1;
					last   = ptr - 1;
					inc   = -1;
				} else {
					throw unrecognized_byteorder();
				}
				return;
			}
			void attach( const byte* ptr, size_t s ) {
				sz = s;
				// fill in variables depending
				// on byteorder
				if( bo.endianness==big ) {
					first  = ptr;
					last   = ptr + sz;
					inc   = +1;
				} else if( bo.endianness==little ) {
					first  = ptr + sz - 1;
					last   = ptr - 1;
					inc   = -1;
				} else {
					throw unrecognized_byteorder();
				}
				return;
			}
			
			// no assignment
			const byter_t<T>& operator=( const byter_t<T>& );
	};

    // this defines the interface for objects that want to
    // be printable in HEX
    struct hexinterface_t {
        // do print yourself in HEX on the given stream
        virtual void print_in_hex( std::ostream& os ) const = 0;
        // create deep copy of self
        virtual hexinterface_t*  clone( void ) const = 0; 
        virtual ~hexinterface_t();
    };


    // This one is for POD types
    template <typename T>
        struct heximpl_t:
            public hexinterface_t
    {
        heximpl_t( const T& t ) :
            datum( t )
        {}

        virtual void print_in_hex( std::ostream& os ) const {
            char                                         buf[4];
            typename hex_lcl::byter_t<T>::const_iterator p;

            os << "0x";
            for( p=datum.begin();
                    p!=datum.end();
                    p++ )
            {
                // use plain old snprintf 'cuz
                // if we use iostream manipulator
                // std::hex on os and we insert
                // *p (which is 'unsigned char'
                // then it don't work. Must
                // convert to 'int' first.
                // Basterd C++ iostreams!!
                ::snprintf(buf, 3, "%02x", *p);
                os << buf;
            }
            return;
        }


        virtual hexinterface_t* clone( void ) const {
            return new heximpl_t<T>(*this);
        }

        virtual ~heximpl_t() {}

        private:
        hex_lcl::byter_t<T>  datum;

        // prohibit default objects...
        heximpl_t();
    };

    // If you have an array of POD [could be a string!]
    // use this'un
    template <typename T>
        struct hexarimpl_t:
            public hexinterface_t
    {
        hexarimpl_t( const T* t, unsigned int n ) :
            sz( n ), data( new T[sz] )
        {
            for( unsigned i=0; i<sz; ++i )
                data[i] = t[i];
        }

        // print elements of the array in hex, separated by a comma
        // and between []'s
        virtual void print_in_hex(std::ostream& os) const {
            os << "[";
            for(unsigned i=0; i<sz; ++i ) {
                heximpl_t<T> t(data[i]);

                t.print_in_hex(os);

                (void)(i<(sz-1) && (os<<','));
            }
            os << "]";
            return;
        }

        virtual hexinterface_t* clone( void ) const {
            return new hexarimpl_t<T>( data, sz );
        }

        virtual ~hexarimpl_t() {
            delete [] data;
        }

        private:
        unsigned int sz;
        T*           data;

        // prohibit default objects...
        hexarimpl_t();
    };
} // end of namespace 'hex_lcl'


// this is what the user will see and use
struct hex_t {
	friend std::ostream& operator<<( std::ostream&, const hex_t& );

    // constructor for POD 
	template <typename T>
	hex_t( const T& t ) :
		hexptr( new hex_lcl::heximpl_t<T>(t) )
	{}

    // for array-types
	template <typename T>
	hex_t( const T t[], unsigned int n ) :
		hexptr( new hex_lcl::hexarimpl_t<T>(t, n) )
	{}

	// with gcc 3.4 and up, temporaries
    // 	(even const ones!) *must*
	//  be copy-constructible... which is 
	//  a bloody nuisance!!!
	hex_t( const hex_t& ht );

	~hex_t();

    // the following things are kept private
	private:
        hex_lcl::hexinterface_t*   hexptr;

        // these are forbidden
		hex_t();
		const hex_t& operator=( const hex_t& );
};

std::ostream& operator<<( std::ostream& os, const hex_t& ht );

#endif
