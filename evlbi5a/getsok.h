// methods that create/configure sockets as per what we think are sensible defaults
#ifndef JIVE5A_GETSOK_H
#define JIVE5A_GETSOK_H

#include <string>
#include <map>

// Open a connection to <host>:<port> via the protocol <proto>.
// Returns the filedescriptor for this open connection.
// It will be in blocking mode.
// Throws if something fails.
int getsok( const std::string& host, unsigned short port, const std::string& proto );

// Get a socket for incoming connections.
// The returned filedescriptor is in blocking mode.
//
// You *must* specify the port/protocol. Optionally specify
// a local interface to bind to. If left empty (which is
// default) bind to all interfaces.
int getsok(unsigned short port, const std::string& proto, const std::string& local = "");

// set the blockiness of the given filedescriptor.
// Throws when something fishy.
void setfdblockingmode(int fd, bool blocking);

// Tying properties to a filedescriptor
// For now just the remote host:port adress
typedef std::map<int, std::string>   fdprops_type;

// perform an accept on 'fd' and return something that is
// suitable for insertion in a 'fdprops_type' typed variable
fdprops_type::value_type do_accept_incoming( int fd );

#endif
