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
#ifndef JIVE5A_GETSOKUDT_H
#define JIVE5A_GETSOKUDT_H

#include <string>
#include <getsok.h>        // for ::resolve_host() and fdprops_type
#include <libudt5ab/ccc.h> // for the congestion control base class


// Open an UDT connection to <host>:<port>
// Returns the filedescriptor for this open connection.
// It will be in blocking mode.
// Throws if something fails.
int getsok_udt( const std::string& host, unsigned short port, const std::string& proto, const unsigned int mtu);

// Get a socket for incoming connections.
// The returned filedescriptor is in blocking mode.
//
// You *must* specify the port/protocol. Optionally specify
// a local interface to bind to. If left empty (which is
// default) bind to all interfaces.
int getsok_udt(unsigned short port, const std::string& proto, const unsigned int mtu, const std::string& local = "");

// perform an accept on 'fd' and return something that is
// suitable for insertion in a 'fdprops_type' typed variable.
fdprops_type::value_type do_accept_incoming_udt( int fd );

// All our UDT sockets will be constructed with a modified congestion
// control algorithm - the default CUDTCC (seems to work nicely) but we
// add the possibility of adjusting the sending rate on the fly
// by modifiying the ipd; much like we do with all our other UDP based
// transfers
class IPDBasedCC:
    public CUDTCC
{
    public:
        // Default c'tor - initial ipd will be 0
        // i.e. no rate control
        IPDBasedCC();

        // We only overload the onACK because that's
        // where the rate limiting occurs
        virtual void onACK(const int32_t& seqno);

        // We support one method - setting the ipd in ns.
        void set_ipd(unsigned int ipd_in_ns);

        virtual ~IPDBasedCC();

    private:
        unsigned int  _ipd_in_ns;
};

#endif
