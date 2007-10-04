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

#endif
