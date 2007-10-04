// implementation
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
