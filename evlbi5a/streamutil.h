// some utilities for pretty printing on streams and other stuff
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
#ifndef STREAMUTIL_H
#define STREAMUTIL_H

#include <deque>
#include <string>
#include <iostream>

#include <stdint.h> // for [u]int<N>_t  types

// pretty print struct: takes a number and a text, <n> and <str>.
// when output on a stream it formats the stuff as follows:
//    <n><space><str>[s]   where the character 's' will be added if
// <n> != 1
//
// usefull for (pretty) printing number of items stuff...
//
// Note: the 's' suffix can be overruled if you want a different
// "multiples" indication

// typeindependant interface
struct num_interface {
	virtual void print_on_stream( std::ostream& os ) const = 0;

    // make sure it's comparable to uint64_t
    // (it will be tested agains "1" to see if the multiple
    // needs to be printed
	virtual bool operator==( uint64_t v ) const = 0;

	// this'un don't need to be virtual...
	bool operator!=( uint64_t v ) const {
		return !((*this)==v);
	}

	virtual ~num_interface()
	{}
};

// a type-dependant "amount" 
template <typename T>
struct num_impl :
	public num_interface
{
	num_impl( T t ) :
		nr( t )
	{}
	virtual void print_on_stream( std::ostream& os ) const {
		os << nr;
		return;
	}
	virtual bool operator==( uint64_t v ) const {
		return ((uint64_t)nr==v);
	}

	virtual ~num_impl()
	{}

	const T   nr;
};

struct pprint {
	friend std::ostream& operator<<( std::ostream& os, const pprint& p );

	// templated c'tor -> you can feed it anything, as long
	// as it is interpretable as an integer...
	template <typename T>
	pprint( T n, const std::string& str, const std::string& mult="s" ) :
		mystr( str ), mymult( mult ),
		nrptr( new num_impl<T>(n) )
	{}

	~pprint();

private:
	const std::string   mystr;
	const std::string   mymult;
	num_interface*      nrptr;

	// prohibit these
	pprint();
};
std::ostream& operator<<( std::ostream& os, const pprint& p );


// Indenter.
// Usage:
//
// cout << "Listing:" << endl
//      << indent(2) 
//      << indent() << "some text" << endl
//      << indent(3)
//      << indent() << "text 1" << endl
//      << indent() << "text 2" << endl
//      << unindent()
//      << indent() << "some other text" << endl
//      << unindent()
//      << indent() << "back to where we started" << endl;
//
// would produce:
// Listing:
//   some text
//      text 1
//      text 2
//   some other text
// back to where we started
//

// only prohibit assignment.
//  Use compiler generated
//   default/copy c'tor; this
//   thang is stateless anyhow...
struct unindent {
	unindent();
	unindent( const unindent& );
private:
	const unindent& operator=( const unindent& );
};

struct indent {
	indent();
	indent( unsigned int howmuch );

	indent( const indent& other );
	const indent& operator=( const indent& other );

private:
	bool            dopush;
	unsigned int    nr;

	// prohibit these
	
	// static stuff
	friend std::ostream& operator<<( std::ostream&, const indent& );
	friend std::ostream& operator<<( std::ostream&, const unindent& );

	static std::deque<unsigned int>    _indents;
};


std::ostream& operator<<( std::ostream& os, const indent& i );
std::ostream& operator<<( std::ostream& os, const unindent& );

// The format(ter) - can be inserted into a stream.
// Allows you to do printf-like formatting on-the-fly!
// eg:
// double    velocity = 2992.3938;
// cout << format("%6.2lf", velocity) << endl;
struct format {
	public:
		format();
		format( const char* fmt, ...);
		format( const format& other );

		const format& operator=( const format& other );

		~format();
	private:
		std::string  buf;
		
		friend std::ostream& operator<<( std::ostream&, const format& );
};

std::ostream& operator<<( std::ostream& os, const format& p );


#endif
