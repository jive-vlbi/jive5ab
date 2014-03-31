//
//  Wrapper class around POSIX regex, imported from pcint
//
#ifndef REGULAR_EXPRESSION_H
#define REGULAR_EXPRESSION_H

#include <ezexcept.h>

#include <sys/types.h>
#include <regex.h>
#include <string>
#include <vector>

DECLARE_EZEXCEPT(Regular_Expression_Exception)

// wrapper around regmatch_t which
// allows to give it some extra functionality
struct pcintregmatch_t:
    ::regmatch_t
{
    // creates invalid one
    pcintregmatch_t();

    // init with a given regmatch
    pcintregmatch_t( const ::regmatch_t& rm );

    // this'un valid?
    // regex(3) sais that an invalid
    // regmatch has -1 for both offsets
    operator bool() const;

};


// matchresult holds a vector
// of matches. Size <> 0 indicates
// a successfull match
// Size >1 implies possible matchgroups
// Not all matchgroups are required to be valid
struct matchresult {
    typedef std::vector<pcintregmatch_t>  matchvec_t;
    
    // empty matchresult
    matchresult();
    
    // fully construct, a list of matches (possibly
    // empty) AND a pointer to the original string
    matchresult( const matchvec_t& mv, const char* os );
    matchresult( const matchvec_t& mv, const std::string& os );
    
    // implement operator bool
    // which tells the truthiness 
    // of the matchresult
    operator bool() const;

    // return the m'th regmatch.
    // Most likely use:
    // matchresult m = rx.match(<some string>);
    // if( m[1] )
    //    cout << "matchgroup 1=" << m.matchgroup(1) << endl;
    matchvec_t::value_type operator[]( unsigned int m ) const;

    // nice.. operator overloading :)
    std::string            operator[]( const matchvec_t::value_type& m ) const;

    // snip the indicated match out of
    // the original string. May be empty if
    // the entry was invalid

    // string interface, caller doesn't
    // have to do mem.management, the
    std::string group( unsigned int m ) const;
	
    // our attributes	
    matchvec_t  __matches;
    std::string __org_string;
};

//  A generic regex....
class Regular_Expression
{
    friend bool operator==( const char* tocheck, const Regular_Expression& against );
    friend bool operator!=( const char* tocheck, const Regular_Expression& against );
    friend bool operator==( const std::string& s, const Regular_Expression& against );
    friend bool operator!=( const std::string& s, const Regular_Expression& against );

 public:
    // Create from a pattern, 
    // note that one match for the whole pattern will be returned in matches()[0]
    // so maxmatch should be at least <number of expectect matches> + 1
    Regular_Expression( const char* pattern,  int flags=REG_EXTENDED );
    Regular_Expression( const std::string& pattern, int flags=REG_EXTENDED );
    
    // match the string 's' against this RX
    matchresult  matches( const char* s ) const;
    matchresult  matches( const std::string& s ) const;

    // Return the pattern
    std::string  pattern( void ) const;

    //  Delete all allocated stuff
    ~Regular_Expression();

 private:
    //  Our private parts
    char*         myOriginalPattern;
    regex_t       myCompiledExpression;
    ::regmatch_t* mySubexprs;
    
    
    //  Prohibit these
    Regular_Expression();
    Regular_Expression( const Regular_Expression& );
    Regular_Expression& operator=( const Regular_Expression& );
};

#endif
