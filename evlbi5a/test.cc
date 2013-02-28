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
#include <dotzooi.h>
#include <version.h>
#include <interchain.h>
#include <mk5_exception.h>

// system headers (for sockets and, basically, everything else :))
#include <time.h>
#include <sys/poll.h>
#include <sys/timeb.h>
#include <sys/time.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <strings.h> // ::strncasecmp
#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace std;


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
//
// NOTE: during startup the value of -1 might
//       be written to the pipe. this will inform
//       the main thread that the signalfunction
//       failed to set-up correctly and that the
//       thread has exited itself.
void* signalthread_fn( void* argptr ) {
    // zignalz to wait for
    const int          sigz[] = {SIGINT, SIGTERM};
    const unsigned int nsigz  = sizeof(sigz)/sizeof(sigz[0]);
    // variables
    int      sig;
    int      rv;
    int*     fdptr = (int*)argptr;
    sigset_t waitset;

    DEBUG(3, "signalthread_fn: starting" << endl);

    // check if we got an argument
    if( !fdptr ) {
        cerr << "signalthread_fn: Called with nullpointer!" << endl;
        return (void*)-1;
    }

    // make sure we are cancellable and it is 'deferred'
    if( (rv=::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0))!=0 ) {
        cerr << "signalthread_fn: Failed to set canceltype to deferred - "
             << ::strerror(rv) << endl;
        if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
            cerr << "signalthread_fn: Failed to write to main thread - "
                 << ::strerror(rv) << endl;
        return (void*)-1;
    }
    if( (rv=::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0))!=0 ) {
        cerr << "signalthread_fn: Failed to set cancelstate to cancellable - "
             << ::strerror(rv) << endl;
        if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
            cerr << "signalthread_fn: Failed to write to main thread - "
                 << ::strerror(rv) << endl;
        return (void*)-1;
    }

    // goodie. Set up the "waitset", the set of
    // signals to wait for.
    if( (rv=sigemptyset(&waitset))!=0 ) {
        cerr << "signalthread_fn: Failed to sigemptyset() - " << ::strerror(errno) << endl;
        if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
            cerr << "signalthread_fn: Failed to write to main thread - "
                 << ::strerror(rv) << endl;
        return reinterpret_cast<void*>(rv);
    }
    // add the signals
    for(unsigned int i=0; i<nsigz; ++i) {
        if( (rv=sigaddset(&waitset, sigz[i]))!=0 ) {
            cerr << "signalthread_fn: Failed to sigaddset(" << sigz[i] << ") - "
                 << ::strerror(errno) << endl;
        if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
            cerr << "signalthread_fn: Failed to write to main thread - "
                 << ::strerror(rv) << endl;
            return reinterpret_cast<void*>(rv);
        }
    }

    // k3wl. Now wait for ever
    DEBUG(5,"signalthread_fn: entering sigwait" << endl);
#ifdef GDBDEBUG
	while( true ) {
    	rv = ::sigwait(&waitset, &sig);
        // Somehow, in debug mode the sigwait() may
        // return "-1", probably much like the 
        // pthread_cond_wait, a spurious wakeup
        // (own observation)
		if( rv==0 || (rv==-1 && sig==0) )
			break;
	}
#else
	rv = ::sigwait(&waitset, &sig);
#endif
    DEBUG(3,"signalthread_fn: sigwait(&sig)=" << rv << ", sig=" << sig << endl);

    if( rv!=0 )
        // not a zignal but an error!
        DEBUG(-1, "signalthread_fn: Failed to sigwait() - " << ::strerror(rv) << endl);

    if( ::write(*fdptr, &sig, sizeof(sig))!=sizeof(sig) )
        DEBUG(-1, "signalthread_fn: Failed to write signal to pipe" << endl);
    return (void*)0;
}

struct streamstor_poll_args {
    xlrdevice xlrdev;
    bool stop;
};

void* streamstor_poll_fn( void* args ) {
    streamstor_poll_args* ss_args = (streamstor_poll_args*)args;
    bool& stop = ss_args->stop;
    xlrdevice xlrdev = ss_args->xlrdev;


    // prevent flooding of error messages from this thread, 
    // gather them and print every 30s
    const string unknown_exception_message = "Unknown exception";
    const int period = 30;
    map< string, unsigned int > error_repeat_count;
    time_t last_report_time = 0;
    while ( !stop ) {
        try {
            xlrdev.update_mount_status( );
        }
        catch ( std::exception& e ) {
            error_repeat_count[ string(e.what()) ]++;
        }
        catch ( ... ) {
            error_repeat_count[ unknown_exception_message ]++;
        }
        if ( !error_repeat_count.empty() ) {
            time_t now = time( NULL );
            if ( (now - last_report_time) >= period ) {
                if ( last_report_time == 0 ) {
                    DEBUG( -1, "StreamStor poller caught the following exception:" << endl);
                }
                else {
                    DEBUG( -1, "StreamStor poller caught the following exceptions in the last " << (now - last_report_time) << " seconds:" << endl);
                }
                for ( map<string, unsigned int>::const_iterator iter = error_repeat_count.begin(); iter != error_repeat_count.end(); iter++ ) {
                    DEBUG( -1, iter->first << " ( " << iter->second << "X )" << endl);
                }
                error_repeat_count.clear();
                last_report_time = now;
            }
        }
        sleep( 1 );
    }
    return NULL;
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

static const string default_runtime("0");
string process_runtime_command( bool qry,
                                vector<string>& args, 
                                string& current_runtime_name,
                                map<string, runtime*>& environment ) {
    ostringstream tmp;
    if ( qry ) {
        tmp << "!runtime? 0 : " << current_runtime_name << " : " << environment.size() << " ;";
        return tmp.str();
    }

    // command
    if ( args.size() == 2 ) {
        map<string, runtime*>::const_iterator rt_iter =
            environment.find(args[1]);
        if ( rt_iter == environment.end() ) {
            // requested runtime doesn't exist yet, create it
            environment[args[1]] = new runtime();
        }
        current_runtime_name = args[1];
    }
    else if ( args.size() == 3 ) {
        if ( args[2] != "delete" ) {
            return string("!runtime = 6 : second argument to runtime command has to be 'delete' if present;");
        }
        if ( args[1] == default_runtime ) {
            return string("!runtime = 6 : cannot delete the default runtime ;");
        }
        
        // remove the runtime
        map<string, runtime*>::iterator rt_iter = environment.find(args[1]);
        if ( rt_iter == environment.end() ) {
            tmp << "!runtime = 6 : no active runtime '" << args[1] << "' ;";
            return tmp.str();
        }
        if ( current_runtime_name == args[1] ) {
            // if the runtime to delete is the current one, 
            // reset it to the default
            current_runtime_name = default_runtime;
        }
        delete rt_iter->second;
        environment.erase( rt_iter );
    }
    else {
        return string("!runtime= 8 : expects one or two parameters ;") ;
    }
    
    tmp << "!runtime= 0 : " << current_runtime_name << " ;";
    return tmp.str();
}


// main!
int main(int argc, char** argv) {
    int            option;
    int            signalpipe[2] = {-1, -1};
    bool           echo = true; // echo the "Processing:" and "Reply:" commands [in combination with dbg level]
    UINT           devnum( 1 );
    sigset_t       newset;
    pthread_t*     signalthread = 0;
    pthread_t*     streamstor_poll_thread = NULL;
    streamstor_poll_args  streamstor_poll_args;
    unsigned int   numcards;
    unsigned short cmdport = 2620;
    
    // mapping from file descriptor to current runtime name
    map<int, string>      current_runtime;

    // mapping from runtime name to actual runtime environment
    map<string, runtime*> environment;


    try {
        cout << "jive5ab Copyright (C) 2007-2011 Harro Verkouter" << endl;
        cout << "This program comes with ABSOLUTELY NO WARRANTY." << endl;
        cout << "This is free software, and you are welcome to " << endl
             << "redistribute it under certain conditions." << endl
             << "Check gpl-3.0.txt." << endl << endl;

        // The absolutely first thing to do is to make sure our timezone is
        // set to UTC. 
        ::setenv("TZ", "", 1);
        ::tzset();

        // Before we try to initialize hardware or anything
        // [the c'tor of 'environment' does go look for hardware]
        // we parse the commandline.
        // Check commandline
        long int       v;
        S_BANKMODE     bankmode = SS_BANKMODE_NORMAL;
        const long int maxport = 0x7fff;
        bool           do_buffering_mapping = false;

        while( (option=::getopt(argc, argv, "behdm:c:p:r:"))>=0 ) {
            switch( option ) {
                case 'e':
                    echo = false;
                    break;
                case 'h':
                    Usage( argv[0] );
                    return -1;
                case 'd':
                    // set dual/nonbank mode (ie two
                    // banks operating as one volume)
                    bankmode = SS_BANKMODE_DISABLED;
                    break;
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
                case 'b':
                    do_buffering_mapping = true;
                    break;
                case 'r':
                    DEBUG(0, "Warning, runtime argument is deprecated; runtimes are created on-the-fly" << endl);
                    break;
                default:
                   cerr << "Unknown option '" << option << "'" << endl;
                   return -1;
            }
        }

        // Initialize the RNG for the whole system exactly once.
        ::srandom( (unsigned int)time(0) );

        // Good. Now we've done that, let's get down to business!
        int                rotsok;
        int                listensok;
        fdprops_type       acceptedfds;
        pthread_attr_t     tattr;
        // two command maps, the first for the default runtime, 
        // which is hardware specific
        // another for the other runtimes, which don't have hardware available
        mk5commandmap_type rt0_mk5cmds     = mk5commandmap_type();
        mk5commandmap_type generic_mk5cmds = mk5commandmap_type();

        // Start looking for streamstor cards
        numcards = ::XLRDeviceFind();
        cout << "Found " << numcards << " StreamStorCard" << ((numcards!=1)?("s"):("")) << endl;

        // Show user what we found. If we cannot open stuff,
        // we don't even try to create threads 'n all
        xlrdevice  xlrdev;

        if( devnum<=numcards ) {
            xlrdev = xlrdevice( devnum );

            xlrdev.setBankMode( bankmode );
        }

        // check what ioboard we have available
        ioboard_type ioboard( true );
        
        environment[default_runtime] = new runtime( xlrdev, ioboard );
        runtime& rt0( *environment[default_runtime] );
        
        if( !ioboard.hardware().empty() ) {
            // make sure the user can write to DirList file (/var/dir/Mark5A)
            // before we let go of our root permissions
            const char* dirlist_file = "/var/dir/Mark5A";
            if ( ::access(dirlist_file, F_OK) == -1 ) {
                // create empty file
                fstream file( dirlist_file, fstream::trunc | fstream::out );
                ASSERT_COND( file.good() );
                file.close();
            }
            ASSERT_ZERO( ::chown(dirlist_file, ::getuid(), ::getgid()) );
        }

        // Set a default inputboardmode and outputboardmode,
        // depending on which hardware we find
        if( ioboard.hardware()&ioboard_type::mk5a_flag ) {
            rt0.set_input( inputmode_type(inputmode_type::mark5adefault) );
            rt0.set_output( outputmode_type(outputmode_type::mark5adefault) );
        } else if( ioboard.hardware()&ioboard_type::dim_flag ) {
            // DIM: set Mk5B default inputboard mode
            rt0.set_input( mk5b_inputmode_type(mk5b_inputmode_type::mark5bdefault) );
        } else if( ioboard.hardware()&ioboard_type::dom_flag ) {
            // DOM: set Mk5B default inputboard mode
            rt0.set_input( mk5bdom_inputmode_type(mk5bdom_inputmode_type::mark5bdefault) );
        } else if( !ioboard.hardware().empty() ){
            DEBUG(0, "Not setting default input/output boardmode because\n"
                  << "  hardware " << ioboard.hardware() << " not supported (yet)" << endl;);
        }


        // If we're running on Mk5B/DIM we start the dotclock
        // This requires our escalated privilegesesess in order
        // to open the device driver "/dev/mk5bio" [which is strange]
        // Strictly speaking - anyone should be able to open this
        // device file ... it's read-only anyway ... But by opening
        // it as root we're sure that we can open it!
        if( ioboard.hardware() & ioboard_type::dim_flag )
            dotclock_init( ioboard );

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

        if ( xlrdev ) {
            // Now that we have done (1) I/O board detection and (2)
            // have access to the streamstor we can finalize our
            // hardware detection
            if( xlrdev.isAmazon() )
                ioboard.set_flag( ioboard_type::amazon_flag );

            if( ::strncasecmp(xlrdev.dbInfo().FPGAConfig, "10 GIGE", 7)==0 )
                ioboard.set_flag( ioboard_type::tengbe_flag );

            rt0.xlrdev = xlrdev;
        }
        // Almost there. If we detect Mark5B+ we must try to set the I/O
        // board to FPDPII mode
        if( ioboard.hardware() & ioboard_type::mk5b_plus_flag ) {
            ioboard[ mk5breg::DIM_REQ_II ] = 1;
            // give it some time
            ::usleep(100000);
            // now check hw status
            if( *ioboard[mk5breg::DIM_II] ) {
                ioboard.set_flag( ioboard_type::fpdp_II_flag );
            } else {
                DEBUG(-1, "**** MK5B+ Requested FPDP2 MODE BUT DOESN'T WORK!" << endl);
                DEBUG(-1, "  DIM_REQ_II=" << *ioboard[mk5breg::DIM_REQ_II] 
                          << " DIM_II=" << *ioboard[mk5breg::DIM_II] << endl);
                DEBUG(-1, "**** Disabling FPDP2" << endl);
                ioboard[ mk5breg::DIM_REQ_II ] = 0;
            }
        }

        cout << "======= Hardware summary =======" << endl
             << "System: " << ioboard.hardware() << endl
             << endl;

        if( xlrdev )
             cout << xlrdev << endl;
        else
             cout << "No XLR device available" << endl;
        cout << "================================" << endl;

        // create interthread pipe for communication between this thread
        // and the signalthread. If this fails there no use in going on, eh?
        ASSERT_ZERO( ::pipe(signalpipe) );

        // make sure we create a joinable thread
        PTHREAD_CALL( ::pthread_attr_init(&tattr) );
        PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE) );

        // ignore SIGPIPE 
        ASSERT_COND( signal(SIGPIPE, SIG_IGN)!=SIG_ERR );

        // Now is a good time to try to start the zignal thread.
        // Give it as argument a pointer to the write end of the pipe.
        signalthread = new pthread_t;
        PTHREAD2_CALL( ::pthread_create(signalthread, &tattr, signalthread_fn, (void*)&signalpipe[1]),
                       delete signalthread; signalthread = 0; );

        PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );

        // now a thread to poll the streamstor, of course only if we have a xlrdevice
        if ( xlrdev ) {
            // make sure we create a joinable thread
            PTHREAD_CALL( ::pthread_attr_init(&tattr) );
            PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE) );
            streamstor_poll_args.xlrdev = xlrdev;
            streamstor_poll_args.stop = false;
            streamstor_poll_thread = new pthread_t;
            PTHREAD2_CALL( ::pthread_create(streamstor_poll_thread, &tattr, streamstor_poll_fn, (void*)&streamstor_poll_args),
                           delete streamstor_poll_thread; streamstor_poll_thread = NULL; );

            PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );

        }

        // Depending on which hardware we found, we get the appropriate
        // commandmap
        ioboard_type::iobflags_type  hwflags = ioboard.hardware();
        if( hwflags&ioboard_type::mk5a_flag ) {
            rt0_mk5cmds = make_mk5a_commandmap( do_buffering_mapping );
        }
        else if( hwflags&ioboard_type::dim_flag ) {
            rt0_mk5cmds = make_dim_commandmap( do_buffering_mapping );
        }
        else if( hwflags&ioboard_type::dom_flag )
            rt0_mk5cmds = make_dom_commandmap();
        else
            rt0_mk5cmds = make_generic_commandmap();

        // for the other runtimes we always use the generic command map
        generic_mk5cmds = make_generic_commandmap();


        // Goodie! Now set up for accepting incoming command-connections!
        // getsok() will throw if no socket can be created
        listensok = getsok( cmdport, "tcp" );

        // and get a socket on which to lissin for ROT broadcasts
        //rotsok    = getsok("0.0.0.0", 7010, "udp");
        rotsok    = getsok( 7010, "udp" );

        // Wee! 
        DEBUG(-1, "main: jive5a [" << buildinfo() << "] ready" << endl);
        DEBUG(2, "main: waiting for incoming connections" << endl);

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
            struct pollfd*               fds = new pollfd[ nrfds ];
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
            DEBUG(5, "Polling for events...." << endl);

            // Wait forever for something to happen - this is
            // rilly the most efficient way of doing things
            // If the poll did return prematurely, da shit 's hit da fan!
            // Note: it will throw on 'interrupted systemcall' because that
            // should not happen! All signals should go to the
            // signalthread!
#ifdef GDBDEBUG
			while( ::poll(&fds[0], nrfds, -1)==-1 && errno==EINTR) { };
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
                cerr << "main: stopping [" << eventor(events) << "] - ";
                if( events&POLLIN ) {
                    int    s( 0 );
                    ASSERT_COND( ::read( fds[signalidx].fd, &s, sizeof(s) )==sizeof(s) );
                    if( s>=0 )
                        cerr << " because of SIG#" << s;
                    else
                        cerr << " because of error in signalthread_fn";
                }
                cerr << endl;
                delete [] fds;
                break;
            }

            // check for new incoming connections
            if( (events=fds[listenidx].revents)!=0 ) {
                // Ok revents not zero: something happened!
                DEBUG(5, "listensok got " << eventor(events) << endl);

                // If the socket's been hung up (can that happen?)
                // we just stop processing commands
                if( events&POLLHUP || events&POLLERR ) {
                    cerr << "main: detected hangup of listensocket." << endl;
                    delete [] fds;
                    break;
                }
                if( events&POLLIN ) {
                    // do_accept_incoming may throw but we don't want to shut
                    // down the whole app upon failure of *that*
                    try {
                        fdprops_type::value_type          fd( do_accept_incoming(fds[listenidx].fd) );
                        pair<fdprops_type::iterator,bool> insres;

                        // And add it to the vector of filedescriptors to watch
                        insres = acceptedfds.insert( fd );
                        if( !insres.second ) {
                            cerr << "main: failed to insert entry into acceptedfds -\n"
                                 << "      connection from " << fd.second << "!?";
                            ::close( fd.first );
                        } else {
                            DEBUG(5, "incoming on fd#" << fd.first << " " << fd.second << endl);
                            current_runtime[fd.first] = default_runtime;
                        }
                    }
                    catch( const exception& e ) {
                        cerr << "main: failed to accept incoming connection -\n"
                             << e.what() << endl;
                    }
                    catch( ... ) {
                        cerr << "main: unknown exception whilst trying to accept incoming connection?!" << endl;
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
                    cerr << "main: internal error. fd#" << fd << "\n"
                        << "       is in pollfds but not in acceptedfds?!" << endl;
                    continue;
                }

                // if error occurred or hung up: close fd and remove from
                // list of fd's to monitor
                if( events&POLLHUP || events&POLLERR ) {
                    DEBUG(4, "detected HUP/ERR on fd#" << fd << " [" << fdptr->second << "]" << endl);
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

                    // HV: 18-nov-2012
                    //     telnet sends \r\n, tstdimino sends a separate \n
                    //     (i.e. you're receiving two packets).
                    //     telnet needs \r\n in the reply, tstdimino needs \n
                    //       because it uses fgets(2)
                    //     tstdimino didn't like the fact that the empy "\n"
                    //     got a ";" reply from jive5ab - which could be
                    //     easily be diagnosed as a jive5ab bug. Now, if
                    //     jive5ab gets an empty command it does not send a
                    //     ';' as reply no more.
                    const bool  crlf = string(linebuf).find("\r\n")!=string::npos;

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
                    DEBUG(5,"Found " << commands.size() << " command"
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

                        if( cmd.empty() )
                            continue;
                        DEBUG((echo?2:10000), "Processing command '" << cmd << "'" << endl);

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

                        // now get the arguments, if any
                        // (split everything after '?' or '=' at ':'s and each
                        // element is an argument)
                        args = ::split(cmd.substr(posn+1), ':');
                        // stick the keyword in at the first position
                        args.insert(args.begin(), keyword);

                        // see if we know about this specific command
                        
                        if( keyword == "runtime" ) {
                            // select a runtime to pass to the functions
                            reply += process_runtime_command( qry, args, current_runtime[fd], environment );
                        } else if( keyword=="echo" ) {
                            // turn command echoing on or off
                            if( qry ) {
                                ostringstream tmp;
                                tmp << "!echo? 0 : " << (echo?"on":"off") << " ;";
                                reply += tmp.str();
                            } else if( args.size()!=2 || !(args[1]=="on" || args[1]=="off") ) {
                                reply += string("!echo= 8 : expects exactly one parameter 'on' or 'off';") ;
                            } else {
                                // already verified we have 'on' or 'off'
                                echo = (args[1]=="on");
                                reply += string("!echo= 0 ;");
                            }
                        } else {
                            mk5commandmap_type& mk5cmds = ( current_runtime[fd] == default_runtime ? rt0_mk5cmds : generic_mk5cmds );
                            if( (cmdptr=mk5cmds.find(keyword))==mk5cmds.end() ) {
                                reply += (string("!")+keyword+((qry)?('?'):('='))+" 7 : ENOSYS - not implemented ;");
                                continue;
                            }

                            try {
                                reply += cmdptr->second(qry, args, *environment[current_runtime[fd]]);
                            }
                            catch( const Error_Code_6_Exception& e) {
                                reply += string("!")+keyword+" = 6 : " + e.what() + ";";
                            }
                            catch( const cmdexception& e ) {
                                reply += string("!")+keyword+" = 6 : " + e.what() + ";";
                            }
                            catch( const exception& e ) {
                                reply += string("!")+keyword+" = 4 : " + e.what() + ";";
                            }
                            catch( ... ) {
                                reply += string("!")+keyword+" = 4 : unknown exception ;";
                            }
                            // do the protect=off bookkeeping
                            environment[current_runtime[fd]]->protected_count = max(environment[current_runtime[fd]]->protected_count, 1u) - 1;
                        }
                    }

                    if( reply.empty() ) {
                        DEBUG(4, "No command(s) found, no reply sent" << endl);
                        break;
                    }
                    // processed all commands in the string. send the reply
                    DEBUG((echo?2:10000), "Reply: " << reply << endl);
                    // do *not* forget the \r\n ...!
                    // HV: 18-nov-2011 see above near 'const bool crlf =...';
                    if( crlf )
                        reply += "\r\n";
                    else
                        reply += "\n";
                    ASSERT_COND( ::write(fd, reply.c_str(), reply.size())==(ssize_t)reply.size() );
                }
                // done with this fd
            }
            // done all fds
            delete [] fds;
        }
        DEBUG(2, "closing listening socket (ending program)" << endl);
        ::close( listensok );

        // the destructor of the runtime will take care of stopping
        // running data-transfer threads, if any 
    }
    catch( const exception& e ) {
        cout << "main: " << e.what() << " !!?!" << endl;
    }
    catch( ... ) {
        cout << "main: caught unknown exception?!" << endl;
    }

    // The dot-clock can be stopped now. This can be done unconditionally;
    // the routine knows wether or not the dotclock was running
    dotclock_cleanup();

    // And make sure the signalthread and streamstor poll thread are killed.
    // Be aware that the signalthread may already have terminated, don't treat
    // that as an error ...
    streamstor_poll_args.stop = true;
    if( signalthread ) {
        int   rv;

        if( signalpipe[1]!=-1 )
            ::close(signalpipe[1]);

        // most likely, if we couldn't send the signal, there's no use in 'join'ing 
        // the signalthread
        DEBUG(4,"killing signalthread ..." << endl);
        if( (rv=::pthread_kill(*signalthread, SIGTERM))!=0 && rv!=ESRCH ) {
            cerr << "main: failed to pthread_kill(signalthread) -\n"
                 << ::strerror(rv) << endl;
        }
        if( rv!=0 ) {
            DEBUG(4," now joining .." << endl);
            ::pthread_join(*signalthread, 0);
        }
    }
    if ( streamstor_poll_thread ) {
        ::pthread_join(*streamstor_poll_thread, 0);
        delete streamstor_poll_thread;
    }
    delete signalthread;
    for ( map<string, runtime*>::iterator rt_iter = environment.begin();
          rt_iter != environment.end();
          rt_iter++ ) {
        delete rt_iter->second;
    }
    return 0;
}
