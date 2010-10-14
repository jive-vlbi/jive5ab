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
    int                interpacketdelay;
    int                theoretical_ipd;
    unsigned int       nblock;

    // p.empty()==true => reset to default (defProtocol)
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
    //   udps     - the s stands for "smart" or "sequencenumber". it implies
    //              that, immediately following the UDP/IPv4 header there is
    //              a strict monotonically incrementing 64bit sequencenumber
    //              such that each next packet sent has a sequencenumber
    //              which is exactly 1 higher than the sequencenumber in the
    //              previous packet. this allows the receiver to put the
    //              packets back in the order they were sent AND detect lost
    //              packets.
    //
    void set_protocol( const std::string& p="" );
    // m==0 => reset to default (defMTU)
    void set_mtu( unsigned int m=0 );
    // bs==0 => reset to default (defBlockSize)
    void set_blocksize( unsigned int bs=0 );
    // portnr==0 => reset to default (defPort)
    void set_port( unsigned short portnr=0 );

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
        std::string        protocol;
        unsigned int       mtu;
        unsigned int       blocksize;
        unsigned short     port;

        // if we ever want to send datagrams larger than 1 MTU,
        // make this'un non-const and clobber it to liking
        const unsigned int nmtu;
};

#endif
