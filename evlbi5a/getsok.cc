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
#include <getsok.h>
#include <evlbidebug.h>
#include <dosyscall.h>

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

//extern int h_errno;

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
int getsok( const string& host, unsigned short port, const string& proto ) {
    int                s;
    int                fmode;
    int                soktiep( SOCK_STREAM );
    string             realproto;
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent*   pptr;
    struct sockaddr_in src, dst;

    // proto may encode more than just tcp or udp.
    // we really need to know the underlying protocol so get it out
    if( proto.find("udp")!=string::npos )
        realproto = "udp";
    else if( proto.find("tcp")!=string::npos )
        realproto = "tcp";
    ASSERT2_COND( realproto.size()>0,
                  SCINFO("protocol '" << proto << "' is not based on UDP or TCP") );

    // If it's UDP, we change soktiep [type of the socket] from
    // SOCK_STREAM => SOCK_DGRAM. Otherwise leave it at SOCK_STREAM.
    if( realproto=="udp" )
        soktiep = SOCK_DGRAM;

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(realproto.c_str())), SCINFO(" - proto: '" << realproto << "'") );
    DEBUG(4, "got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=::socket(PF_INET, soktiep, pptr->p_proto) );
    DEBUG(4, "got socket " << s << endl);

    // Set in blocking mode
    fmode  = fcntl(s, F_GETFL);
    fmode &= ~O_NONBLOCK;
    // do not use "setfdblockingmode()" as we may have to execute cleanupcode
    // and "setfdblockingmode()" will just throw, giving us no opportunity to
    // clean up ...
    ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );

// only if the system we're compiling under seems to
// know about disabling checksumming ...
// Thanks go to Jan Wagner who supplied this patch: he found this during
// his high-volume datatransport protocol research (Tsunami).
#ifdef SO_NO_CHECK
    // If udp, disable checksumming
    if( realproto=="udp" ) {
        const int  sflag( 1 );

        if( ::setsockopt(s, SOL_SOCKET, SO_NO_CHECK, &sflag, sizeof(sflag))!=0 ) {
            DEBUG(-1, "Optimization warning: failed to disable UDP checksumming.\n");
        } else {
            DEBUG(-1, "Optimization: disabled UDP checksumming.\n");
        }
    }
#endif

    // Bind to local
    src.sin_family      = AF_INET;
    src.sin_port        = 0;
    src.sin_addr.s_addr = INADDR_ANY;
    ASSERT2_ZERO( ::bind(s, (const struct sockaddr*)&src, slen), ::close(s) );

    // Fill in the destination adress
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons( port );

    // First try the simple conversion, otherwise we need to do
    // a lookup
    if( inet_aton(host.c_str(), &dst.sin_addr)==0 ) {
        struct hostent*  hptr;

        DEBUG(4, "Attempt to lookup " << host << endl);
        ASSERT2_NZERO( (hptr=::gethostbyname(host.c_str())),
                       ::close(s); SCINFO(" - " << hstrerror(h_errno) << " '" << host << "'") );
        ::memcpy(&dst.sin_addr.s_addr, hptr->h_addr, sizeof(dst.sin_addr.s_addr));
        DEBUG(4, "Found it: " << hptr->h_name << " [" << inet_ntoa(dst.sin_addr) << "]" << endl);
    }

    // Seems superfluous to use "dst.sin_*" here but those are the actual values
    // that get fed to the systemcall...
    DEBUG(2, "getsok: trying " << host << "{" << inet_ntoa(dst.sin_addr) << "}:"
             << ntohs(dst.sin_port) << " ... " << endl);
    // Attempt to connect
    ASSERT2_ZERO( ::connect(s, (const struct sockaddr*)&dst, slen), ::close(s) );
    DEBUG(4, "getsok: connected to " << inet_ntoa(dst.sin_addr) << ":" << ntohs(dst.sin_port) << endl);

    return s;
}


// Get a socket for incoming connections.
// The returned filedescriptor is in blocking mode.
//
// You *must* specify the port/protocol. Optionally specify
// a local interface to bind to. If left empty (which is
// default) bind to all interfaces.
int getsok(unsigned short port, const string& proto, const string& local) {
    int                s;
    int                fmode;
    int                soktiep( SOCK_STREAM );
    int                reuseaddr;
    string             realproto;
    unsigned int       optlen( sizeof(reuseaddr) );
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent*   pptr;
    struct sockaddr_in src;

    DEBUG(2, "getsok: req. server socket@" << proto
             << (local.size()?("{"+local+"}"):(""))
             << ":" << port << endl);

    // proto may encode more than just tcp or udp.
    // we really need to know the underlying protocol so get it out
    if( proto.find("udp")!=string::npos )
        realproto = "udp";
    else if( proto.find("tcp")!=string::npos )
        realproto = "tcp";
    ASSERT2_COND( realproto.size()>0,
                  SCINFO("protocol '" << proto << "' is not based on UDP or TCP") );

    // If it's UDP, we change soktiep [type of the socket] from
    // SOCK_STREAM => SOCK_DGRAM. Otherwise leave it at SOCK_STREAM.
    if( realproto=="udp" )
        soktiep = SOCK_DGRAM;

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(realproto.c_str())), SCINFO(" - proto: '" << realproto << "'") );
    DEBUG(4, "getsok: got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=::socket(PF_INET, soktiep, pptr->p_proto) );
    DEBUG(4, "getsok: got socket " << s << endl);

    // Set in blocking mode
    fmode = fcntl(s, F_GETFL);
    fmode &= ~O_NONBLOCK;
    ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );

    // Before we actually do the bind, set 'SO_REUSEADDR' to 1
    reuseaddr = 1;
    ASSERT2_ZERO( ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, optlen),
                  ::close(s) );

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
		struct in_addr   ip;

        // first resolve <local>
        if( inet_aton(local.c_str(), &ip)==0 ) {
            struct hostent*  hptr;

            DEBUG(2, "getsok: attempt to lookup " << local << endl);
            ASSERT2_NZERO( (hptr=::gethostbyname(local.c_str())),
                           ::close(s); SCINFO(hstrerror(h_errno) << " '" << local << "'") );
            memcpy(&ip.s_addr, hptr->h_addr, sizeof(ip.s_addr));
            DEBUG(2, "getsok: '" << hptr->h_name << "' = " << inet_ntoa(ip) << endl);
        }
        // Good. <ip> now contains the ipaddress specified in <local>
		// If multicast detected, join the group and throw up if it fails. 
		if( IN_MULTICAST(ntohl(ip.s_addr)) ) {
			// ok do the MC join
            unsigned char   newttl( 30 );
			struct ip_mreq  mcjoin;

			DEBUG(1, "getsok: joining multicast group " << local << endl);

			// By the looks of the docs we do not have to do a lot more than a group-join.
			// The other options are irrelevant for us.
			// (*) We're interested in MC traffik on any interface.
			mcjoin.imr_multiaddr        = ip;
			mcjoin.imr_interface.s_addr = INADDR_ANY; // (*)
			ASSERT_ZERO( ::setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
						              &mcjoin, sizeof(mcjoin)) );

            // okay, we did connex0r to a multicast addr.
            // Possibly, failing to set the ttl is not fatal
            // but we _do_ warn the user that their data
            // may not actually arrive!
            if( ::setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL,
                        &newttl, sizeof(newttl))!=0 ) {
                DEBUG(-1, "getsok: WARN Failed to set MulticastTTL to "
                        << newttl << endl);
                DEBUG(-1, "getsok: WARN Your data may or may not arrive, " 
                        << "depending on LAN or WAN" << endl);
            }
		} else {
            src.sin_addr = ip;
			DEBUG(1, "getsok: binding to local address " << local << " " << inet_ntoa(src.sin_addr) << endl);
		}
    }
	// whichever local address we have - we must bind to it
	ASSERT2_ZERO( ::bind(s, (const struct sockaddr*)&src, slen), ::close(s) );

    // Ok. It's bound.
    // Now do the listen()
    if( realproto=="tcp" ) {
        DEBUG(3, "getsok: listening on interface " << local << endl);
        ASSERT2_ZERO( ::listen(s, 5), ::close(s) );
    }

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
fdprops_type::value_type do_accept_incoming( int fd ) {
    int                afd;
    int                fmode;
    unsigned int       slen( sizeof(struct sockaddr_in) );
    ostringstream      strm;
    struct sockaddr_in remote;
    // somebody knockin' on the door!
    // Do the accept but do not let the system throw - it would
    // kill all other running stuff... 
    ASSERT_COND( (afd=::accept(fd, (struct sockaddr*)&remote, &slen))!=-1 );

    // Put the socket in blocking mode
    fmode  = ::fcntl(afd, F_GETFL);
    fmode &= ~O_NONBLOCK;
    ASSERT_ZERO( ::fcntl(afd, F_SETFL, fmode) );

    // Get the remote adress/port
    strm << inet_ntoa(remote.sin_addr) << ":" << ntohs(remote.sin_port);

    return make_pair(afd, strm.str());
}

// ...
void setfdblockingmode(int fd, bool blocking) {
    int  fmode;

    ASSERT2_COND(fd>=0, SCINFO("fd=" << fd));
 
    fmode = ::fcntl(fd, F_GETFL);
    fmode = (blocking?(fmode&(~O_NONBLOCK)):(fmode|O_NONBLOCK));
    ASSERT2_ZERO( ::fcntl(fd, F_SETFL, fmode),
                  SCINFO("fd=" << fd << ", blocking=" << blocking); );
    fmode = ::fcntl(fd, F_GETFL);
    if( (blocking && ((fmode&O_NONBLOCK)==O_NONBLOCK)) ||
        (!blocking && ((fmode&O_NONBLOCK)==0)) )
        ASSERT2_NZERO(0, SCINFO(" Failed to set blocking=" << blocking << " on fd#" << fd));
    return;
}

