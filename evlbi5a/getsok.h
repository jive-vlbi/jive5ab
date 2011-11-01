// methods that create/configure sockets as per what we think are sensible defaults
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
#ifndef JIVE5A_GETSOK_H
#define JIVE5A_GETSOK_H

#include <string>
#include <map>

// for "struct sockaddr"
#include <sys/socket.h>

// Open a connection to <host>:<port> via the protocol <proto>.
// Returns the filedescriptor for this open connection.
// It will be in blocking mode.
// Throws if something fails.
int getsok( const std::string& host, unsigned short port, const std::string& proto );

// client connection to Unix domain socket.
// Same behaviour as the IPv4 one above.
int getsok_unix_client( const std::string& path );
int getsok_unix_server( const std::string& path );

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
