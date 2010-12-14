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

using std::string;
using std::vector;

vector<string>  split( const string& str, char c )  {
	vector<string>    retval;
	string::size_type substart;
	string::size_type subend;

	substart = 0;
	subend   = str.size();

	while( str.size() ) {
		string            substr = str.substr( substart, subend );
		string::size_type ssubend;

		if( (ssubend=substr.find(c))==string::npos ) {
			retval.push_back(substr);
			break;
		} else {
			string   tmp = substr.substr(0, ssubend);

			retval.push_back( tmp );
		}

		substart += (ssubend+1);
		subend    = str.size();
	}
	return retval;
}

// enhanced split, honour escaped split characters
//  (split character preceded by a \ )

vector<string>  esplit( const string& str, char c ) {
    const char        escape( '\\' );     
	vector<string>    retval;
	string::size_type substart;
	string::size_type subend;

	substart = 0;
	subend   = str.size();

	while( str.size() ) {
		string            substr = str.substr( substart, subend );
		string::size_type ssubend = 0;

        // find the first occurrence of split character
        // that is NOT preceded by a backslash
        while( (ssubend=substr.find(c, ssubend))!=string::npos &&
               ssubend>0 &&
               substr[ssubend-1]==escape ) {
            // remove the escape-char
            substr.erase(ssubend-1, 1);
        }
        // check what to do
		if( ssubend==string::npos ) {
			retval.push_back(substr);
			break;
		} else {
			string   tmp = substr.substr(0, ssubend);

			retval.push_back( tmp );
		}

		substart += (ssubend+1);
		subend    = str.size();
	}
	return retval;
}

string tolower( const string& s ) {
	string                 retval;
	string::const_iterator cptr;

	for( cptr=s.begin(); cptr!=s.end(); cptr++ )
		retval.push_back( ::tolower(*cptr) );
	return retval;
}

string toupper( const string& s ) {
	string                 retval;
	string::const_iterator cptr;

	for( cptr=s.begin(); cptr!=s.end(); cptr++ )
		retval.push_back( ::toupper(*cptr) );
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
