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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

extern int h_errno;

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
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent*   pptr;
    struct sockaddr_in src, dst;

    // If it's UDP, we change soktiep [type of the socket] from
    // SOCK_STREAM => SOCK_DGRAM. Otherwise leave it at SOCK_STREAM.
    if( proto=="udp" )
        soktiep = SOCK_DGRAM;

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(proto.c_str())), SCINFO(" - proto: '"<<proto << "'") );
    DEBUG(3, "Got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=::socket(PF_INET, soktiep, pptr->p_proto) );
    DEBUG(2, "Got socket " << s << endl);

    // Set in NONblocking mode
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
    if( proto=="udp" ) {
        const int  sflag( 1 );

        if( ::setsockopt(s, SOL_SOCKET, SO_NO_CHECK, &sflag, sizeof(sflag))!=0 ) {
            DEBUG(-1, "Optimization warning: failed to disable UDP checksumming.");
        } else {
            DEBUG(-1, "Optimization: Disabled UDP checksumming.");
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

        DEBUG(2, "Attempt to lookup " << host << endl);
        ASSERT2_NZERO( (hptr=::gethostbyname(host.c_str())),
                       ::close(s); SCINFO(" - " << hstrerror(h_errno) << " '" << host << "'") );
        memcpy(&dst.sin_addr.s_addr, hptr->h_addr, sizeof(dst.sin_addr.s_addr));
        DEBUG(2, "Found it: " << hptr->h_name << " [" << inet_ntoa(dst.sin_addr) << "]" << endl);
    }

    // Seems superfluous to use "dst.sin_*" here but those are the actual values
    // that get fed to the systemcall...
    DEBUG(2, "Trying " << host << "{" << inet_ntoa(dst.sin_addr) << "}:" << ntohs(dst.sin_port) << " ... " << endl);
    // Attempt to connect
    ASSERT2_ZERO( ::connect(s, (const struct sockaddr*)&dst, slen), ::close(s) );
    DEBUG(2, "Connected to " << inet_ntoa(dst.sin_addr) << ":" << ntohs(dst.sin_port) << endl);

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
    unsigned int       optlen( sizeof(reuseaddr) );
    unsigned int       slen( sizeof(struct sockaddr_in) );
    struct protoent*   pptr;
    struct sockaddr_in src;

    DEBUG(3, "getsok(port=" << port << ", proto=" << proto
             << ", local=" << local << ")" << endl);
    // If it's UDP, we change soktiep [type of the socket] from
    // SOCK_STREAM => SOCK_DGRAM. Otherwise leave it at SOCK_STREAM.
    if( proto=="udp" )
        soktiep = SOCK_DGRAM;

    // Get the protocolnumber for the requested protocol
    ASSERT2_NZERO( (pptr=::getprotobyname(proto.c_str())), SCINFO(" - proto: '" << proto << "'") );
    DEBUG(4, "Got protocolnumber " << pptr->p_proto << " for " << pptr->p_name << endl);

    // attempt to create a socket
    ASSERT_POS( s=::socket(PF_INET, soktiep, pptr->p_proto) );
    DEBUG(3, "Got socket " << s << endl);

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
    // a lookup
    if( local.size() ) {
        if( inet_aton(local.c_str(), &src.sin_addr)==0 ) {
            struct hostent*  hptr;

            DEBUG(2, "Attempt to lookup " << local << endl);
            ASSERT2_NZERO( (hptr=::gethostbyname(local.c_str())),
                           ::close(s); lclSvar_0a << " - " << hstrerror(h_errno) << " '" << local << "'"; );
            memcpy(&src.sin_addr.s_addr, hptr->h_addr, sizeof(src.sin_addr.s_addr));
            DEBUG(2, "Found it: " << hptr->h_name << " [" << inet_ntoa(src.sin_addr) << "]" << endl);
        }
    }

    ASSERT2_ZERO( ::bind(s, (const struct sockaddr*)&src, slen), ::close(s) );

    // Ok. It's bound.
    // Now do the listen()
    if( proto=="tcp" ) {
        DEBUG(3, "Start to listen on interface " << local << endl);
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
    ASSERT_POS( (afd=::accept(fd, (struct sockaddr*)&remote, &slen)) );

    // Put the socket in blockig mode
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

    ASSERT_POS(fd);
 
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

