// implementation
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
#include <stringutil.h>

#include <cctype>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

using std::string;
using std::vector;
using std::remove_if;


struct eraser_type {
    eraser_type(char c):
        thechar( c )
    {}
    bool operator()( char c ) const {
        return thechar==c;
    }

    char thechar;
};

vector<string>  split(const string& str, char c, bool skipempty)  {
    vector<string>    retval;
    string::size_type substart;

    substart = 0;

    while( substart<=str.size() ) {
        string::size_type  next = str.find(c, substart);
        const string       substr = str.substr(substart, (next==string::npos?next:(next-substart))); 

        // empty strings never get skipped unless skipempty==false 
        if( !substr.empty() || skipempty==false)
            retval.push_back(substr);
        substart += (substr.size()+1);
    }
    return retval;
}


// enhanced split, honour escaped split characters
//  (split character preceded by a \ )
vector<string>  esplit( const string& str, char c, bool skipempty ) {
    const char        escape( '\\' );     
    vector<string>    retval;
    string::size_type substart;

    substart = 0;

    std::cout << "esplit('" << str << "', " << c << ")" << std::endl;

    while( substart<=str.size() ) {
        string::size_type searchstart = substart;
        string::size_type sep;

        // keep on scanning characters until we find a non-escaped one
        while( (sep=str.find(c, searchstart))!=string::npos &&
               sep>0 && str[sep-1]==escape )
            searchstart = sep+1;
        // good. sep now points at a non-escaped separation character
        string s = str.substr(substart, (sep==string::npos?sep:(sep-substart)));

        // now erase the escape characters
        s.erase( remove_if(s.begin(), s.end(), eraser_type(escape)), s.end() );

        // Anything left? do we need to skip it?
        if( skipempty==false || (skipempty==true && !s.empty()) )
            retval.push_back(s);

        // Update start of next search - taking care not to go beyond the string
        substart = sep;
        if( substart!=string::npos )
            substart++;
    }
    return retval;
}

string tolower( const string& s ) {
    string                 retval;
    string::const_iterator cptr;

    for( cptr=s.begin(); cptr!=s.end(); cptr++ )
        retval.push_back( (char)::tolower(*cptr) );
    return retval;
}

string toupper( const string& s ) {
    string                 retval;
    string::const_iterator cptr;

    for( cptr=s.begin(); cptr!=s.end(); cptr++ )
        retval.push_back( (char)::toupper(*cptr) );
    return retval;
}

string strip( const string& s ) {
    string::const_iterator bptr = s.begin();
    string::const_iterator eptr = s.end();

    while( bptr!=s.end() && isspace(*bptr) )
        bptr++;
    while( eptr!=s.begin() && isspace(*(eptr-1)) )
        eptr--;
    return string(bptr, eptr);
}

vector<unsigned int> parseUIntRange( const string& s, char sep ) {
    vector<string>       parts( ::split(s, sep) );
    vector<unsigned int> rv;

    for(vector<string>::const_iterator cur=parts.begin();
        cur!=parts.end(); cur++) {
        unsigned int   start, end;
        // Does the string look like "n-m"?
        if( ::sscanf(cur->c_str(), "%u-%u", &start, &end)!=2 ) {
            // no, then try single digit
            if( ::sscanf(cur->c_str(), "%u", &start)!=1 )
                continue;
            else
                end = start;
        }
        for(unsigned int n=start; n<=end; n++)
            rv.push_back( n );
    }
    return rv;
}

std::string tm2vex(const struct tm& time_struct, unsigned int nano_seconds) {
    std::ostringstream reply;

    reply << time_struct.tm_year + 1900 << "y" 
          << std::setfill('0') << std::setw(3) << (time_struct.tm_yday + 1) << "d" 
          << std::setw(2) << time_struct.tm_hour << "h" 
          << std::setw(2) << time_struct.tm_min << "m" 
          << std::fixed << std::setw(7) << std::setprecision(4) << time_struct.tm_sec + nano_seconds / 1000000000.0 << "s";

    return reply.str();
}

std::string from_c_str(const char* str, unsigned int max_chars) {
    std::string ret(str, max_chars);
    size_t terminator = ret.find('\0');
    if ( terminator != std::string::npos ) {
        ret.resize( terminator );
    }
    return ret;
}
