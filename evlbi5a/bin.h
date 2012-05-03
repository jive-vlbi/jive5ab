// print a datum in binary
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
#ifndef BIN_H
#define BIN_H

#include <iostream>
#include <iomanip>
#include <string>
#include <exception>
#include <stddef.h>

namespace bin_lcl {
	struct unrecognized_byteorder :
		public std::exception
	{
		public:
			virtual const char* what( void ) const throw();
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


	//  Crud to do with detecting if
	//  the template type is a pointer
	//  or not
	class NullType {};

	template <typename T>
    class TypeTraits {
		private:
			template <class U> struct PointerTraits {
				enum { result = false };
				typedef NullType PointeeType;
			};
        
			template <class U> struct PointerTraits<U*> {
				enum { result = true };
				typedef U PointeeType;
			};
		public:
			enum { isPointer = PointerTraits<T>::result };
			typedef typename PointerTraits<T>::PointeeType PointeeType;
	};

	
	typedef unsigned char       byte;
			
	class iter_t {
		public:
			// default c'tor - creates invalid
			// iterator
			iter_t();
			// create well defined iterator
			iter_t(const byte* p, ptrdiff_t incr);
	
			// and pre-/postfix increment
			const iter_t& operator++( void );
			const iter_t& operator++( int );
						
			// de-reference
			const byte& operator*( void ) const;

			// and compare
			bool operator==( const iter_t& other ) const;
			bool operator!=( const iter_t& other ) const;

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
			typedef bin_lcl::iter_t const_iterator;

			// construct from const ref to a variable
			// pointers point to a (local) copy of the variable.
			// UNLESS the underlying type T is of
			// type "pointer to something" (eg T='const char*')
			// THEN the pointers point at whatever
			// 't' was pointing to (eg the character that
			// 't' points to, following the 'eg' before)
			byter_t( T const & t ):
				datum( 0 ), first(0), last(0),
				inc(0), sz( 0 )
			{
				bool  T_is_pointer = TypeTraits<T>::isPointer;
				
				if( T_is_pointer ) {
					// T is actually a pointer type
					// so do not copy the pointer
					// (otherwise we'd be iterating
					// over the *pointer* rather than
					// what we're pointing at)
					this->attach(
						reinterpret_cast<const byte*>(t),
						sizeof(typename TypeTraits<T>::PointeeType) );
                } else {
					// T looks like POD.
					// Just create a copy and 
					// iterator over *that*
					datum = new T(t);
					this->attach((const byte*)datum);
				}
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
				// 'incr' and 'sz' since they
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
					// iterator over *that*
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
				return const_iterator( first, inc );
			}
			const_iterator  end( void ) const {
				// do not support increment on 'end()'?
				return const_iterator( last, 0 );
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
					inc    = +1;
				} else if( bo.endianness==little ) {
					first  = ptr + sz -1;
					last   = ptr - 1;
					inc    = -1;
				} else 
					throw unrecognized_byteorder();
				return;
			}
			void attach( const byte* ptr, size_t s ) {
				sz = s;
				// fill in variables depending
				// on byteorder
				if( bo.endianness==big ) {
					first  = ptr;
					last   = ptr + sz;
					inc    = +1;
				} else if( bo.endianness==little ) {
					first  = ptr + sz - 1;
					last   = ptr - 1;
					inc    = -1;
				} else 
					throw unrecognized_byteorder();
				return;
			}
			
			// no assignment
			const byter_t<T>& operator=( const byter_t<T>& );
	};

    // the interface that objects, who say they're
    // printable in binary, should adhere to
    struct bininterface_t {
        virtual void print_in_bin( std::ostream& os ) const = 0;
        virtual bininterface_t* clone( void ) const = 0;
        virtual ~bininterface_t();
    };


    // An implementation of the bininterface_t
    // for POD
    template <typename T>
        struct binimpl_t:
            public bininterface_t 
    {
        binimpl_t( const T& t ) :
            datum( t )
        {}

        // return deep copy of self
        virtual bininterface_t* clone( void ) const {
            return new binimpl_t<T>(*this);
        }

        // do the actual printing!
        virtual void print_in_bin(std::ostream& os) const {
            typename bin_lcl::byter_t<T>::const_iterator  p;

            //os << "[" << datum.size()*8 << "bit:";
            // iterate over the bytes, from MSB -> LSB
            for( p=datum.begin(); p!=datum.end(); p++ ) {
                // 'iterate' over the bits, from
                // MSBit to LSBit
                unsigned char  mask = 0x80;

                do {
                    //(void)((mask==0x80||mask==0x08)?(os<<' '):(os));
                    os << (((*p&mask)==mask)?('1'):('0'));
                    mask = (unsigned char)(mask>>1);
                } while( mask!=0x0 );
            }
            //os << "]";
        }

        virtual ~binimpl_t() {}

        private:
            bin_lcl::byter_t<T>  datum;

            // prohibit default objects...
            binimpl_t();
    };
} // end of namespace bin_lcl




struct bin_t {
	friend std::ostream& operator<<( std::ostream&, const bin_t& );

	template <typename T>
	bin_t( const T& t ) :
		binptr( new bin_lcl::binimpl_t<T>(t) )
	{}

	bin_t( const bin_t& o );

	~bin_t();

	private:
        bin_lcl::bininterface_t*   binptr;

		bin_t();
		const bin_t& operator=( const bin_t& );
};

std::ostream& operator<<( std::ostream& os, const bin_t& ht );

#endif
