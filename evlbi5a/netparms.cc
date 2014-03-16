// implementation of the netparms methods
// such that we can pass this around
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
#include <netparms.h>
#include <dosyscall.h>

using namespace std;

// some constant string-valued defaults for netparm
const std::string defProtocol = std::string("tcp");
const std::string defUDPHelper = std::string("smart");

// construct a default network parameter setting thingy
netparms_type::netparms_type():
    rcvbufsize( netparms_type::defSockbuf ), sndbufsize( netparms_type::defSockbuf )
    , interpacketdelay_ns( netparms_type::defIPD )
    , theoretical_ipd_ns( netparms_type::defIPD )
    , nblock( netparms_type::defNBlock )
    , protocol( defProtocol ), mtu( netparms_type::defMTU )
    , blocksize( netparms_type::defBlockSize )
    , port( netparms_type::defPort )
#if 0
    , nmtu( netparms_type::nMTU )
#endif
{}

void netparms_type::set_protocol( const std::string& p ) {
    protocol = p;

    // do silent transformations
    if( protocol=="udp" )
        protocol="udps";
    if( protocol=="pudp" )
        protocol="udp";

    if( protocol.empty() )
        protocol = defProtocol;
}

void netparms_type::set_mtu( unsigned int m ) {
    mtu = m;
    if( !mtu )
        mtu = netparms_type::defMTU;
}

void netparms_type::set_blocksize( unsigned int bs ) {
    blocksize = bs;
    if( blocksize==0 )
        blocksize = netparms_type::defBlockSize;
}

void netparms_type::set_port( unsigned short portnr ) {
    port = portnr;
    if( port==0 )
        port = netparms_type::defPort;
    return;
}

#if 0
void netparms_type::set_nmtu( unsigned int n ) {
    nmtu = n;
    if( !nmtu )
        nmtu = netparms_type::nMTU;
    constrain();
}
#endif

// Helper functions

int ipd_us( const netparms_type& np ) {
    return ipd_ns(np) / 1000;
}

int ipd_ns( const netparms_type& np ) {
    return np.interpacketdelay_ns < 0 ? np.theoretical_ipd_ns : np.interpacketdelay_ns;
}

// Return the actual value that was set
int ipd_set_us( const netparms_type& np ) {
    return np.interpacketdelay_ns / 1000;
}

int ipd_set_ns( const netparms_type& np ) {
    return np.interpacketdelay_ns;
}

// Return the theoretical value that was set
int theoretical_ipd_us( const netparms_type& np ) {
    return np.theoretical_ipd_ns / 1000;
}
int theoretical_ipd_ns( const netparms_type& np ) {
    return np.theoretical_ipd_ns;
}

