// group all mark5 commands together in one location
#ifndef JIVE5A_MK5COMMAND_H
#define JIVE5A_MK5COMMAND_H

#include <runtime.h>
#include <map>
#include <string>
#include <vector>
#include <exception>

// exception thrown when failure to insert command into
// mk5a commandset map
struct cmdexception:
    public std::exception
{
    cmdexception( const std::string& m );
    virtual const char* what( void ) const throw();
    virtual ~cmdexception() throw();

    const std::string __msg;
};


// map commands to functions.
// The functions must have the following signature.
//
// The functions take a (bool, const vector<string>&, runtime&),
// the bool indicating query or not, the vector<string>
// the arguments to the function and the 'runtime' environment upon
// which the command may execute. Obviously it's non-const... otherwise
// you'd have a rough time changing it eh!
//
// Note: fn's that do not need to access the environment (rarely...)
// may list it as an unnamed argument...
// Return a reply-string. Be sure to fully format the reply
// (including semi-colon and all)
//
// NOTE: the first entry in the vector<string> is the command-keyword
// itself, w/o '?' or '=' (cf. main() where argv[0] is the
// programname itself)
typedef std::string (*mk5cmd)(bool, const std::vector<std::string>&, runtime& rte);

// this is our "dictionary"
typedef std::map<std::string, mk5cmd>  mk5commandmap_type;

// This method will (obviously) not create a gazillion of instances
// but fills the map the first time you ask for it.
// Throws cmdexception() if it fails to insert (or
// something else goes wrong).
const mk5commandmap_type& make_mk5commandmap( void );

#endif
