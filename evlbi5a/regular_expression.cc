//  Implementation of the RegularExpression class
#include <regular_expression.h>

#include <iostream>
#include <algorithm>

#include <cstdlib>
#include <cstring>

using std::cerr;
using std::endl;
using std::flush;
using std::string;

DEFINE_EZEXCEPT(Regular_Expression_Exception)

// support classes
pcintregmatch_t::pcintregmatch_t()  {
    this->::regmatch_t::rm_so = -1;
    this->::regmatch_t::rm_eo = -1;
}

pcintregmatch_t::pcintregmatch_t( const ::regmatch_t& rm ) {
    this->::regmatch_t::rm_so = rm.rm_so;
    this->::regmatch_t::rm_eo = rm.rm_eo;
}

pcintregmatch_t::operator bool() const {
    return (rm_so!=-1 && rm_eo!=-1);
}

matchresult::matchresult()
{}

matchresult::matchresult( const matchresult::matchvec_t& mv,
                          const char* os ):
    __matches( mv ),
    __org_string( os )
{}
matchresult::matchresult( const matchresult::matchvec_t& mv,
                          const std::string& os ):
    __matches( mv ),
    __org_string( os )
{}

matchresult::operator bool() const {
    return (__matches.size()!=0);
}

matchresult::matchvec_t::value_type matchresult::operator[]( unsigned int m ) const {
    if( m>=__matches.size() ) {
        THROW_EZEXCEPT(Regular_Expression_Exception, 
                       "requesting group " << m << ", but only " << __matches.size() << " groups matched");
    }
    return __matches[m];
}

std::string matchresult::operator[]( const matchvec_t::value_type& m ) const {
    if( !m ) {
        THROW_EZEXCEPT(Regular_Expression_Exception, 
                       "requesting string from invalid group");
    }
    return __org_string.substr(m.rm_so, (m.rm_eo-m.rm_so));
}

std::string matchresult::group( unsigned int m ) const {
    return (*this)[(*this)[m]];
}

bool operator==( const char* tocheck, const Regular_Expression& against ) {
    return against.matches( tocheck );
}

bool operator!=( const char* tocheck, const Regular_Expression& against ) {
    return !(against.matches( tocheck ));
}

bool operator==( const string& s, const Regular_Expression& against ) {
    return against.matches( s.c_str() );
}

bool operator!=( const string& s, const Regular_Expression& against ) {
    return !(against.matches(s.c_str()));
}


Regular_Expression::Regular_Expression( const char* pattern_text, int flags ) :
    myOriginalPattern( ((pattern_text!=0)?(strdup(pattern_text)):(strdup(""))) )
{
    //  Compile the pattern....
    int    r;
    char   errbuf[ 512 ];

    if( (r=::regcomp(&myCompiledExpression,
                     myOriginalPattern,
                     flags))!=0 ) {
        ::regerror(r, &myCompiledExpression, errbuf, sizeof(errbuf));
        THROW_EZEXCEPT(Regular_Expression_Exception,
                       "Failed to compile RegEx(" << myOriginalPattern << "): " << errbuf);
    }
    // After compiling the regexp, the re_nsub member informs how many
    // sub expressions/match groups there were. Together with the zeroth
    // group (the whole) match, we know how many there are in total
    mySubexprs = new ::regmatch_t[ 1 + myCompiledExpression.re_nsub ];
}

Regular_Expression::Regular_Expression( const string& pattern_text, int flags ) :
    myOriginalPattern( ((pattern_text.size()!=0)?(strdup(pattern_text.c_str())):(strdup(""))) )
{
    //  Compile the pattern....
    int    r;
    char   errbuf[ 512 ];

    if( (r=::regcomp(&myCompiledExpression,
                     myOriginalPattern,
                     flags))!=0 ) {
        ::regerror(r, &myCompiledExpression, errbuf, sizeof(errbuf));
        THROW_EZEXCEPT(Regular_Expression_Exception,
                       "Failed to compile RegEx(" << myOriginalPattern << "): " << errbuf);
    }
    // After compiling the regexp, the re_nsub member informs how many
    // sub expressions/match groups there were. Together with the zeroth
    // group (the whole) match, we know how many there are in total
    mySubexprs = new ::regmatch_t[ 1 + myCompiledExpression.re_nsub ];
}
		       

matchresult Regular_Expression::matches( const char* s ) const {
    matchresult   rv;

    if( !s )
        return rv;
   
    if( ::regexec(&myCompiledExpression, s, (size_t)(1+myCompiledExpression.re_nsub), mySubexprs, 0)==0 ) {
        // The "+1" is from the zeroth sub expression (the whole match)
        rv = matchresult(matchresult::matchvec_t(mySubexprs, mySubexprs+myCompiledExpression.re_nsub+), s);
    }
    return rv;
}

matchresult Regular_Expression::matches( const string& s ) const {
    return this->matches( s.c_str() );
}

string Regular_Expression::pattern( void ) const {
    return myOriginalPattern;
}

Regular_Expression::~Regular_Expression() {
    if( myOriginalPattern )
        ::free( myOriginalPattern );
    ::regfree( &myCompiledExpression );
    delete [] mySubexprs;
}

