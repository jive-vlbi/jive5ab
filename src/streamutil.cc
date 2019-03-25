// implementations
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
#include <streamutil.h>
#include <iomanip>

#include <cstdio>
#include <cstdarg>

using std::ostream;
using std::string;
using std::setw;

// define the static thing in indent
std::deque<unsigned int>  indent::_indents;



// the pprint thingy
pprint::~pprint() {
	delete nrptr;
}

ostream& operator<<( ostream& os, const pprint& p ) {
    // first: print the 'amount'
	p.nrptr->print_on_stream(os);
	os << " " << p.mystr
	   << (((p.nrptr->operator==(1))==true)?(""):(p.mymult.c_str()));
	return os;
}


// the unindent thing
unindent::unindent()
{}

unindent::unindent( const unindent& )
{}

ostream& operator<<( ostream& os, const unindent& ) {
	indent::_indents.pop_back();
	return os;
}

// the indent thingy. Each time an indent
// is inserted into a stream, something happens,
// dependant on which indent (created with or without
// 'howmuch'):
// without 'howmuch': the current amount of indent of
//   spaces are printed
// with 'howmuch': nothing gets printed, the amount of
//   indent is increased by 'howmuch' of the indent() thingy
//   that's being inserted into the stream
indent::indent() :
	dopush( false ), nr( 0 )
{}

indent::indent( unsigned int howmuch ) :
	dopush( true ), nr( howmuch )
{}

indent::indent( const indent& other ) :
	dopush( other.dopush ), nr( other.nr )
{}

const indent& indent::operator=( const indent& other ) {
	if( this!=&other ) {
		dopush = other.dopush;
		nr     = other.nr;
	}
	return *this;
}

ostream& operator<<( ostream& os, const indent& i ) {
	// decide what to do:
	// if dopush == true -> we just push the value on the deque
	//    (somebody inserts an indent object in a stream which
	//     has come from a c'tor with an argument)
	// if dopush == false -> we insert sum[deque.begin(), deque.end()]
	//     thingys into the stream
	if( i.dopush ) {
		indent::_indents.push_back( i.nr );
	} else {
		unsigned int                             total = 0;
		std::deque<unsigned int>::const_iterator cur = indent::_indents.begin();

        for( cur=indent::_indents.begin();
             cur!=indent::_indents.end();
             cur++ )
            total += *cur;
		os << setw((int)total) << "";
	}
	return os;
}

// the format(ter)
format::format()
{}

format::format(const char* fmt, ...) {
	char    tmp[2048];
	va_list a;

	va_start(a, fmt);
	::vsnprintf(tmp, sizeof(tmp), fmt, a);
	va_end(a);
	buf = tmp;
}

format::format( const format& other ) :
	buf( other.buf )
{}

const format& format::operator=( const format& other ) {
	if( this!=&other )
		buf = other.buf;
	return *this;
}

format::~format()
{}

ostream& operator<<( ostream& os, const format& p ) {
	return os << p.buf;
}

