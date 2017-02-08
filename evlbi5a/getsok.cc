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
#include <ezexcept.h>
#include <dosyscall.h>
#include <threadutil.h>

#include <stdexcept>

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>

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
    int                s, pent_rv;
    int                fmode;
    int                soktiep( SOCK_STREAM );
    char               pbuf[1024];
    string             realproto;
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent    pent;
    struct protoent*   pptr = 0;
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
    EZASSERT2_ZERO(pent_rv = ::getprotobyname_r(realproto.c_str(), &pent, pbuf, sizeof(pbuf), &pptr), std::runtime_error,
                   EZINFO(" (proto: '" << realproto << "') - " << evlbi5a::strerror(pent_rv)));
    EZASSERT2(pptr, std::runtime_error,
              EZINFO("::getprotobyname_r yielded NULL pointer (proto: '" << realproto << "') - " << evlbi5a::strerror(pent_rv)));
    DEBUG(4, "got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=::socket(PF_INET, soktiep, pptr->p_proto) );
    DEBUG(4, "got socket " << s << endl);

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

    ASSERT2_ZERO( ::resolve_host(host, soktiep, pptr->p_proto, dst),
                  SCINFO("No IPv4 address found for " << host); ::close(s) );

    // Seems superfluous to use "dst.sin_*" here but those are the actual values
    // that get fed to the systemcall...
    DEBUG(2, "getsok: trying " << host << "{" << inet_ntoa(dst.sin_addr) << "}:"
             << ntohs(dst.sin_port) << " ... " << endl);

    // Start with fd in blocking mode
    fmode  = fcntl(s, F_GETFL);
    fmode &= ~O_NONBLOCK;
    // do not use "setfdblockingmode()" as we may have to execute cleanupcode
    // and "setfdblockingmode()" will just throw, giving us no opportunity to
    // clean up ...
    ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );

    // If we're doing a TCP connect to an invalid/non-responsive IPv4
    // address the time out is extremely long. jive5ab remains unresponsive
    // and cannot even be "^C"-ed.
    // Let's do the connect() in non-blocking mode
    if( realproto=="tcp" ) {
        int            r;
        int            connecterrno;
        int            connecterrnolen = sizeof(connecterrno);
        struct pollfd  pfd;

        // force NON-blocking mode
        fmode  = fcntl(s, F_GETFL);
        fmode |= O_NONBLOCK;
        // do not use "setfdblockingmode()" as we may have to execute cleanupcode
        // and "setfdblockingmode()" will just throw, giving us no opportunity to
        // clean up ...
        ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );

        // Issue connect. On a non-blocking socket it may return -1 with
        // errno==EINPROGRESS. Anything else is a direct error
        r = ::connect(s, (const struct sockaddr*)&dst, slen);
        
        ASSERT2_COND(r==0 || (r==-1 && errno==EINPROGRESS), ::close(s); SCINFO(" TCP connect fails") );

        // If r==-1 => the connect hadn't finished yet so we wait for ~3
        // seconds for the socket to become writable (which marks the fact
        // that the socket is connected).
        pfd.fd     = s;
        pfd.events = POLLOUT;

        // poll(2) must return a positive number [0, ...>
        ASSERT2_POS( r = ::poll(&pfd, 1, 3000), SCINFO(" poll() error - "); ::close(s));

        // If r==0, we timed out
        ASSERT2_COND(r==1, SCINFO(" connect timed out"); ::close(s));

        // Retrieve the error code
        ASSERT2_ZERO( ::getsockopt(s, SOL_SOCKET, SO_ERROR, &connecterrno, (socklen_t*)&connecterrnolen), ::close(s) );

        // If the error code wasn't zero, something must've gone wrong!
        errno = connecterrno;
        ASSERT2_ZERO( connecterrno, SCINFO(" Failed to connect - "); ::close(s));

        // One final assertion - the socket MUST be marked as writable
        ASSERT2_COND(pfd.revents & POLLOUT, SCINFO(" Socket not marked as writable?!!"); ::close(s));

        // put back to blocking mode
        fmode  = fcntl(s, F_GETFL);
        fmode &= ~O_NONBLOCK;
        // do not use "setfdblockingmode()" as we may have to execute cleanupcode
        // and "setfdblockingmode()" will just throw, giving us no opportunity to
        // clean up ...
        ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );
    } else {
        // Attempt to connect
        ASSERT2_ZERO( ::connect(s, (const struct sockaddr*)&dst, slen), ::close(s) );
    }

    DEBUG(4, "getsok: connected to " << inet_ntoa(dst.sin_addr) << ":" << ntohs(dst.sin_port) << endl);

    return s;
}



int getsok_unix(const string& path, bool do_connect) {
    int                s;
    int                fmode;
    int                reuseaddr;
    unsigned int       slen( sizeof(struct sockaddr_un) );
    unsigned int       optlen( sizeof(reuseaddr) );
    struct sockaddr_un dst;

    // attempt to create a socket
    ASSERT_POS( s=::socket(AF_UNIX, SOCK_STREAM, 0) );
    DEBUG(4, "getsok_unix: got socket " << s << endl);

    // Set in blocking mode
    fmode  = fcntl(s, F_GETFL);
    fmode &= ~O_NONBLOCK;
    // do not use "setfdblockingmode()" as we may have to execute cleanupcode
    // and "setfdblockingmode()" will just throw, giving us no opportunity to
    // clean up ...
    ASSERT2_ZERO( ::fcntl(s, F_SETFL, fmode), ::close(s) );

    reuseaddr = 1;
    ASSERT2_ZERO( ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, optlen),
                  ::close(s) );

    // Connect or bind to dest
    dst.sun_family      = AF_UNIX;
    ::strncpy(dst.sun_path, path.c_str(), sizeof(dst.sun_path));

    if( do_connect ) {
        // Attempt to connect
        ASSERT2_ZERO( ::connect(s, (const struct sockaddr*)&dst, slen), ::close(s) );
        DEBUG(4, "getsok_unix: connected to " << path << endl);
    } else {
        // Attempt to bind
        ASSERT2_ZERO( ::bind(s, (const struct sockaddr*)&dst, slen), ::close(s) );
        DEBUG(4, "getsok_unix: bound to " << path << endl);
        ASSERT2_ZERO( ::listen(s, 5), ::close(s) );
        DEBUG(3, "getsok_unix: listening on interface " << path << endl);
    }
    return s;
}

int getsok_unix_client(const string& path) {
    return getsok_unix(path, true);
}
int getsok_unix_server(const string& path) {
    return getsok_unix(path, false);
}



// Get a socket for incoming connections.
// The returned filedescriptor is in blocking mode.
//
// You *must* specify the port/protocol. Optionally specify
// a local interface to bind to. If left empty (which is
// default) bind to all interfaces.
int getsok(unsigned short port, const string& proto, const string& local) {
    int                s, pent_rv;
    int                fmode;
    int                soktiep( SOCK_STREAM );
    int                reuseaddr;
    char               pbuf[1024];
    string             realproto;
    unsigned int       optlen( sizeof(reuseaddr) );
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent    pent;
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
    EZASSERT2_ZERO(pent_rv = ::getprotobyname_r(realproto.c_str(), &pent, pbuf, sizeof(pbuf), &pptr), std::runtime_error,
                   EZINFO(" (proto: '" << realproto << "') - " << evlbi5a::strerror(pent_rv)));
    EZASSERT2(pptr, std::runtime_error,
              EZINFO("::getprotobyname_r yielded NULL pointer (proto: '" << realproto << "') - " << evlbi5a::strerror(pent_rv)));
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
        if( inet_pton(AF_INET, local.c_str(), &ip)==-1 ) {
            int                gai_error;
            struct addrinfo    hints;
            struct addrinfo*   resultptr = 0, *rp;

            // Provide some hints
            ::memset(&hints, 0, sizeof(struct addrinfo));
            hints.ai_family   = AF_INET;       // IPv4 only at the moment
            hints.ai_socktype = soktiep;       // only the socket type we require
            hints.ai_protocol = pptr->p_proto; // Id. for the protocol

            ASSERT2_ZERO( (gai_error=::getaddrinfo(local.c_str(), 0, &hints, &resultptr)),
                    SCINFO("[" << local << "] " << ::gai_strerror(gai_error)); ::freeaddrinfo(resultptr) );

            // Scan the results for an IPv4 address
            ip.s_addr = INADDR_NONE;
            for(rp=resultptr; rp!=0 && ip.s_addr==INADDR_NONE; rp=rp->ai_next) {
                if( rp->ai_family==AF_INET )
                    ip = ((struct sockaddr_in const*)rp->ai_addr)->sin_addr;
            }
            // don't need the list of results anymore
            ::freeaddrinfo(resultptr);
            // If we din't find one, give up
            ASSERT2_COND( ip.s_addr!=INADDR_NONE,
                    SCINFO(" - No IPv4 address found for " << local) );
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
	ASSERT2_ZERO( ::bind(s, (const struct sockaddr*)&src, slen),
                  SCINFO(proto << ":" << port << " [" << local << "]"); ::close(s); );

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

fdprops_type::value_type do_accept_incoming_ux( int fd ) {
    int                afd;
    int                fmode;
    unsigned int       slen( sizeof(struct sockaddr_un) );
    ostringstream      strm;
    struct sockaddr_un remote;
    // somebody knockin' on the door!
    // Do the accept but do not let the system throw - it would
    // kill all other running stuff... 
    ASSERT_COND( (afd=::accept(fd, (struct sockaddr*)&remote, &slen))!=-1 );

    // Put the socket in blocking mode
    fmode  = ::fcntl(afd, F_GETFL);
    fmode &= ~O_NONBLOCK;
    ASSERT_ZERO( ::fcntl(afd, F_SETFL, fmode) );

    // Get the remote adress/port
    strm << remote.sun_path << ends;

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


//
//  Resolve a hostname in dotted quad notation or canonical name format
//  to an IPv4 address. Returns 0 on success after filling in the
//  "dst.sin_addr" member
//
int resolve_host(const string& host, const int socktype, const int protocol, struct sockaddr_in& dst) {
    // First try the simple conversion, otherwise we need to do
    // a lookup
    // inet_pton is POSIX and returns -1 if the string is
    // NOT in dotted-decimal format. Then we fall back to getaddrinfo(3)
    if( inet_pton(AF_INET, host.c_str(), &dst.sin_addr)!=1 ) {
        int                gai_error;
        struct addrinfo    hints;
        struct addrinfo*   resultptr = 0, *rp;

        // Provide some hints to the address resolver about
        // what it is what we're looking for
        ::memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family   = AF_INET;     // IPv4 only at the moment
        hints.ai_socktype = socktype;    // only the socket type we require
        hints.ai_protocol = protocol;    // Id. for the protocol

        if( (gai_error=::getaddrinfo(host.c_str(), 0, &hints, &resultptr))!=0 ) {
            DEBUG(-1, "resolve_host[" << host << "] " << ::gai_strerror(gai_error) << endl);
            ::freeaddrinfo(resultptr);
            return -1;
        }

        // Scan the results for an IPv4 address
        dst.sin_addr.s_addr = INADDR_NONE;
        for(rp=resultptr; rp!=0 && dst.sin_addr.s_addr==INADDR_NONE; rp=rp->ai_next)
            if( rp->ai_family==AF_INET )
                dst.sin_addr = ((struct sockaddr_in const*)rp->ai_addr)->sin_addr;

        // don't need the list of results anymore
        ::freeaddrinfo(resultptr);
    }
    return (dst.sin_addr.s_addr==INADDR_NONE)?-1:0;
}
