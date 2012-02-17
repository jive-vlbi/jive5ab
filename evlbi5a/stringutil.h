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
#ifndef STRINGUTIL_H
#define STRINGUTIL_H

#include <vector>
#include <string>

// Split a string into substrings at char 'c'
// Note: the result may contain empty strings, eg if more than one separation
// characters follow each other. This is different behaviour than, say,
// strtok(3) which, if >1 separator characters follow each other, it would
// skip over them...
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
std::vector<std::string> split( const std::string& str, char c );
// the esplit function behaves like split, only this one deals with
// escapes; it will treat escaped split characters as non-split characters
std::vector<std::string> esplit( const std::string& str, char c );

// Let's define a tolower and toupper for string objects...
std::string toupper( const std::string& s );
std::string tolower( const std::string& s );

// strip whitespace at both ends
std::string strip( const std::string& s );

// Parse a comma separated list of unsigned ints or ranges into one
// no checks for multiple occurrences/overlaps.
// "0-2,4,8-12" => [0,1,2,4,8,9,10,11,12]
std::vector<unsigned int> parseUIntRange( const std::string& s, char sep=',' );
#endif
