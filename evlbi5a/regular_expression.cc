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
    matchvec_t::value_type   rv;
    if( m<__matches.size() ) {
        rv = __matches[m];
    }
    else {
        THROW_EZEXCEPT(Regular_Expression_Exception, 
                       "requesting group " << m << ", but only " << __matches.size() << " groups matched");
    }
    return rv;
}

std::string matchresult::operator[]( const matchvec_t::value_type& m ) const {
    if( m ) {
        return __org_string.substr(m.rm_so, (m.rm_eo-m.rm_so));
    }
    else {
        THROW_EZEXCEPT(Regular_Expression_Exception, 
                       "requesting string from invalid group");
    }
    return string();
}

std::string matchresult::matchgroup( unsigned int m ) {
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


Regular_Expression::Regular_Expression( const char* pattern,
                                        unsigned int maxmatch ) :
    myOriginalPattern( ((pattern!=0)?(strdup(pattern)):(strdup(""))) ),
    nmatch( std::max((unsigned int)1,maxmatch) )
{
    //  Compile the pattern....
    int    r;
    char   errbuf[ 512 ];

    if( (r=::regcomp(&myCompiledExpression,
                     myOriginalPattern,
                     REG_EXTENDED))!=0 ) {
        ::regerror(r, &myCompiledExpression, errbuf, sizeof(errbuf));
        THROW_EZEXCEPT(Regular_Expression_Exception,
                       "Failed to compile RegEx(" << myOriginalPattern << "): " << errbuf);
    }
}

Regular_Expression::Regular_Expression( const string& pattern,
                                        unsigned int maxmatch ) :
    myOriginalPattern( ((pattern.size()!=0)?(strdup(pattern.c_str())):(strdup(""))) ),
    nmatch( std::max((unsigned int)1,maxmatch) )
{
    //  Compile the pattern....
    int    r;
    char   errbuf[ 512 ];

    if( (r=::regcomp(&myCompiledExpression,
                     myOriginalPattern,
                     REG_EXTENDED))!=0 ) {
        ::regerror(r, &myCompiledExpression, errbuf, sizeof(errbuf));
        THROW_EZEXCEPT(Regular_Expression_Exception,
                       "Failed to compile RegEx(" << myOriginalPattern << "): " << errbuf);
    }
}
		       

matchresult Regular_Expression::matches( const char* s ) const {
    matchresult   rv;
    
    if( s ) {
        int             m;
        ::regmatch_t*   rms = new ::regmatch_t[nmatch];

        m = ::regexec(&myCompiledExpression, s, (size_t)nmatch, rms, 0);
        if( m==0 ) {
            // ok, matched

            // we can do this since we've made sure
            // that nmatch is AT LEAST 1 (see c'tor)
            // Find the last matchgroup that is valid
            // Between 0 and the last valid matchgroup
            // there may be invalid(=empty) matchgroups, eg with
            // alternatives
            unsigned int  n = nmatch-1;
            while( n && rms[n].rm_so==-1 ) 
                n--;

            // we can do (rms, rms+n+1) because we KNOW the rx matched
            // so we have *at least* the whole thing matching, so rms[0] is
            // *always* filled in
            rv = matchresult(matchresult::matchvec_t(rms, rms+n+1), s);
        }
        /*
        else {
            char regex_error_buffer[512];
            ::regerror(m, &myCompiledExpression, &regex_error_buffer[0], sizeof(regex_error_buffer));
            cerr << "regex error: " << regex_error_buffer << endl;

        }
        */
        delete[] rms;
    }
    return rv;
}

matchresult Regular_Expression::matches( const string& s ) const {
    return this->matches( s.c_str() );
}



Regular_Expression::~Regular_Expression() {
    if( myOriginalPattern )
        ::free( myOriginalPattern );
    ::regfree( &myCompiledExpression );
}

