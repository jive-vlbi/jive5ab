// some utilities for pretty printing on streams and other stuff
//
//# $Id$
//
//# $Log$
//# Revision 1.1  2007/10/04 15:04:41  jive_cc
//# HV: Weeheee! A rewrite that does support non-crashing and non-hanging
//#     switching of transfermodes!
//#     Only n/w performance (receiving) is on the Fritz :(
//#     Have Idea(tm) but first save this version for posterity
//#
//# Revision 1.7  2005/03/18 11:08:59  verkout
//# ${logmsg}
//#
//# Revision 1.6  2004/11/03 21:51:14  verkout
//# HV: - Better use of std:: and :: (global) namespaces
//#     - changed implementation of pprint to method a la
//#       used in 'hex.h' [in this dir]
//#     - Removed bostrstream. Now using *stringstream
//#
//# Revision 1.5  2003/03/26 10:24:07  verkout
//# HV: Added a manipulator, printeffer, which you can use
//#     to format data as you would with printf().
//#     Example usage (to eg. print a hexadecimal value)
//#     cout << printeffer("0x%08x",hexval) << endl;
//#
//# Revision 1.4  2003/02/18 13:27:35  verkout
//# HV: The indent objects now are copieable/assignable
//#
//# Revision 1.3  2002/11/15 08:13:38  verkout
//# HV: Added bostrstream - a wrapper around std::ostrstream. This class does the buffer management for you. Just create with the size of the buffer and this class will take care of allocation/de-allocation of the buffer for std::ostrstream.
//#
//# Revision 1.2  2002/06/13 08:17:27  loose
//# Changed "string" into "std::string"
//#
//# Revision 1.1  2002/06/05 14:29:39  verkout
//# HV: Removed the pretty print from stringutil and stuffed it in streamutil. Created the indent/unindent stuff. Can be found in streamutil as well.
//#
#ifndef STREAMUTIL_H
#define STREAMUTIL_H

#include <deque>
#include <string>
#include <iostream>

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

    // make sure it's comparable to unsigned long long
    // (it will be tested agains "1" to see if the multiple
    // needs to be printed
	virtual bool operator==( unsigned long long v ) const = 0;

	// this'un don't need to be virtual...
	bool operator!=( unsigned long long v ) const {
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
	virtual bool operator==( unsigned long long v ) const {
		return ((unsigned long long)nr==v);
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
