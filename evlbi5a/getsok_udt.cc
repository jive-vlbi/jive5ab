// implementation of the functions
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
#include <getsok_udt.h>
#include <evlbidebug.h>
#include <dosyscall.h>
#include <libudt5ab/udt.h> // for UDT ... gah!

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

using namespace std;

// Open a connection to <host>:<port> via the protocol <proto>.
// Returns the filedescriptor for this open connection.
// It will be in blocking mode. If UDP, checksumming will be
// disabled (for our data it's not important and it saves quite
// some CPU cycles!). Note, if _this_ fails, only a warning will
// be displayed, this is merely an optimization and as such is not
// fatal if it fails. Also, as it may be an undocumented/not portable
// feature (setsockopt-option), I'll try to make it not fail under
// systems that don't have it.
// Throws if something fails.
int getsok_udt( const string& host, unsigned short port, const string& /*proto*/, const unsigned int mtu ) {
    int                s;
    const string       realproto( "tcp" );      // for getprotoent() - we want protocol number for TCP
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent*   pptr;
    struct sockaddr_in src, dst;

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(realproto.c_str())), SCINFO(" - proto: '" << realproto << "'") );
    DEBUG(4, "getsok_udt: got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=UDT::socket(PF_INET, SOCK_STREAM, pptr->p_proto) );

    DEBUG(4, "getsok_udt: got socket " << s << endl);

    // Before we connect, set properties of the UDT socket
    int           MTU   = (int)mtu;
    int           bufsz = 32*1024*1024;

    // check and set MTU
    ASSERT2_COND( MTU>0, SCINFO("The MTU " << mtu << " is > INT_MAX!"); UDT::close(s) );
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_MSS,  &MTU, sizeof(MTU)), UDT::close(s) );

    // turn off lingering - close the connection immediately
    // It should be noted that IF you want all data being put
    // into a UDT socket to arrive at the other side, you 
    // MUST linger. So the udtwriter() function in jive5ab re-enables
    // lingering after closing normally. This means that an abort 
    // is quick but a normal data transfer is allowed to finish properly
    struct linger l = {0, 0};
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_LINGER, &l, sizeof(struct linger)), UDT::close(s) );

    // This is client socket so we need to set the sendbufsize only
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_SNDBUF, &bufsz, sizeof(bufsz)), UDT::close(s) );

    // On a client socket we support congestion control
    CCCFactory<IPDBasedCC>  ccf;
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_CC, &ccf, sizeof(&ccf)), UDT::close(s) );

    // Bind to local
    src.sin_family      = AF_INET;
    src.sin_port        = 0;
    src.sin_addr.s_addr = INADDR_ANY;

    ASSERT2_ZERO( UDT::bind(s, (const struct sockaddr*)&src, slen), UDT::close(s) );

    // Fill in the destination adress
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons( port );

    ASSERT2_ZERO( ::resolve_host(host, SOCK_STREAM, pptr->p_proto, dst),
                  SCINFO("Failed to find IPv4 address for " << host); UDT::close(s) );


    // Seems superfluous to use "dst.sin_*" here but those are the actual values
    // that get fed to the systemcall...
    DEBUG(2, "getsok_udt: trying " << host << "{" << inet_ntoa(dst.sin_addr) << "}:"
             << ntohs(dst.sin_port) << " ... " << endl);
    // Attempt to connect
    ASSERT2_ZERO( UDT::connect(s, (const struct sockaddr*)&dst, slen), UDT::close(s) );
    DEBUG(4, "getsok_udt: connected to " << inet_ntoa(dst.sin_addr) << ":" << ntohs(dst.sin_port) << endl);

    return s;
}


// Get a socket for incoming connections.
// The returned filedescriptor is in blocking mode.
//
// You *must* specify the port/protocol. Optionally specify
// a local interface to bind to. If left empty (which is
// default) bind to all interfaces.
int getsok_udt(unsigned short port, const string& proto, const unsigned int mtu, const string& local) {
    int                s;
    int                slen( sizeof(struct sockaddr_in) );
    int                reuseaddr;
    const string       realproto( "tcp" );
    unsigned int       optlen( sizeof(reuseaddr) );
    struct protoent*   pptr;
    struct sockaddr_in src;

    DEBUG(2, "getsok_udt: req. server socket@"
             << (local.size()?("{"+local+"}"):(""))
             << ":" << port << endl);

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(realproto.c_str())), SCINFO(" - proto: '" << realproto << "'") );
    DEBUG(4, "getsok_udt: got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=UDT::socket(PF_INET, SOCK_STREAM, pptr->p_proto) );
    DEBUG(4, "getsok_udt: got socket " << s << endl);

    // Before we actually do the bind, set 'SO_REUSEADDR' to 1
    reuseaddr = 1;

    int           MTU   = (int)mtu;
    int           bufsz = 32*1024*1024;
    struct linger l = {0, 0};

    // check and set MTU
    ASSERT2_COND( MTU>0, SCINFO("The MTU " << mtu << " is > INT_MAX!"); UDT::close(s) );
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_MSS,  &MTU, sizeof(MTU)), UDT::close(s) );

    // turn off lingering - close the connection immediately
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_LINGER, &l, sizeof(struct linger)), UDT::close(s) );

    // We're a server socket so we set the receive buffer size
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_RCVBUF, &bufsz, sizeof(bufsz)), UDT::close(s) );

    // And finally indicate we want to reuse the address
    ASSERT2_ZERO( UDT::setsockopt(s, SOL_SOCKET, UDT_REUSEADDR, &reuseaddr, optlen), UDT::close(s) );

    // Bind to local
    src.sin_family      = AF_INET;
    src.sin_port        = htons( port );
    src.sin_addr.s_addr = INADDR_ANY;

    // if 'local' not empty, attempt to bind to that address
    // First try the simple conversion, otherwise we need to do
    // a lookup. Which is to say - if it isn't a multicast address.
	// In which case the multicast will be joined rather than a bind
	// to a local ip address
    if( local.size() ) {
        struct sockaddr_in ip;

        ASSERT2_COND( ::resolve_host(local, SOCK_DGRAM, pptr->p_proto, ip), 
                      SCINFO("Failed to resolve local IPv4 address for '" << local << "'"); UDT::close(s) );

        src.sin_addr = ip.sin_addr;
        DEBUG(1, "getsok_udt: binding to local address " << local << " " << inet_ntoa(src.sin_addr) << endl);
    }
	// whichever local address we have - we must bind to it
    ASSERT2_ZERO( UDT::bind(s, (const struct sockaddr*)&src, slen),
                  SCINFO(proto << ":" << port << " [" << local << "]"); UDT::close(s); );

    DEBUG(3, "getsok_udt: listening on interface " << local << endl);
    ASSERT2_ZERO( UDT::listen(s, 5), UDT::close(s) );

    return s;
}


// Perform an accept on the given filedescriptor
// and return a fdprops_type::value_type
// which is a pair<filedescriptor,remote-adress>
// with the 'filedescriptor' the newly accepted connection
// and the 'remote-adress' the ... stringified remote
// address.
//
// if anything wonky/fails an exception will be thrown
// so you do not have to check the returnvalue... if you
// get it, it's ok!
//
// Note: caller should make sure that the passed in socket
//       indeed has an incoming connection waiting!
fdprops_type::value_type do_accept_incoming_udt( int fd ) {
    int                afd;
    int                islen( sizeof(struct sockaddr_in) );
    ostringstream      strm;
    struct sockaddr_in remote;

    // somebody knockin' on the door!
    // Do the accept but do not let the system throw - it would
    // kill all other running stuff... 
    ASSERT2_COND( (afd=UDT::accept(fd, (struct sockaddr*)&remote, &islen))!=-1,
            SCINFO("UDTError:" << UDT::getlasterror().getErrorMessage())  );

    // Get the remote adress/port
    strm << inet_ntoa(remote.sin_addr) << ":" << ntohs(remote.sin_port);

    return make_pair(afd, strm.str());
}



// The congestion control class
IPDBasedCC::IPDBasedCC() :
    CUDTCC(), _ipd_in_ns( -1 )
{}

void IPDBasedCC::onACK(const int32_t& seqno) {
    // Let our base-class do it's thang
    this->CUDTCC::onACK(seqno);

    // And for an encore we do *our* stuff on top of it :D
    // that is, if an ipd was set
    if( _ipd_in_ns>0 )
        this->m_dPktSndPeriod = double(_ipd_in_ns) / 1000.0;
}

void IPDBasedCC::set_ipd(unsigned int ipd_in_ns) {
    _ipd_in_ns = ipd_in_ns;
}

IPDBasedCC::~IPDBasedCC()
{}


