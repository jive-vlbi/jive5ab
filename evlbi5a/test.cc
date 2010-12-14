// main program file
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
// c++ headers
#include <iostream>
#include <string>
#include <sstream>
#include <exception>
#include <map>
#include <vector>
#include <algorithm>

// our own stuff
#include <dosyscall.h>
#include <ioboard.h>
#include <xlrdevice.h>
#include <evlbidebug.h>
#include <playpointer.h>
#include <transfermode.h>
#include <runtime.h>
#include <hex.h>
#include <streamutil.h>
#include <stringutil.h>
#include <mk5command.h>
#include <getsok.h>
#include <rotzooi.h>

// system headers (for sockets and, basically, everything else :))
#include <sys/poll.h>
#include <sys/timeb.h>
#include <sys/time.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>


using namespace std;
extern int       h_errno;


// For displaying the events set in the ".revents" field
// of a pollfd struct after a poll(2) in human readable form.
// Usage:
// struct pollfd  pfd;
//   <... do polling etc ...>
//
// cout << "Events: " << eventor(pfd.revents) << endl;
struct eventor {
    eventor( short e ):
        events( e )
    {}
    short events;
};
ostream& operator<<( ostream& os, const eventor& ev ) {
    short e( ev.events );

    if( e&POLLIN )
        os << "IN,";
    if( e&POLLERR )
        os << "ERR, ";
    if( e&POLLHUP )
        os << "HUP, ";
    if( e&POLLPRI )
        os << "PRI, ";
    if( e&POLLOUT )
        os << "OUT, ";
    if( e&POLLNVAL )
        os << "NVAL!, ";
    return os;
}


// block all signals and wait for SIGINT (^C)
// argptr is supposed to point at an integer,
// taken to be the filedescriptor to write to
// in case of zignal [or error!]
// Precondition: this thread should be started
// with all signals blocked. This threadfn does
// NOT alter the signalmask, it will only wait
// on spezifik zignalz!
void* signalthread_fn( void* argptr ) {
    // zignalz to wait for
    const int          sigz[] = {SIGINT, SIGTERM, SIGSEGV};
    const unsigned int nsigz  = sizeof(sigz)/sizeof(sigz[0]);
    // variables
    int      sig;
    int      rv;
    int*     fdptr = (int*)argptr;
    sigset_t waitset;

    DEBUG(3, "signalthread_fn starting, it is " << ::pthread_self() << endl);

    // check if we got an argument
    if( !fdptr ) {
        cerr << "signalthread_fn: Called with nullpointer!" << endl;
        return (void*)-1;
    }

    // make sure we are cancellable and it is 'deferred'
    if( (rv=::pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0))!=0 ) {
        cerr << "signalthread_fn: Failed to set canceltype to deferred - "
             << ::strerror(rv) << endl;
        return (void*)-1;
    }
    if( (rv=::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0))!=0 ) {
        cerr << "signalthread_fn: Failed to set cancelstate to cancellable - "
             << ::strerror(rv) << endl;
        return (void*)-1;
    }

    // goodie. Set up the "waitset", the set of
    // signals to wait for.
    if( (rv=sigemptyset(&waitset))!=0 ) {
        cerr << "signalthread_fn: Failed to sigemptyset() - " << ::strerror(errno) << endl;
        ::write(*fdptr, &rv, sizeof(rv));
        return (void*)rv;
    }
    // add the signals
    for(unsigned int i=0; i<nsigz; ++i) {
        if( (rv=sigaddset(&waitset, sigz[i]))!=0 ) {
            cerr << "signalthread_fn: Failed to sigaddset(" << sigz[i] << ") - "
                 << ::strerror(errno) << endl;
            ::write(*fdptr, &rv, sizeof(rv));
            return (void*)rv;
        }
    }

    // k3wl. Now wait for ever
    DEBUG(2,"signalthread entering sigwait " << ::pthread_self() << endl);
#ifdef GDBDEBUG
	while( true ) {
    	rv = ::sigwait(&waitset, &sig);
		if( rv==-1 && sig==0 )
			break;
	}
#else
	rv = ::sigwait(&waitset, &sig);
#endif
    DEBUG(2,"signalthread woke up: 'rv=sigwait(&sig)' => rv=" << rv << ", sig=" << sig << endl);

    if( rv!=0 )
        // not a zignal but an error!
        DEBUG(-1, "signalthread_fn: Failed to sigwait() - " << ::strerror(rv) << endl);

    if( sig>0 )
        DEBUG(3,"signalthread_fn: Got signal " << sig << endl);
    ::write(*fdptr, &sig, sizeof(sig));
    return (void*)0;
}


void Usage( const char* name ) {
    cout << "Usage: " << name << " [-h] [-m <messagelevel>] [-c <cardnumber>]" << endl
         << "   defaults are: messagelevel " << dbglev_fn() << " and card #1" << endl;
    return;
}


#define KEES(a,b) \
    case b: a << #b; break;

ostream& operator<<( ostream& os, const S_BANKSTATUS& bs ) {
    os << "BANK[" << bs.Label << "]: ";

    // bank status
    switch( bs.State ) {
        KEES(os, STATE_READY);
        KEES(os, STATE_NOT_READY);
        KEES(os, STATE_TRANSITION);
        default:
            os << "<Invalid state #" << bs.State << ">";
            break;
    }
    os << ", " << ((bs.Selected)?(""):("not ")) << "selected";
    os << ", " << ((bs.WriteProtected)?("ReadOnly "):("R/W "));
    os << ", ";
    // Deal with mediastatus
    switch( bs.MediaStatus ) {
        KEES(os, MEDIASTATUS_EMPTY);
        KEES(os, MEDIASTATUS_NOT_EMPTY);
        KEES(os, MEDIASTATUS_FULL);
        KEES(os, MEDIASTATUS_FAULTED);
        default:
            os << "<Invalid media status #" << bs.MediaStatus << ">";
    }
    return os;
}

// main!
int main(int argc, char** argv) {
    char           option;
    UINT           devnum( 1 );
    sigset_t       newset;
    pthread_t*     signalthread = 0;
    unsigned int   numcards;
    unsigned short cmdport = 2620;

    try {
        cout << "jive5ab Copyright (C) 2007-2010 Harro Verkouter" << endl;
        cout << "This program comes with ABSOLUTELY NO WARRANTY." << endl;
        cout << "This is free software, and you are welcome to " << endl
             << "redistribute it under certain conditions." << endl
             << "Check gpl-3.0.txt." << endl << endl;
        // Before we try to initialize hardware or anything
        // [the c'tor of 'environment' does go look for hardware]
        // we parse the commandline.
        // Check commandline
        long int       v;
        const long int maxport = 0x7fff;

        while( (option=::getopt(argc, argv, "hm:c:p:"))>=0 ) {
            switch( option ) {
                case 'h':
                    Usage( argv[0] );
                    return -1;
                case 'm':
                    v = ::strtol(optarg, 0, 0);
                    // check if it's too big for int
                    if( v<INT_MIN || v>INT_MAX ) {
                        cerr << "Value for messagelevel out-of-range.\n"
                            << "Useful range is: [" << INT_MIN << ", "
                            << INT_MAX << "]" << endl;
                        return -1;
                    }
                    dbglev_fn((int)v);
                    break;
                case 'c': 
                    v = ::strtol(optarg, 0, 0);
                    // check if it's out-of-range for UINT
                    if( v<-1 || v>INT_MAX ) {
                        cerr << "Value for devicenumber out-of-range.\n"
                            << "Useful range is: [-1, " << INT_MAX << "]" << endl;
                        return -1;
                    }
                    devnum = ((UINT)v);
                    break;
                case 'p':
                    v = ::strtol(optarg, 0, 0);
                    // check if it's out-of-range for portrange
                    if( v<0 || v>maxport ) {
                        cerr << "Value for port is out-of-range.\n"
                            << "Usefull range is: [0, " << maxport << "]" << endl;
                        return -1;
                    }
                    cmdport = ((unsigned short)v);
                    break;
                default:
                   cerr << "Unknown option '" << option << "'" << endl;
                   return -1;
            }
        }

        // Good. Now we've done that, let's go down to business!
        int                rotsok;
        int                listensok;
        int                signalpipe[2];
        runtime            environment;
        fdprops_type       acceptedfds;
        pthread_attr_t     tattr;
        // mk5cmds will be filled with appropriate, H/W specific functions lat0r
        mk5commandmap_type mk5cmds = mk5commandmap_type();

        // The runtime environment has already been created so it has
        // already checked the hardware and memorymapped the registers into
        // our addressspace. We have no further need for our many escalated
        // privilegesesess'
        ASSERT_ZERO( ::setreuid(::getuid(), ::getuid()) );

        // Block all zignalz. Not interested in the old mask as we
        // won't be resetting the sigmask anyway
        // Do this before we do anything else such that ANY thread that's
        // created (eg by "libssapi" (!)) has all signals blocked. This
        // guarantees that only our thread "signalthread" will catsj teh zignalz!
        ASSERT_ZERO( sigfillset(&newset) );
        PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &newset, 0) );

        // Start looking for streamstor cards
        ASSERT_COND( ((numcards=::XLRDeviceFind())>0) );
        cout << "Found " << numcards << " StreamStorCard" << ((numcards!=1)?("s"):("")) << endl;

        // Show user what we found. If we cannot open stuff,
        // we don't even try to create threads 'n all
        if( devnum<=numcards )
            environment.xlrdev = xlrdevice( devnum );
        cout << environment.xlrdev << endl;

        // create interthread pipe for communication between this thread
        // and the signalthread. If this fails there no use in going on, eh?
        ASSERT_ZERO( ::pipe(signalpipe) );

        // make sure we create a joinable thread
        PTHREAD_CALL( ::pthread_attr_init(&tattr) );
        PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE) );

        // ignore SIGPIPE 
        ASSERT_COND( signal(SIGPIPE, SIG_IGN)!=SIG_ERR );
        DEBUG(3, "main() is " << ::pthread_self() << endl);

        // Now is a good time to try to start the zignal thread.
        // Give it as argument a pointer to the write end of the pipe.
        signalthread = new pthread_t;
        PTHREAD2_CALL( ::pthread_create(signalthread, &tattr, signalthread_fn, (void*)&signalpipe[1]),
                       delete signalthread; signalthread = 0; );

        PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );

        // Depending on which hardware we found, we get the appropriate
        // commandmap
        ioboard_type::iobflags_type  hwflags = environment.ioboard.hardware();
        if( hwflags&ioboard_type::mk5a_flag )
            mk5cmds = make_mk5a_commandmap();
        else if( hwflags&ioboard_type::dim_flag )
            mk5cmds = make_dim_commandmap();
        else if( hwflags&ioboard_type::dom_flag )
            mk5cmds = make_dom_commandmap();
        else
            mk5cmds = make_generic_commandmap();

        // Goodie! Now set up for accepting incoming command-connections!
        // getsok() will throw if no socket can be created
        listensok = getsok( cmdport, "tcp" );

        // and get a socket on which to lissin for ROT broadcasts
        //rotsok    = getsok("0.0.0.0", 7010, "udp");
        rotsok    = getsok( 7010, "udp" );

        // Wee! 
        DEBUG(2, "Start main loop, waiting for incoming connections" << endl);

        while( true ) {
            // We poll all fd's in one place; here.
            // There's a number of 'special' sockets:
            //    * 'listening' => this is where commandclients
            //       connect to [typically port 2620/tcp]
            //    * 'signal' => the signal thread will write
            //      on this fd when one of the unmasked signals
            //      is raised
            //    * 'rot' => listens for ROT-clock broadcasts.
            //      mainly used at correlator(s). Maps 'task_id'
            //      to rot-to-systemtime mapping
            //    * 'commandfds' => accepted commandclients send
            //      commands over these fd's and we reply to them
            //      over the same fd. 
            //
            // 'cmdsockoffs' is the offset into the "struct pollfd fds[]"
            // variable at which the oridnary command connections start.
            //
            const unsigned int           listenidx   = 0;
            const unsigned int           signalidx   = 1;
            const unsigned int           rotidx      = 2;
            const unsigned int           cmdsockoffs = 3;
            const unsigned int           nrfds       = 3 + acceptedfds.size();
            // non-const stuff
            short                        events;
            unsigned int                 idx;
            struct pollfd                fds[ nrfds ];
            fdprops_type::const_iterator curfd;

            // Position 'listenidx' is always used for the listeningsocket
            fds[listenidx].fd     = listensok;
            fds[listenidx].events = POLLIN|POLLPRI|POLLERR|POLLHUP;

            // Position 'signalidx' is always used for the 'signal' socket:
            // the thread that catches zignalz writes to this fd.
            // So if we detect *any* activity or error on THIS one, we quit.
            fds[signalidx].fd      = signalpipe[0];
            fds[signalidx].events  = POLLIN|POLLPRI|POLLERR|POLLHUP;

            // Position 'rotidx' is used for the socket on which we listen
            // for ROT broadcasts.
            fds[rotidx].fd         = rotsok;
            fds[rotidx].events     = POLLIN|POLLPRI|POLLERR|POLLHUP;

            // Loop over the accepted connections
            for(idx=cmdsockoffs, curfd=acceptedfds.begin();
                curfd!=acceptedfds.end(); idx++, curfd++ ) {
                fds[idx].fd     = curfd->first;
                fds[idx].events = POLLIN|POLLPRI|POLLERR|POLLHUP;
            }
            DEBUG(4, "Polling for events...." << endl);

            // Wait forever for something to happen - this is
            // rilly the most efficient way of doing things
            // If the poll did return prematurely, da shit 's hit da fan!
            // Note: it will throw on 'interrupted systemcall' because that
            // should not happen! All signals should go to the
            // signalthread!
#ifdef GDBDEBUG
			while( ::poll(&fds[0], nrfds, -1)==-1 && errno==EINTR);
#else
            ASSERT_COND( ::poll(&fds[0], nrfds, -1)>0 );
#endif
            // Really the first thing to do is the ROT broadcast - if any.
            // It is the most timecritical: it should map systemtime -> rot
            if( (events=fds[rotidx].revents)!=0 ) {
                if( events&POLLIN )
                    process_rot_broadcast( fds[rotidx].fd, environment );
                if( events&POLLHUP || events&POLLERR )
                    DEBUG(0, "ROT-broadcast sokkit is geb0rkt. Therfore delayedplay != werk." << endl);
            }

            // Check the signalsokkit -> if something happened there, we surely
            // MUST break from our loop
            if( (events=fds[signalidx].revents)!=0 ) {
                cerr << "main: Stopping [" << eventor(events) << "] - ";
                if( events&POLLIN ) {
                    int    s( 0 );
                    ::read( fds[signalidx].fd, &s, sizeof(s) );
                    if( s>=0 )
                        cerr << " because of SIG#" << s;
                    else
                        cerr << " because of error in signalthread_fn";
                }
                cerr << endl;
                break;
            }

            // check for new incoming connections
            if( (events=fds[listenidx].revents)!=0 ) {
                // Ok revents not zero: something happened!
                DEBUG(4, "listensok got " << eventor(events) << endl);

                // If the socket's been hung up (can that happen?)
                // we just stop processing commands
                if( events&POLLHUP || events&POLLERR ) {
                    cerr << "Detected hangup of listensocket. Closing down." << endl;
                    break;
                }
                if( events&POLLIN ) {
                    // do_accept_incoming may throw but we don't want to shut
                    // down the whole app upon failure of *that*
                    try {
                        fdprops_type::value_type          v( do_accept_incoming(fds[listenidx].fd) );
                        pair<fdprops_type::iterator,bool> insres;

                        // And add it to the vector of filedescriptors to watch
                        insres = acceptedfds.insert( v );
                        if( !insres.second ) {
                            cerr << "Failed to insert entry into acceptedfds for connection from "
                                << v.second << "!?";
                            ::close( v.first );
                        } else {
                            DEBUG(4, "Incoming on fd#" << v.first << " " << v.second << endl);
                        }
                    }
                    catch( const exception& e ) {
                        cerr << "Failed to accept incoming connection: " << e.what() << endl;
                    }
                    catch( ... ) {
                        cerr << "Unknown exception whilst trying to accept incoming connection?!" << endl;
                    }
                }
                // Done dealing with the listening socket
            }

            // On all other sockets, loox0r for commands!
            for( idx=cmdsockoffs; idx<nrfds; idx++ ) {
                // If no events, nothing to do!
                if( (events=fds[idx].revents)==0 )
                    continue;

                // only now it makes sense to Do Stuff!
                int                    fd( fds[idx].fd );
                fdprops_type::iterator fdptr;

                DEBUG(5, "fd#" << fd << " got " << eventor(events) << endl);

                // Find it in the list
                if(  (fdptr=acceptedfds.find(fd))==acceptedfds.end() ) {
                    // no?!
                    cerr << "Internal error. FD#" << fd
                        << " is in pollfds but not in acceptedfds?!" << endl;
                    continue;
                }

                // if error occurred or hung up: close fd and remove from
                // list of fd's to monitor
                if( events&POLLHUP || events&POLLERR ) {
                    DEBUG(2, "Detected HUP/ERR on fd#" << fd << " [" << fdptr->second << "]" << endl);
                    ::close( fd );

                    acceptedfds.erase( fdptr );
                    // Move on to checking next FD
                    continue;
                }

                // if stuff may be read, see what we can make of it
                if( events&POLLIN ) {
                    char                           linebuf[4096];
                    char*                          sptr;
                    char*                          eptr;
                    string                         reply;
                    ssize_t                        nread;
                    vector<string>                 commands;
                    vector<string>::const_iterator curcmd;

                    // attempt to read a line
                    nread = ::read(fd, linebuf, sizeof(linebuf));

                    // if <=0, socket was closed, remove it from the list
                    if( nread<=0 ) {
                        if( nread<0 ) {
                            lastsyserror_type lse;
                            DEBUG(0, "Error on fd#" << fdptr->first << " ["
                                 << fdptr->second << "] - " << lse << endl);
                        }
                        ::close( fdptr->first );
                        acceptedfds.erase( fdptr );
                        continue;
                    }
                    // make sure line is null-byte terminated
                    linebuf[ nread ] = '\0';

                    // strip all whitespace and \r and \n's.
                    // As per VSI/S, embedded ws is 'illegal'
                    // and leading/trailing is insignificant...
                    sptr = eptr = linebuf;
                    while( *sptr ) {
                        if( ::strchr(" \r\n", *sptr)==0 )
                            *eptr++ = *sptr;
                        sptr++;
                    }
                    // eptr is the new end-of-string
                    *eptr = '\0';


                    commands = ::split(string(linebuf), ';');
                    DEBUG(4,"Found " << commands.size() << " command"
                            << ((commands.size()==1)?(""):("s")) );
                    if( commands.size()==0 )
                        continue;

                    DEBUG(5, "Cmd: " << linebuf << endl);

                    // process all commands
                    // Even if we did receive only whitespace, we still need to
                    // send back *something*. A single ';' for an empty command should
                    // be just fine
                    for( curcmd=commands.begin(); curcmd!=commands.end(); curcmd++ ) {
                        bool                               qry;
                        string                             keyword;
                        const string&                      cmd( *curcmd );
                        vector<string>                     args;
                        string::size_type                  posn;
                        mk5commandmap_type::const_iterator cmdptr;

                        DEBUG(4,"Processing command '" << cmd << "'" << endl);
                        if( cmd.empty() ) {
                            reply += ";";
                            continue;
                        }
                        // find out if it was a query or not
                        if( (posn=cmd.find_first_of("?="))==string::npos ) {
                            reply += ("!syntax = 7 : Not a command or query;");
                            continue;
                        }
                        qry     = (cmd[posn]=='?');
                        keyword = ::tolower( cmd.substr(0, posn) );
                        if( keyword.empty() ) {
                            reply += "!syntax = 7 : No keyword given ;";
                            continue;
                        }

                        // see if we know about this specific command
                        if( (cmdptr=mk5cmds.find(keyword))==mk5cmds.end() ) {
                            reply += (string("!")+keyword+((qry)?('?'):('='))+" 7 : ENOSYS - not implemented ;");
                            continue;
                        }

                        // now get the arguments, if any
                        // (split everything after '?' or '=' at ':'s and each
                        // element is an argument)
                        args = ::split(cmd.substr(posn+1), ':');
                        // stick the keyword in at the first position
                        args.insert(args.begin(), keyword);

                        try {
                            reply += cmdptr->second(qry, args, environment);
                        }
                        catch( const exception& e ) {
                            reply += string("!")+keyword+" = 4 : " + e.what() + ";";
                        }
                    }
                    // processed all commands in the string
                    // send a reply
                    if( reply.find("!play?")==string::npos )
                        DEBUG(2,"Reply: " << reply << endl);
                    // do *not* forget the \r\n ...!
                    reply += "\r\n";
                    ASSERT_COND( ::write(fd, reply.c_str(), reply.size())==(ssize_t)reply.size() );
                }
                // done with this fd
            }
            // done all fds
        }
        DEBUG(2, "Closing listening socket (ending program)" << endl);
        ::close( listensok );

        // the destructor of the runtime will take care of stopping
        // running data-transfer threads, if any 
    }
    catch( const exception& e ) {
        cout << "!!!! " << e.what() << endl;
    }
    catch( ... ) {
        cout << "caught unknown exception?!" << endl;
    }
    // And make sure the signalthread is killed.
    // Be aware that the signalthread may already have terminated, don't treat
    // that as an error ...
    if( signalthread ) {
        int   rv;
        // most likely, if we couldn't send the signal, there's no use in 'join'ing 
        // the signalthread
        if( (rv=::pthread_cancel(*signalthread))!=0 && rv!=ESRCH )
            cerr << "Failed to pthread_cancel(signalthread) - " << ::strerror(rv) << endl;
        else {
            ::pthread_join(*signalthread, 0);
            DEBUG(2, "signalthread joined" << endl);
        }
    }
    delete signalthread;

    return 0;
}
