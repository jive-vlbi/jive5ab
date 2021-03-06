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
#include <trackmask.h>

// Collect together the network related parameters
// typically, net_protocol modifies these
extern const std::string  defProtocol;// = std::string("tcp");
extern const std::string  defUDPHelper;// = std::string("smart");

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

    // comes up with 'sensible' defaults
    netparms_type();

    // netprotocol values
    int                rcvbufsize;
    int                sndbufsize;
    std::string        host;
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
    // portnr==0 => reset to default (defPort)
    void set_port( unsigned short portnr=0 );
    // ack==0 => reset to default (defACK)
    void set_ack( int ack=0 );

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
    inline unsigned short get_port( void ) const {
        return port;
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
        unsigned short       port;

        // if we ever want to send datagrams larger than 1 MTU,
        // make this'un non-const and clobber it to liking
        // HV: 20 feb 2014 - "clang" compilert on OSX Mavericks sais
        //                   it's not used anywhere so let's comment
        //                   it out alltogether. If it must be used
        //                   this is the place to uncomment it.
#if 0
        mutable unsigned int nmtu;
#endif
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
