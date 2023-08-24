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
#include <algorithm>

using namespace std;

// some constant string-valued defaults for netparm
const std::string defProtocol            = std::string("tcp");
const std::string defUDPHelper           = std::string("smart");
const hpslist_type netparms_type::defHPS = hpslist_type( hpslist_type::size_type(1) );

hps_type::hps_type():
    port( netparms_type::defPort )
{}
hps_type::hps_type(std::string const &h, unsigned short p, std::string const& s):
    host( h ), port( p ), suffix( s )
{}

// construct a default network parameter setting thingy
netparms_type::netparms_type():
    rcvbufsize( netparms_type::defSockbuf ), sndbufsize( netparms_type::defSockbuf )
    , interpacketdelay_ns( netparms_type::defIPD )
    , theoretical_ipd_ns( netparms_type::defIPD )
    , ackPeriod( netparms_type::defACK )
    , nblock( netparms_type::defNBlock )
    , protocol( defProtocol ), mtu( netparms_type::defMTU )
    , blocksize( netparms_type::defBlockSize )
#if 0
    , port( netparms_type::defPort )
#endif
#if 0
    , nmtu( netparms_type::nMTU )
#endif
    // the default c'tor for hps_type() does its thing so
    // we just create a vector of size one
    , __m_hps( netparms_type::defHPS )
{
    this->analyzeSuffixes();
}

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

#if 0
void netparms_type::set_port( unsigned short portnr ) {
    port = portnr;
    if( port==0 )
        port = netparms_type::defPort;
    return;
}
#endif

void netparms_type::set_ack( int ack ) {
    ackPeriod = ack;
    if( ackPeriod==0 )
        ackPeriod = netparms_type::defACK;
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

void netparms_type::set_host( std::string const& h) {
    __m_hps[0].host = h;
}

void netparms_type::set_hps( hpslist_type const& hps ) {
    if( hps.empty() )
        __m_hps = netparms_type::defHPS;
    else
        __m_hps = hps;
    this->analyzeSuffixes();
}

void netparms_type::rotate( void ) {
    DEBUG(1, "netparms: rotate " << __m_hps.size() << " elements" << std::endl);
    std::rotate(__m_hps.begin(), __m_hps.begin()+1, __m_hps.end());
}

void netparms_type::analyzeSuffixes( void ) {
    unsigned int           streamId( 0 );
    std::set<std::string>  nonEmptySuffixes;

    // Start with clear sheet
    __m_suffixmap.clear();

    // in c++11 would use lambda to extract suffix and std::copy() into
    // set's insert iterator ...
    for( hpslist_type::const_iterator p=__m_hps.begin(); p!=__m_hps.end(); p++) {
        EZASSERT2( __m_suffixmap.insert(suffixmap_type::value_type(streamId++, p->suffix)).second, std::runtime_error,
                   EZINFO("Failure trying to insert net_port stream=>suffix map entry?") );
        if( !p->suffix.empty() )
            nonEmptySuffixes.insert( p->suffix );
    }
    __m_n_non_empty_suffixes = nonEmptySuffixes.size();
    return;
}

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

