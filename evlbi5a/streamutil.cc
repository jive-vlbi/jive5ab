//# $Id$
//
//# $Log$
//# Revision 1.1  2007/10/04 15:04:40  jive_cc
//# HV: Weeheee! A rewrite that does support non-crashing and non-hanging
//#     switching of transfermodes!
//#     Only n/w performance (receiving) is on the Fritz :(
//#     Have Idea(tm) but first save this version for posterity
//#
//# Revision 1.6  2005/03/18 10:53:19  verkout
//# ${logmsg}
//#
//# Revision 1.5  2004/11/03 21:51:14  verkout
//# HV: - Better use of std:: and :: (global) namespaces
//#     - changed implementation of pprint to method a la
//#       used in 'hex.h' [in this dir]
//#     - Removed bostrstream. Now using *stringstream
//#
//# Revision 1.4  2003/03/26 10:24:07  verkout
//# HV: Added a manipulator, printeffer, which you can use
//#     to format data as you would with printf().
//#     Example usage (to eg. print a hexadecimal value)
//#     cout << printeffer("0x%08x",hexval) << endl;
//#
//# Revision 1.3  2003/02/18 13:27:35  verkout
//# HV: The indent objects now are copieable/assignable
//#
//# Revision 1.2  2002/11/15 08:13:35  verkout
//# HV: Added bostrstream - a wrapper around std::ostrstream. This class does the buffer management for you. Just create with the size of the buffer and this class will take care of allocation/de-allocation of the buffer for std::ostrstream.
//#
//# Revision 1.1  2002/06/05 14:29:39  verkout
//# HV: Removed the pretty print from stringutil and stuffed it in streamutil. Created the indent/unindent stuff. Can be found in streamutil as well.
//#
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
		unsigned int                              total = 0;
		std::deque<unsigned int>::const_iterator  cur = indent::_indents.begin();

        for( cur=indent::_indents.begin();
             cur!=indent::_indents.end();
             cur++ )
            total += *cur;
		os << setw(total) << "";
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

