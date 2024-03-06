// network parameters with sensible defaults 
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
#ifndef JIVE5A_NETPARMS_H
#define JIVE5A_NETPARMS_H

#include <string>
#include <map>
#include <vector>
#include <trackmask.h>

// Collect together the network related parameters
// typically, net_protocol modifies these
extern const std::string  defProtocol;// = std::string("tcp");
extern const std::string  defUDPHelper;// = std::string("smart");

// Aug 2023 MV It's about time we started supporting
//             net_port = [ip1@]port1[=suffix1] [ : [ip2@]port2[=suffix2] ... ];
//             to be able to easily read from different sockets
//             We want each of those streams to be tagged so
//             it is at least possible to name them differently.
//             But we need to keep that info together,
//             host-port-suffix, or hps
struct hps_type {
    std::string     host;
    unsigned short  port;
    std::string     suffix;

    // empty host (will be treated as IPv4 0.0.0.0),
    // default port as compiled in
    // empty suffix
    hps_type();
    hps_type( std::string const& h, unsigned short p, std::string const& s = std::string());
};

// Sometimes we want to keep a list of those
typedef std::vector<hps_type>               hpslist_type;
typedef std::map<unsigned int, std::string> suffixmap_type;


struct netparms_type {
    // Defaults, for easy readability

    // default inter-packet-delay (none)
    // [meant for links that don't get along with bursty traffik]
    static const unsigned int   defIPD       = 0;
    // 20 May 2016: Add acknowledgment period: every ACK'th packet gets backtraffic
    //              (when reading plain-UDP or UDP-with-sequence-numbers)
    //              Paul Boven et. al. experimentally verified that once
    //              every 10 packets:
    //                (i)  does not generate too much overhead or data
    //                (ii) prevents packet loss because some equipment
    //                     between sender and us may forget (or w/o
    //                     backtraffic never even learn) our MAC address
    static const int            defACK       = 10;
    // default dataport
    static const unsigned short defPort      = 2630;
    // default MTU + how manu mtu's a datagram should span
    static const unsigned int   defMTU       = 1500;
    static const unsigned int   nMTU         = 1;
    // number of blocks + size of individual blocks
    static const unsigned int   defNBlock    = 8;
    static const unsigned int   defBlockSize = 128*1024;
    // OS socket rcv/snd bufsize
    static const unsigned int   defSockbuf   = 4 * 1024 * 1024;
    static const hpslist_type   defHPS       /*= hpslist_type(1)*/;

    // comes up with 'sensible' defaults
    netparms_type();

    // netprotocol values
    int                rcvbufsize;
    int                sndbufsize;
    // We record ipd now in ns internally.
    // Below are helpers to get the actual ipd in a specific unit
    int                interpacketdelay_ns;
    int                theoretical_ipd_ns;
    int                ackPeriod;
    unsigned int       nblock;

    // 
    // various parts in "the system" know about the following set of
    // protocols:
    //   tcp|udp  - exactly as you'd expect, plain old TCP/IPv4 and UDP/IPv4
    //   rtcp     - exactly as tcp only implies switching of roles of
    //              client and server. everywhere where tcp=>client,
    //              rtcp=>server and where tcp=>server, rtcp=>client. mainly
    //              never used to get through firewalls where the server
    //              normally wouldn't allow incoming traffic you can easily
    //              switch the initiator of the connection. the direction,
    //              content and other properties of the datatransfer are
    //              unchanged if compared with tcp
    //   unix     - support unix socket protocols. The "host" setting is
    //              taken to be the filename of the socket. MTU does not
    //              apply
    //   udps     - the s stands for "smart" or "sequencenumber". it implies
    //              that, immediately following the UDP/IPv4 header there is
    //              a strict monotonically incrementing 64bit sequencenumber
    //              such that each next packet sent has a sequencenumber
    //              which is exactly 1 higher than the sequencenumber in the
    //              previous packet. this allows the receiver to put the
    //              packets back in the order they were sent AND detect lost
    //              packets.
    //   udpsnor  - like udps, expect a 64-bit sequence number prepended.
    //              Unlike udps, the sequence number will NOT be used for
    //              REORDERING ("nor" - "no reordering") the packets or filling
    //              in lost packets.
    //              The statistics of the sequence numbers will, however, still
    //              be kept up-to-date; i.e. the "evlbi?" query will still be
    //              informative.
    //
    //  Some protocol names get translated to a different protocol internally.
    //  The table below lists the affected protocols. Strings not listed in the
    //  table will be kept as-is. There is no guarantee that what you store in
    //  the protocol is actually supported by the code, besides the ones listed
    //  above.
    //  
    //  argument to set_protocol        what the protocol gets set to
    //  ------------------------        -----------------------------
    //  udp                             udps [historical reasons: eVLBI + udp meant upds, so it's the default]
    //  pudp                            udp  ["plain" udp - no 64 bit sequence number prepended]
    //
    //  The reasoning is that the code creating a socket will, conceptually,
    //  perform an action like this:
    //
    //  if( netparms.get_protocol.find("udp")!=string::npos )
    //      // udp and udps all require a UDP socket
    //      fd = open_udp_socket();
    //  else if( netparms.get_protocol.find("tcp")
    //      // tcp, rtcp require a TCP socket
    //      fd = open_tcp_socket();
    //
    // And later on drop into the actual network writer:
    //   if( netparms.get_protocol()=="udps" )
    //      udpswriter();
    //   else if( netparms.get_protocol()=="..." )
    //      specificwriter();
    //   &cet
    //
    // p.empty()==true => reset to default (defProtocol)
    void set_protocol( const std::string& p="" );
    // m==0 => reset to default (defMTU)
    void set_mtu( unsigned int m=0 );
    // bs==0 => reset to default (defBlockSize)
    void set_blocksize( unsigned int bs=0 );
#if 0
    // the API for getting host/port is going to change
    // portnr==0 => reset to default (defPort)
    void set_port( unsigned short portnr=0 );
#endif
    // ack==0 => reset to default (defACK)
    void set_ack( int ack=0 );
    // for backwards compatibility code that used to do
    // "np.host = <some string>" can now do
    // "np.set_host( <some string> )"
    void set_host( std::string const& h = std::string() );
    // empty list = go back to default
    void set_hps( hpslist_type const& hps = hpslist_type() );

    // Note: the following method is implemented but 
    // we're not convinced that nmtu/datagram > 1
    // is usefull. At least this allows us to
    // play around with it, if we feel like it.
    // Passing '0' => reset to default
    //void set_nmtu( unsigned int n ); 

    // and be able to read them back
    inline std::string  get_protocol( void ) const {
        return protocol;
    }
    inline unsigned int get_mtu( void ) const {
        return mtu;
    }
    // given the current protocol + MTU return what
    // the maximum payload is in one packet
    inline unsigned int get_max_payload( void ) const {
        unsigned int        hdr = 20;

        // all flavours of tcp protocol have at least 6 4-byte words of
        // tcp header after the IP header. we assume that we do not have any
        // "OPTIONS" set - they would add to the tcp headersize. 
        //
        // all flavours of udp have 4 2-byte words of udp header after the IP
        // header. If we run udps (s=smart or sequencenumber) this means each
        // datagram contains a 64-bit sequencenumber immediately following the
        // udp header.
        if( protocol.find("tcp")!=std::string::npos )
            hdr += 6*4;
        else if( protocol.find("udp")!=std::string::npos ) {
            hdr += 4*2;
            if( protocol.find("udps")!=std::string::npos )
                hdr += sizeof(uint64_t);
        }
        // if MTU too small ... then some code will probably blow up
        // if hdr > mtu!!!
        return mtu - hdr;
    }

    inline unsigned int get_blocksize( void ) const {
        return blocksize;
    }
    // multiple-netport aware code _may_ use these calls in a different form
    // w/o breaking backwards compatibility
    inline unsigned short get_port( hpslist_type::size_type n = 0 ) const {
        return __m_hps.at(n).port;
    }
    inline std::string get_host( hpslist_type::size_type n = 0 ) const {
        return __m_hps.at(n).host;
    }

    // multiple-netport aware code can use this api too
    inline hpslist_type const& get_hps( void ) const {
        return __m_hps;
    }

    // Quick test if we have multiple netreaders
    inline unsigned int n_port( void ) const {
        return __m_hps.size();
    }
    // rotate the hps-entries such that elemenet 0 => back of the list
    void rotate( void );

    // return the number of non-empty suffixes
    inline unsigned int n_non_empty_suffixes( void ) const {
        return __m_n_non_empty_suffixes;
    }
    inline std::string const& stream2suffix( unsigned int sid ) const {
        suffixmap_type::const_iterator  sptr( __m_suffixmap.find(sid) );
        EZASSERT2( sptr!=__m_suffixmap.end(), std::runtime_error,
                   EZINFO("Failure trying to find net_port suffix for stream#" << sid) );
        return sptr->second;
    }

    private:
        // keep mtu and blocksize private.
        // this allows us to automagically
        // enforce the constraint-relation(s)
        // between the variables.
        //
        // Changing one(or more) may have an
        // effect on the others
        // [eg: changing the protocol
        //  changes the datagramsize, which
        //  affects the blocksize]
        // In order to compute the size of a datagram
        // the mtu is used:
        // it is assumed that only one datagram per MTU
        // is sent. Size starts off with MTU. Protocol
        // specific headersize is subtracted, then
        // internal protocol headersize is subtracted
        // and the remaining size is truncated to be
        // a multiple of 8.
        std::string          protocol;
        unsigned int         mtu;
        unsigned int         blocksize;
#if 0
        // this is now in __m_hps[0]
        unsigned short       port;
#endif
        // if we ever want to send datagrams larger than 1 MTU,
        // make this'un non-const and clobber it to liking
        // HV: 20 feb 2014 - "clang" compilert on OSX Mavericks sais
        //                   it's not used anywhere so let's comment
        //                   it out alltogether. If it must be used
        //                   this is the place to uncomment it.
#if 0
        mutable unsigned int nmtu;
#endif

        // We keep an internal vector of host-port-suffix items
        // Must assure it's at least one entry!
        hpslist_type         __m_hps;
        // Each time the hps list gets altered we pre-compute this
        // 1) keep track of port nr => suffix mapping
        // 2) count the non-empty entries
        unsigned int         __m_n_non_empty_suffixes;
        suffixmap_type       __m_suffixmap;
        void                 analyzeSuffixes( void );
};


// Helper functions to access the ipd in a variety of ways and units

// In jive5ab there's the magic value of ipd==-1, which means 
// "use auto value", i.e. use the theoretical ipd, based on the data rate
// and the MTU.
// Both of these methods implement this and return in us or ns:
//      np.ipd < 0 ? return np.theoretical_ipd : return np.ipd
int ipd_us( const netparms_type& np );
int ipd_ns( const netparms_type& np );

// Return the actual value that was set
int ipd_set_us( const netparms_type& np );
int ipd_set_ns( const netparms_type& np );

// Return the theoretical value that was set
int theoretical_ipd_us( const netparms_type& np );
int theoretical_ipd_ns( const netparms_type& np );

#endif
