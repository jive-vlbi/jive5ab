// some string utility functions
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
#ifndef JIVE5A_STRINGUTIL_H
#define JIVE5A_STRINGUTIL_H

#include <vector>
#include <string>
#include <utility>
#include <exception>
#include <iterator>
#include <sstream>
#include <time.h> // struct timespec

// Split a string into substrings at char 'c'
//
// Note: by default the result may contain empty strings,
// eg if more than one separation characters follow each other.
// This is different behaviour than, say, strtok(3) which,
// if >1 separator characters follow each other, it would
// skip over them.
//
// It is possible to mimic this behaviour by setting the
// 'skipempty' to true
//
// E.g.
//    res = ::split( "aa,bb,,dd,", ',' )
//
// would yield the following array:
//   res[0] = "aa"
//   res[1] = "bb"
//   res[2] = ""
//   res[3] = "dd"
//   res[4] = ""
// with 'skipempty==true' it will yield:
//   res[0] = "aa"
//   res[1] = "bb"
//   res[2] = "dd"
//
std::vector<std::string> split( const std::string& str, char c, bool skipempty = false );
// the esplit function behaves like split, only this one deals with
// escapes; it will treat escaped split characters as non-split characters
std::vector<std::string> esplit( const std::string& str, char c, bool skipempty = false );

// Let's define a tolower and toupper for string objects...
std::string toupper( const std::string& s );
std::string tolower( const std::string& s );

// strip whitespace at both ends
std::string strip( const std::string& s );

// Parse a comma separated list of unsigned ints or ranges into one
// no checks for multiple occurrences/overlaps.
// "0-2,4,8-12" => [0,1,2,4,8,9,10,11,12]
std::vector<unsigned int> parseUIntRange( const std::string& s, char sep=',' );

// see strtol(3) - this function mimics that behaviour but returns a 64bit quantity
//uint64_t strtouint64(const std::string& s, std::string::const_iterator* eptr, int base);

// extract strings found within open-close character sequence,
// honouring escapes
// return a vector of start,end iterators, pointing at the
// sections *inside* the enclosing characters
//
// res  = find_enclosed( "..(abc)...(bcd)...", "()" );
//                          ^   ^   ^   ^
//                          i0  i1  i2  i3   (iterators)
// yields
//
// res[0] = pair<char*,char*>(i0+1, i1)
// res[1] = pair<char*,char*>(i2+1, i3)
//
// characters at the locations of the dots are completely
// ignored
struct unbalanced_open_close: public std::exception { };

typedef char    openclose_type[2];


template <typename Iterator>
std::vector< std::pair<Iterator, Iterator> >  find_enclosed(Iterator begin, Iterator end, openclose_type oc, bool skipempty = false) {
#if 0
    struct charfinder {
        charfinder(char c):
            tofind(c)
        {}
        bool operator()(char c) const {
            return c==tofind;
        }
        char tofind;
    };
#endif
    const char                                   escape( '\\' );
    std::vector< std::pair<Iterator, Iterator> > rv;

    while( begin!=end ) {
        Iterator sstart;
        Iterator send;

        // look for sequence opening character
        sstart = begin;
        while( sstart!=end ) {
            if( *sstart==oc[0] && (sstart==begin || *(sstart-1)!=escape) )
                break;
            sstart++;
        }
        // find the closing character - it is a syntax error
        // if there is an opening but not a closing character
        send = (sstart!=end?sstart+1:sstart);
        while( send!=end ) {
            if( *send==oc[1] && (send==begin || *(send-1)!=escape) )
                break;
            send++;
        }
        // if not both not found or found that is an error
        if( !((sstart==end && send==end) || (sstart!=end && send!=end)) )
            throw unbalanced_open_close();
        // cool, either we have nothing or we have a part.
        // if we have a part we remove the escape character
        if( sstart!=end )
            if( !skipempty || (skipempty && (sstart+1)!=send) )
                rv.push_back( make_pair(sstart+1, send) );
//            rv.push_back( make_pair(sstart+1, remove_if(sstart+1, send, charfinder(escape))) );
        begin = (send!=end?send+1:send);
    }
    return rv;
}


template <typename T, size_t N>
size_t arraysize( T(&)[N] ) {
    return N;
}

// a negative nano_seconds value means unknown and will print '*'s
std::string tm2vex(const struct tm& time_struct, long nano_seconds = 0);

std::string from_c_str(const char* str, unsigned int max_chars);

// Convert anything that supports "ostream& operator<<(ostream&, <type>)"
// into std::string
template <typename T>
std::string repr(const T& t) {
    std::ostringstream oss;
    oss << t;
    return oss.str();
}

// Define "output string iterator" - appends to string with a PREFIX separator.
// (thus the string "<separator><item>" will be added). Wether or not the
// first item added will have a separator prefixed is settable via a
// constructor argument
//
// This allows the use of "std::copy(iter, iter, ostringiterator(str, sep))"
// to form a string
//
// For std::copy(inputiter, inputiter, outputiter) the
// following semantics must be honoured:
//   operator*   (dereferencing)
//   operator++  (post-fix increment)
//
// and because "operator*" will return reference to self,
// we must also implement:
//   operator=   (assignment)
struct ostringiterator: public std::iterator<std::output_iterator_tag, ostringiterator> {

    // 'startWithSep' indicates wether to start with a separator or not, the
    // first time an element is added
    ostringiterator(std::string& s, const std::string& sep, bool startWithSep=false);

    virtual ostringiterator& operator*(void);

    // pre- and post-fix increment
    ostringiterator& operator++();
    ostringiterator& operator++(int);

    virtual ostringiterator& operator=(const std::string& addendum);

    virtual ~ostringiterator();

    private:
        bool              do_sep;
        std::string*      sptr;
        const std::string separator;

        // No default c'tor or copy c'tor
#if __cplusplus >= 201103L
        // this how that's done in c++11 happy land
        ostringiterator() = delete;
        ostringiterator& operator=(const ostringiterator&) = delete;
#else
        // meh ...
        ostringiterator();
        ostringiterator& operator=(const ostringiterator&);
#endif
};

// Id. but now for any basic_ostream<> and separator
template <typename separator_type, typename stream_type = std::ostream>
struct ostream_prefix_inserter:
    public std::iterator<std::output_iterator_tag, ostream_prefix_inserter<separator_type, stream_type> >
{
    typedef ostream_prefix_inserter<separator_type, stream_type> self_type;

    // 'startWithSep' indicates wether to start with a separator or not, the
    // first time an element is added
    ostream_prefix_inserter(stream_type& s, separator_type const& sep, bool startWithSep=false):
        __m_do_sep( startWithSep ), __m_sptr( &s ), __m_sep( sep )
    {}

#if __cplusplus >= 201103L
    // And ... because we have defined a home-grown move c'tor, in C++11 the
    // copy c'tor is delected and we have to opt-in.
    // (see https://stackoverflow.com/a/11255258)
    //
    // For STL algorithms (where the main use case of this class lie anyway)
    // the objects must be copy-constructible/freely copyable.
    ostream_prefix_inserter(self_type const& other) = default;

    // This only works/is available in C++11 (and up) happy land
    ostream_prefix_inserter(self_type&& other) :
        __m_do_sep( other.__m_do_sep ), __m_sptr( other.__m_sptr ), __m_sep( other.__m_sep )
    {}
#endif

    // These operators don't do nothing
    virtual self_type& operator*(void) { return *this; }
    self_type& operator++()            { return *this; }
    self_type& operator++(int)         { return *this; }

    // Insert something to the stream
    template <typename T>
    self_type& operator=(const T& addendum) {
        if( __m_do_sep )
            *__m_sptr << __m_sep;
        *__m_sptr << addendum;
        __m_do_sep = true;
        return *this;
    }

    virtual ~ostream_prefix_inserter() {}

    private:
        bool            __m_do_sep;
        stream_type*    __m_sptr;
        separator_type  __m_sep;

        // No default c'tor or assignment
        ostream_prefix_inserter();
        self_type& operator=(self_type const&);
};

#if __cplusplus >= 201103L
// This only works/is available in C++11 (and up) happy land
template <typename Sep, typename Stream>
ostream_prefix_inserter<Sep, Stream>    mk_prefix_inserter(Sep const& sep, Stream& stream, bool start_w_sep=false) {
    return ostream_prefix_inserter<Sep, Stream>(sep, stream, start_w_sep);
}
#endif

#endif
