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
#include <set>
#include <vector>
#include <algorithm>
#include <locale>

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
#include <carrayutil.h>
#include <scan_label.h>
#include <ezexcept.h>
#include <mk6info.h>
#include <sciprint.h>
#include <sfxc_binary_command.h>

// system headers (for sockets and, basically, everything else :))
#include <time.h>
#include <sys/poll.h>
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
#include <libgen.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace std;

#define QRY(q)  ((q?"?":"="))


DECLARE_EZEXCEPT(bookkeeping)
DEFINE_EZEXCEPT(bookkeeping)

// Per runtime name keep the pointer to the actual runtime object and a set
// of file descriptors (connections) which refer to it
typedef set<int>    fdset_type;

bool is_member(int fd, const fdset_type& fds) {
    return fds.find(fd)!=fds.end();
}

struct per_rt_data {
    int         owner;     // fd of owner if >=0. If owner goes, then also the runtime
    runtime*    rteptr;
    fdset_type  observers;

    per_rt_data(runtime* r):
        owner( -1 ), rteptr( r )
    {}
    per_rt_data(runtime* r, int o):
        owner( o ), rteptr( r )
    {}
};

typedef map<string, per_rt_data> runtimemap_type;

// Per connection (file descriptor) keep track of the echo setting and which
// runtime it referred to
struct per_fd_data {
    bool    echo;
    string  runtime;

    per_fd_data(bool e):
        echo( e )
    {}
};

typedef map<int, per_fd_data>  fdmap_type;

// returns iterator to the actual runtime IF the runtime exists AND has a
// backreference to the fdmentry (i.e. if the fdmptr's file descriptor is in
// the observers list for the runtime).
// If not, then return rmt.end().
runtimemap_type::iterator current_runtime(fdmap_type::iterator fdmptr, runtimemap_type& rtm) {
    runtimemap_type::iterator  rtmptr = rtm.find( fdmptr->second.runtime );

    if( rtmptr!=rtm.end() && ::is_member(fdmptr->first, rtmptr->second.observers) )
        return rtmptr;
    return rtm.end();
}

// De-associate whatever the current connection was referring to and
// re-associate it to whatever it should be associated with
void observe(const string& rt, fdmap_type::iterator fdmptr, runtimemap_type& rtm) {
    runtimemap_type::iterator rtmptr;

    // If we cannot find the entry the fd _was_ referring to, that's not an
    // error; someone may have deleted that one. In case we *do* find it, 
    // we de-associate the fd from that runtime
    rtmptr = rtm.find( fdmptr->second.runtime );
    
    if( rtmptr!=rtm.end() ) {
        // remove if we were observing that one
        // Note: if we didn't - this is not an error; someone has deleted + created the
        // runtime behind our backs.
        fdset_type::iterator   fdsptr = rtmptr->second.observers.find( fdmptr->first );
        if( fdsptr!=rtmptr->second.observers.end() )
            rtmptr->second.observers.erase( fdsptr );
    }

    // Now go find the runtime we want to be re-associated with.
    // Not finding *that* one IS an error!
    rtmptr = rtm.find( rt );

    EZASSERT2(rtmptr!=rtm.end(), bookkeeping,
              EZINFO("runtime '" << rt << "' (to be observed by fd#" << fdmptr->first << ") not in runtimemap administration"));

    // We can, without checking, add ourselves as observers
    rtmptr->second.observers.insert( fdmptr->first );
    // And safely indicate that we now reference 'rt'
    fdmptr->second.runtime = rt;
}

// Assume the file descriptor fd is not valid anymore - remove 
// the references to it
void unobserve(int fd, fdmap_type& fdm, runtimemap_type& rtm) {
    fdmap_type::iterator      fdmptr;
    runtimemap_type::iterator rtmptr;

    fdmptr = fdm.find( fd );
    EZASSERT2(fdmptr!=fdm.end(), bookkeeping, EZINFO("fd#" << fd << " not in fdmap administration"));

    rtmptr = rtm.find( fdmptr->second.runtime );
    if( rtmptr!=rtm.end() ) {
        // Ok, remove the file descriptor as observer
        fdset_type::iterator fdsptr = rtmptr->second.observers.find( fdmptr->first ); 
        if( fdsptr!=rtmptr->second.observers.end() )
            rtmptr->second.observers.erase( fdsptr );
    }
    // Erase fd entry from the fdmap
    fdm.erase( fdmptr );

    // So, the filedescriptor fd is not valid anymore. Now iterate over the
    // runtime map and delete all runtime(s) which have this fd as owner
    typedef std::vector<runtimemap_type::iterator> erase_type;
    erase_type  rts_to_erase;

    for(runtimemap_type::iterator currtm=rtm.begin(); currtm!=rtm.end(); currtm++)
        if( currtm->second.owner==fd )
            rts_to_erase.push_back( currtm );
    for(erase_type::iterator eraseptr=rts_to_erase.begin(); eraseptr!=rts_to_erase.end(); eraseptr++) {
        DEBUG(4, "unobserve: delete runtime " << (*eraseptr)->first << " because fd#" << fd << " is gone" << endl);
        delete (*eraseptr)->second.rteptr;
        rtm.erase( *eraseptr );
    }
}

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
        os << "POLLIN,";
    if( e&POLLERR )
        os << "POLLERR, ";
    if( e&POLLHUP )
        os << "POLLHUP, ";
    if( e&POLLPRI )
        os << "POLLPRI, ";
    if( e&POLLOUT )
        os << "POLLOUT, ";
    if( e&POLLNVAL )
        os << "POLLNVAL!, ";
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
             << evlbi5a::strerror(rv) << endl;
        if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
            cerr << "signalthread_fn: Failed to write to main thread - "
                 << evlbi5a::strerror(rv) << endl;
        return (void*)-1;
    }
    if( (rv=::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0))!=0 ) {
        cerr << "signalthread_fn: Failed to set cancelstate to cancellable - "
             << evlbi5a::strerror(rv) << endl;
        if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
            cerr << "signalthread_fn: Failed to write to main thread - "
                 << evlbi5a::strerror(rv) << endl;
        return (void*)-1;
    }

    // goodie. Set up the "waitset", the set of
    // signals to wait for.
    if( (rv=sigemptyset(&waitset))!=0 ) {
        cerr << "signalthread_fn: Failed to sigemptyset() - " << evlbi5a::strerror(errno) << endl;
        if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
            cerr << "signalthread_fn: Failed to write to main thread - "
                 << evlbi5a::strerror(rv) << endl;
        return reinterpret_cast<void*>(rv);
    }
    // add the signals
    for(unsigned int i=0; i<nsigz; ++i) {
        if( (rv=sigaddset(&waitset, sigz[i]))!=0 ) {
            cerr << "signalthread_fn: Failed to sigaddset(" << sigz[i] << ") - "
                 << evlbi5a::strerror(errno) << endl;
            if( ::write(*fdptr, &rv, sizeof(rv))!=sizeof(rv) )
                cerr << "signalthread_fn: Failed to write to main thread - "
                     << evlbi5a::strerror(rv) << endl;
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
        DEBUG(-1, "signalthread_fn: Failed to sigwait() - " << evlbi5a::strerror(rv) << endl);

    if( ::write(*fdptr, &sig, sizeof(sig))!=sizeof(sig) )
        DEBUG(-1, "signalthread_fn: Failed to write signal to pipe" << endl);
    return (void*)0;
}

struct streamstor_poll_args {
    bool      stop;
    runtime*  rteptr;
    xlrdevice xlrdev;
};

void* streamstor_poll_fn( void* args ) {
    streamstor_poll_args* ss_args = (streamstor_poll_args*)args;
    bool&     stop = ss_args->stop;
    runtime*  rteptr = ss_args->rteptr;
    xlrdevice xlrdev = ss_args->xlrdev;

    // prevent flooding of error messages from this thread, 
    // gather them and print every 30s
    time_t                       last_report_time = 0;
    const int                    period = 30;
    const string                 unknown_exception_message = "Unknown exception";
    map<string, unsigned int>    error_repeat_count;
    xlrdevice::mount_status_type mount_status;

    while ( !stop ) {
        try {
            // If we detect a bank change/mount/dismount
            // then we erase the current scan settings.
            // If there *is* a disk pack with scans on it,
            // we automatically select scan #1 
            xlrdevice::mount_status_type  tmp = xlrdev.update_mount_status();

            if( mount_status!=tmp ) {
                scopedrtelock   srtl( *rteptr );

                // Yay. runtime must be re-initialized.
                if( xlrdev.nScans() ) {
                    rteptr->setCurrentScan( 0 );
                } else {
                    rteptr->current_scan = 0;
                    rteptr->pp_current   = playpointer();
                    rteptr->pp_end       = playpointer();
                }
            }
            mount_status = tmp;
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

// Beware of basename(3) returning NULL (if it ever does that)
char const* get_basename( char* const argv0 ) {
    char const*const   bn = ::basename( argv0 );
    return (bn == 0 ? argv0 : bn);
}

typedef sciprint<size_map_type::mapped_type, 1024> minbs_print_type;

void Usage( const char* name ) {
    size_map_type::mapped_type const vbs_bs(mk6info_type::minBlockSizeMap[false]),
                                     mk6_bs(mk6info_type::minBlockSizeMap[true]);
    cout <<
"Usage: " << name << " [-hned6*] [-m <level>] [-c <card>] [-p <port>] [-S <where>]\n"
"              [-S <where>] [-f <fmt>] [-B <size>]\n\n"
"   -h, --help this message\n"
"   -v, --version\n"
"              display version information and exit succesfully\n"
"   -n, --no-buffering\n"
"              do not 'buffer' - recorded data is NOT put into memory\n"
"              this is the default mode\n"
"   -b, --buffering\n"
"              when recording, also read the data into a memory buffer\n"
"   -m, --message-level <level>\n"
"              message level (default " << dbglev_fn() << ")\n"
"              higher number is more verbose output. Stay below 3\n"
"   -c, --card <card>\n"
"              card index, default StreamStor number '1' is used\n"
"   -p, --port <port>\n"
"              TCP port number to listen for incoming commands\n"
"              connections. Default is port 2620 (mark5 default)\n"
"   -6, --mark6 by default select Mark6 disk mountpoints for recording\n"
"              rather than FlexBuff, which is the default\n"
"   -f, --format <fmt>\n"
"              set default vbs recording format to <fmt>. Valid <fmt> values:\n"
"                 mk6      = MIT Haystack Mark6 dplane v1.2+ compatible\n"
"                 flexbuff = FlexBuff format (this is the default)\n"
"   -e, --echo do NOT echo 'Command' and 'Reply' statements,\n"
"              irrespective of message level\n"
"   -d, --dual-bank\n"
"              start in dual bank mode (default: bank mode)\n"
"   -B, --min-block-size <size in bytes>\n"
"              Set default minimum block size for vbs/mk6 recordings\n"
"              for the selected default recording format (see '-f')\n"
"              <size> can use suffix 'k' or 'M' for 1024 or 1048576 multipliers\n"
"              Defaults for the formats: \n"
"                  vbs: " << vbs_bs << " (" << minbs_print_type(vbs_bs, "Byte") << ")\n"
"                  mk6: " << mk6_bs << " (" << minbs_print_type(mk6_bs, "Byte") << ")\n"
"   -*, --allow-root\n"
"              do NOT drop privileges before accepting input\n"
"              this may be necessary to capture data from\n"
"              privileged ports (0 <= net_port <= 1024)\n"
"   -S, --sfxc-port <where>\n"
"              start server for SFXC binary commands on <where>\n"
"              recognized formats for <where> are\n"
"                <where> = [0-9]+ => open TCP server on port <where>\n"
"                <where> = *      => open UNIX server on path <where>\n"
"              Default: do not listen for SFXC binary commands\n";
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

runtime* get_runtimeptr( runtimemap_type::value_type const& p ) {
    return p.second.rteptr;
}

// Suggestion by Jon Q now that runtiems are exposed to the user
// (documentation fallout) to rename default runtime's name to
// 'default' to make it stand out in e.g. 'runtime?'
static const string default_runtime("default");

string process_runtime_command( bool qry,
                                vector<string>& args,
                                fdmap_type::iterator fdmptr,
                                runtimemap_type& rtm) {
    if ( qry ) {
        ostringstream tmp;
        // As a convenience, let's return the list of runtime names, where
        // the first one is the current one
        tmp << "!runtime? 0 : " << fdmptr->second.runtime << " : " << rtm.size();
        for(runtimemap_type::const_iterator p=rtm.begin(); p!=rtm.end(); p++)
            if( p->first!=fdmptr->second.runtime )
                tmp << " : " << p->first;
        tmp << " ;";
        return tmp.str();
    }

    // command
    //
    // runtime = XXXX 
    //      switch to observing runtime XXXX, potentially create it if it
    //      does not exist
    //
    // runtime = XXXX : new | exists | transient
    //      dedicated 'creation' flags:
    //          'new'       same function as the O_EXCL flag when opening files:
    //                      the runtime may not exist yet
    //          'exists'    transforms the command into a query; tests if the
    //                      runtime named XXXX exists or not. return error
    //                      if it does not
    //          'transient' new in 2.7.3 and up: the runtime must not
    //                      exist yet and the creating control connection
    //                      will be listed as 'owner' of the runtime. If the
    //                      control connection goes, the runtime will be
    //                      deleted automatically.
    
    // Command *must* have one or two arguments
    if( args.size()<2 || args.size()>3 )
        return string("!runtime= 8 : expects one or two parameters ;") ;
    // Having verified that, we can extract the runtime name and the
    // optional command
    const string   rt_name( (args[1]=="0") ? default_runtime : args[1] );
    const string   rt_cmd( (args.size()>2) ? args[2]         : string() );

    if( rt_cmd.empty() /* no subcommand => create if not exist */ ||
        (rt_cmd=="new" || rt_cmd=="exists" || rt_cmd=="transient") ) {
        // Check if we have a runtime by the name of 'rt_name'
        runtimemap_type::const_iterator rt_iter = rtm.find(rt_name);

        if ( rt_iter == rtm.end() ) {
            // No. We didn't have one.
            if ( rt_cmd == "exists" ) {
                return string("!runtime = 6 : '"+rt_name+"' doesn't exist ;");
            }
            // Before blindly creating a runtime, enforce a non-empty name!
            if( rt_name.empty() )
                return string("!runtime = 4 : cannot create runtime with no name ;");

            // requested runtime doesn't exist yet, create it
            if( rt_cmd == "transient" )
                rtm.insert( make_pair(rt_name, per_rt_data(new runtime(), fdmptr->first)) );
            else
                rtm.insert( make_pair(rt_name, per_rt_data(new runtime())) );
        }
        else if ( rt_cmd == "new" || rt_cmd == "transient" ) {
            // we requested a brand new runtime, it already existed, so report an error
            return string("!runtime = 6 : '"+rt_name+"' already exists ;");
        }
        // Ok - the current connection (fdmptr) wishes to be associated with
        // a new runtime!
        ::observe(rt_name, fdmptr, rtm);
    }
    else {
        // *must* be 'delete' 
        if ( rt_cmd != "delete" ) {
            return string("!runtime = 6 : second argument to runtime command has to be 'delete', 'exists', 'transient' or 'new' if present;");
        }

        if ( rt_name == default_runtime ) {
            return string("!runtime = 6 : cannot delete the default runtime ;");
        }
        
        // remove the runtime - if we can find it
        runtimemap_type::iterator rt_iter = rtm.find(rt_name);

        if ( rt_iter == rtm.end() ) {
            return string("!runtime = 6 : no active runtime '") + rt_name + "' ;";
        }

        if ( fdmptr->second.runtime == rt_name ) {
            // if the runtime to delete is the current one, 
            // reset it to the default
            ::observe(default_runtime, fdmptr, rtm);
        }
        delete rt_iter->second.rteptr;
        rtm.erase( rt_iter );
    }
   
    return string("!runtime = 0 : ") + fdmptr->second.runtime + " ;"; 
}

typedef enum { no_sfxc = 0, lissen_tcp, lissen_unix } sfxc_lissen_type;

// main!
int main(int argc, char** argv) {
    int                   option;
    int                   signalpipe[2] = {-1, -1};
    bool                  echo = true; // echo the "Processing:" and "Reply:" commands [in combination with dbg level]
    bool                  drop_privilege = true; // only if absolutely necessary do not do this
    UINT                  devnum( 1 );
    string                sfxc_option; // empty => no lissen; [0-9]+ => TCP; otherwise => UNIX [see sfxc_lissen below]
    sigset_t              newset;
    pthread_t*            signalthread = 0;
    pthread_t*            streamstor_poll_thread = 0;
    unsigned int          numcards;
    unsigned short        cmdport = 2620, sfxc_port = 0;
    sfxc_lissen_type      sfxc_lissen = no_sfxc;
    streamstor_poll_args  streamstor_poll_args;
    
    // mapping from file descriptor to properties
    fdmap_type            fdmap;

    // mapping from runtime name to properties
    runtimemap_type       runtimes;

    // used when only the values of the above map are needed
    vector<runtime*>      environment_values;


    try {

        // The absolutely first thing to do is to make sure our timezone is
        // set to UTC. 
        ::setenv("TZ", "", 1);
        ::tzset();

        // Set the locale to use to POSIX
        std::locale::global( std::locale("POSIX") );

        // Initialize the RNGs for the whole system exactly once, before any
        // threads are active
        ::srandom( (unsigned int)::time(0) );
        ::srand48( (long)::time(0) );


        // Before we try to initialize hardware or anything
        // [the c'tor of 'environment' does go look for hardware]
        // we parse the commandline.
        // Check commandline
        bool         do_buffering_mapping = false;
        long int     v;
        S_BANKMODE   bankmode = SS_BANKMODE_NORMAL;
        unsigned int minimum_bs = 0;

        struct option  longopts[] = {
            { "echo",          no_argument,       NULL, 'e' },
            { "help",          no_argument,       NULL, 'h' },
            { "dual-bank",     no_argument,       NULL, 'd' },
            { "message-level", required_argument, NULL, 'm' },
            { "buffering",     no_argument,       NULL, 'b' },
            { "no-buffering",  no_argument,       NULL, 'n' },
            { "runtimes",      required_argument, NULL, 'r' },
            { "card",          required_argument, NULL, 'c' },
            { "port",          required_argument, NULL, 'p' },
            { "mark6",         no_argument,       NULL, '6' },
            { "format",        required_argument, NULL, 'f' },
            { "sfxc-port",     required_argument, NULL, 'S' },
            { "min-block-size",required_argument, NULL, 'B' },
            { "allow-root",    no_argument,       NULL, '*' },
            { "version",       no_argument,       NULL, 'v' },
            // Leave this one as last
            { NULL,            0,                 NULL, 0   }
        };

        while( (option=::getopt_long(argc, argv, "nbehdm:c:p:r:6*f:S:B:v", longopts, NULL))>=0 ) {
            switch( option ) {
                case '*':
                    // ok .. someone might allow us to run with root privilege!
                    drop_privilege = false;
                    break;
                case 'e':
                    echo = false;
                    break;
                case 'h':
                    Usage( get_basename(argv[0]) );
                    return -1;
                case 'v':
                    // print version info and exit succesfully
                    cout << buildinfo() << endl;
                    return 0;
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
                    if( v<0 || v>USHRT_MAX ) {
                        cerr << "Value for port is out-of-range.\n"
                            << "Useful range is: [0, " << USHRT_MAX << "] (inclusive)" << endl;
                        return -1;
                    }
                    cmdport = ((unsigned short)v);
                    break;
                case 'b':
                    do_buffering_mapping = true;
                    break;
                case 'n':
                    do_buffering_mapping = false;
                    break;
                case 'r':
                    DEBUG(0, "Warning, runtime argument is deprecated; runtimes are created on-the-fly" << endl);
                    break;
                // Mark6 specific flags. Allow orthogonal selection of default
                // mountpoints & recording format
                case '6':
                    // Default to finding Mark6 mountpoints
                    mk6info_type::defaultMk6Disks = true;
                    break;
                case 'f':
                    // Which format to record in?
                    {
                        const string    fmt( optarg );
                        if( fmt=="mk6" )
                            mk6info_type::defaultMk6Format = true;
                        else if( fmt.find("flexbuf")==0 )
                            mk6info_type::defaultMk6Format = false;
                        else {
                            cerr << "Unknown recording format " << fmt << " specified." << endl
                                 << "   choose one from 'mk6' or 'flexbuff'" << endl;
                            return -1;
                        }
                    }
                    break;
                case 'S':
                    // Exactly how do we need to lissen for sfxc command(s)?
                    {
                        char*   endptr;

                        // Save actual option value for (potential) later use
                        sfxc_option = string(optarg);

                        // Check if made of all numbers [and we only do decimal numbers]
                        v = ::strtol(sfxc_option.c_str(), &endptr, 10);

                        // From strtol(3):
                        // "If endptr is not NULL, strtol() stores the address of the first invalid
                        // character in *endptr.  If there were no digits at all, however,
                        // strtol() stores the original value of str in *endptr.  (Thus, if *str is
                        // not `\0' but **endptr is `\0' on return, the entire string was valid.)"
                        if( endptr!=sfxc_option.c_str() &&  /* OK, strtol() did parse at least *some* characters*/
                            *endptr=='\0' ) {               /* it's now pointing at end-of-string so all characters were parsed */
                            // So the whole string was made of base-10 digits. Yay!
                            // Now check if it's out-of-range for portrange
                            if( v<0 || v>USHRT_MAX ) {
                                cerr << "Value for sfxc port is out-of-range.\n"
                                    << "Useful range is: [0, " << USHRT_MAX << "] (inclusive)" << endl;
                                return -1;
                            }
                            sfxc_port   = (unsigned short)v;
                            sfxc_lissen = lissen_tcp;
                        } else {
                            sfxc_lissen = lissen_unix;
                        }
                    }
                    break;
                case 'B':
                    // Set default block size - note: we store it for later
                    // use because it will be tied to the default recording
                    // format, which may or may not be changed on the
                    // command line as well
                    {
                        char*               eptr;
                        const uint32_t      max_blocksize( std::min(((uint32_t)1)<<30, static_cast<uint32_t>(UINT_MAX)) );
                        unsigned long int   bs;
                      
                        // Convert as many digits as we can 
                        errno = 0;
                        bs    = ::strtoul(optarg, &eptr, 0);

                        // was a unit given? [note: all whitespace has already been stripped
                        // by the main commandloop]
                        if( eptr==optarg /*no digits at all*/ ||
                            errno==ERANGE || errno==EINVAL /*something went wrong*/ ) {
                                cerr << "Minimum block size '" << optarg << "' is not a number or out of range" << endl;
                                return -1;
                        }
                        // Optional suffixes kM supported
                        if( *eptr!='\0' ) {
                            if( ::strchr("kM", *eptr)==NULL || *(eptr+1)!='\0' ) {
                                cerr << "Invalid block size unit " << eptr << " - only 'k' (x1024) or 'M' (x1024^2) supported" << endl;
                                return -1;
                            }
                            // at least 'k'
                            bs *= 1024;
                            // maybe 'M'
                            if( *eptr=='M' )
                                bs *= 1024;
                        }
                        if( bs > max_blocksize ) {
                            cerr << "Maximum block size of " << max_blocksize << " exceeded by new minimum block size " << optarg << endl;
                            return -1;
                        }
                        if( bs == 0 ) {
                            cerr << "Minimum block size of 0 not allowed!" << endl;
                            return -1;
                        }
                        minimum_bs = (unsigned int)bs;
                    }
                    break;
                default:
                   cerr << "Unknown option '" << option << "'" << endl;
                   return -1;
            }
        }
        cout << "jive5ab Copyright (C) 2007-2020 Harro Verkouter" << endl;
        cout << "This program comes with ABSOLUTELY NO WARRANTY." << endl;
        cout << "This is free software, and you are welcome to " << endl
             << "redistribute it under certain conditions." << endl
             << "Check gpl-3.0.txt." << endl << endl;

        // If the user indicated a different minimum block size, then
        // install that value
        if( minimum_bs!=0 )
            mk6info_type::minBlockSizeMap[ mk6info_type::defaultMk6Format ] = minimum_bs;

        // Block all zignalz. Not interested in the old mask as we
        // won't be resetting the sigmask anyway
        // Do this before we do anything else such that ANY thread that's
        // created (eg by "libssapi" (!)) has all signals blocked. This
        // guarantees that only our thread "signalthread" will catsj teh zignalz!
        ASSERT_ZERO( sigfillset(&newset) );
        PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &newset, 0) );

        // Good. Now we've done that, let's get down to business!
        int                rotsok = -1;
        int                listensok;
        int                sfxcsok = -1;
        fdprops_type       acceptedfds, acceptedsfxcfds;
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
        // check what ioboard we have available. Note that
        // the ioboard follows availability of streamstor;
        // in theory one could operate the i/o board without the streamstor
        // but then again, there would be no point at all in that.
#ifdef NOSSAPI
        const bool         haveStreamStor = false;
#else
        const bool         haveStreamStor = (devnum>0 && devnum<=numcards);
#endif
        xlrdevice          xlrdev;
        ioboard_type       ioboard( haveStreamStor );

        if( haveStreamStor ) {
            xlrdev = xlrdevice( devnum );

            xlrdev.setBankMode( bankmode );
        }

        // Create the default runtime and store in map; the one with access
        // to the hardware
        EZASSERT2(runtimes.insert( make_pair(default_runtime, per_rt_data(new runtime(xlrdev, ioboard))) ).second,
                  bookkeeping, EZINFO("Failed to put default runtime into runtime-map?!!!"));
        runtime&  rt0( *(runtimes.find(default_runtime)->second.rteptr) );
        
        if( !ioboard.hardware().empty() ) {
            // make sure the user can write to DirList file (/var/dir/Mark5A)
            // before we let go of our root permissions
            int         a;
            const char* dirlist_file = "/var/dir/Mark5A";

            if ( ::access(dirlist_file, F_OK) == -1 ) {
                // attempt to create empty file
                fstream file( dirlist_file, fstream::trunc | fstream::out );
                file.close();
            }
            // Only assert if the file is there.
            // (We've seen instances where "/var/dir" didn't exist, in
            // which case there's little point in trying to change ownership
            // of /var/dir/Mark5A ...
            ASSERT_COND( (a=::access(dirlist_file, F_OK))==-1 || (a==0 && ::chown(dirlist_file, ::getuid(), ::getgid())==0) );
        }
        
        // compile all regular expression needed for scan label checking
        scan_label::initialize(ioboard.hardware());

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
        // 22Jun2019 - Weeelllll ... (famous last words there)
        //             The fucking RDBEs send UDP frames on port 625!!
        //             A fucking privileged port WHICH CANNOT BE CHANGED!
        //             This requires the recording application to have 
        //             fucking ROOT PRIVILEGE ffs.
        //             But that also means that we must change ownership of 
        //             any file we create or else root will be the only
        //             one being able to deal with them.
        if( drop_privilege )
            ASSERT_ZERO( ::setreuid(::getuid(), ::getuid()) );
        // See what we're left with. If our effective uid == 0
        // we're running with root privilege. So we warn.
        if( ::geteuid()==0 ) {
            // Remember: the ownership chaning stuff can only be done by root.
            DEBUG(-1, "+++++++++++++++++++++++++++++++++++++++++++++++++++\n");
            DEBUG(-1, "+                                                 +\n");
            DEBUG(-1, "+       jive5ab running with root privilege       +\n");
            DEBUG(-1, "+                                                 +\n");
            DEBUG(-1, "+++++++++++++++++++++++++++++++++++++++++++++++++++\n");
            // and if the real user id != 0 it means we were running suid root
            // which in turn means we must configure for changing owership
            if( (mk6info_type::real_user_id=::getuid())!=0 ) {
                mk6info_type::fchown_fn = ::fchown;
                mk6info_type::chown_fn  = ::chown;
            }
        }

        if ( xlrdev ) {
            // Now that we have done (1) I/O board detection and (2)
            // have access to the streamstor we can finalize our
            // hardware detection
            if( xlrdev.isAmazon() )
                ioboard.set_flag( ioboard_type::amazon_flag );
            else
                ioboard.set_flag( ioboard_type::streamstor_flag );

            if( ::strncasecmp(xlrdev.dbInfo().FPGAConfig, "10 GIGE", 7)==0 )
                ioboard.set_flag( ioboard_type::tengbe_flag );
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

        // Set a default inputboardmode and outputboardmode,
        // depending on which hardware we have found
        if( ioboard.hardware()&ioboard_type::mk5a_flag ) {
            rt0.set_input( inputmode_type(inputmode_type::mark5adefault) );
            rt0.set_output( outputmode_type(outputmode_type::mark5adefault) );
        } else if( ioboard.hardware()&ioboard_type::dim_flag ) {
            // DIM: set Mk5B default inputboard mode
            // HV: 13 sep 2013 - No we don't. We should recover the
            //                   previous settings from the I/O board
            //                   at startup!
            mk5b_inputmode_type  boardconfig( mk5b_inputmode_type::empty );

            // get values from the registers into the boardconfig variable
            // this only copies the *register* values into its internal copy
            // and into our local variable. 
            // Non-recoverable settings like "data source" and "clockfreq"
            // need to be reconstructed by us. After having done that we
            // write the updated config back to the runtime
            rt0.get_input( boardconfig );

            // After a reboot the bitstreammask is 0; let's take
            // that as sentinel for "we have no mode yet"
            // In that case program the full mk5b default
            // "K" == 0 is a valid value - it represents 2MHz clock rate
            if( boardconfig.bitstreammask==0 ) {
                boardconfig = mk5b_inputmode_type( mk5b_inputmode_type::mark5bdefault );
                DEBUG(1, "*** No prior 5B mode detected - setting default" << endl);
            } else {
                DEBUG(1, "*** detected existing 5B mode - taking it over" << endl);
                // looks like an existing mode is present in the h/w

                // we loose the exact tvg mode - there's about 7 of 'm!
                boardconfig.datasource = (boardconfig.tvgsel?"tvg":"ext");

                // clock frequency must be computed from ipm.k because
                // we can't read it out. 'k' gets programmed to 
                // "log2( freq ) - 1" so freq = 2^(k+1) MHz
                if( boardconfig.k>5 ) {
                    DEBUG(-1, "WARNING: found invalid 'K' value (" << boardconfig.k
                              << " which is >5) in existing mode. Changing it to 4 (==32MHz)");
                    boardconfig.k = 4;
                }
                // Expand 'k' value into frequency with a maximum of 32MHz
                boardconfig.clockfreq = 1000000 * std::min( 32, 2 << boardconfig.k );
            }

            // If running on 5B+, ask for FPDP2 by default
            if( ioboard.hardware() & ioboard_type::mk5b_plus_flag )
                boardconfig.fpdp2 = true;

            // And send the full configuration into the runtime - now
            // it has the default mk5b setup augmented with the actual
            // values from the I/O board
            rt0.set_input( boardconfig );
        } else if( ioboard.hardware()&ioboard_type::dom_flag ) {
            // DOM: set Mk5B default inputboard mode
            rt0.set_input( mk5bdom_inputmode_type(mk5bdom_inputmode_type::mark5bdefault) );
        } else if( !(ioboard.hardware().empty() || (ioboard.hardware()&ioboard_type::mk5c_flag)) ){
            DEBUG(0, "Not setting default input/output boardmode because\n"
                  << "  hardware " << ioboard.hardware() << " not supported (yet)" << endl;);
        }


        cout << "======= Hardware summary =======" << endl
             << "System: " << ioboard.hardware() << endl
             << endl;

        if( xlrdev ) {
             cout << xlrdev << endl;
             if ( (xlrdev.maxForkDataRate() < 1024000000) 
                  && do_buffering_mapping ) {
                 // warn the user that the maximum forking data rate
                 // is limited
                 cout << "Warning, this StreamStor card has a maximum data rate of " << endl
                      << (int)round(xlrdev.maxForkDataRate()/1e6)
                      << "Mbps for simultaneous recording and writing to memory." << endl;
                 cout << "When a higher data rate is requested for recording," << endl
                      << "jive5ab will automatically fall back to plain recording." << endl;
             }
        }
        else {
             cout << "No XLR device available" << endl;
        }
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
            streamstor_poll_args.stop   = false;
            streamstor_poll_args.rteptr = &rt0;
            streamstor_poll_thread = new pthread_t;
            PTHREAD2_CALL( ::pthread_create(streamstor_poll_thread, &tattr, streamstor_poll_fn, (void*)&streamstor_poll_args),
                           delete streamstor_poll_thread; streamstor_poll_thread = NULL; );

            PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );

        }

        // Depending on which hardware we found, we get the appropriate
        // commandmap
        ioboard_type::iobflags_type  hwflags = ioboard.hardware();
        if( hwflags&ioboard_type::mk5a_flag )
            rt0_mk5cmds = make_mk5a_commandmap( do_buffering_mapping );
        else if( hwflags&ioboard_type::dim_flag )
            rt0_mk5cmds = make_dim_commandmap( do_buffering_mapping );
        else if( hwflags&ioboard_type::dom_flag )
            rt0_mk5cmds = make_dom_commandmap();
        else if( hwflags&ioboard_type::mk5c_flag )
            rt0_mk5cmds = make_mk5c_commandmap( do_buffering_mapping, true );
        else if( (hwflags&ioboard_type::streamstor_flag || hwflags&ioboard_type::amazon_flag) )
            rt0_mk5cmds = make_mk5c_commandmap( do_buffering_mapping, false );
        else
            rt0_mk5cmds = make_generic_commandmap( do_buffering_mapping );

        // for the other runtimes we always use the generic command map
        generic_mk5cmds = make_generic_commandmap( do_buffering_mapping );


        // Goodie! Now set up for accepting incoming command-connections!
        // getsok() will throw if no socket can be created
        listensok = getsok( cmdport, "tcp" );

        // and get a socket on which to lissin for ROT broadcasts
        // HV: 17 may 2014 UDP port 7010 is the ROT clock
        //                 and it only works on Mk5A systems.
        if( hwflags & ioboard_type::mk5a_flag )
            rotsok = getsok( 7010, "udp" );

        // And get mk5read Unix or TCP socket or nuffink at all
        if( sfxc_lissen!=no_sfxc )
            sfxcsok = ((sfxc_lissen==lissen_tcp) ? getsok(sfxc_port, "tcp") : getsok_unix_server(sfxc_option));

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
            //    * 'sfxc' => listens for incoming SFXC DataReader connections
            //    * 'commandfds' => accepted commandclients send
            //      commands over these fd's and we reply to them
            //      over the same fd. 
            //    * 'sfxcfds' => accepted SFXC DataReader 
            //
            // 'cmdsockoffs' is the offset into the "struct pollfd fds[]"
            // variable at which the oridnary command connections start.
            // 'cmdsockoffs'+accedptedfds.size() is the extent of the normal
            // command socket. Above that are the SFXC/mk5read command socket(s)
            //
            const unsigned int           listenidx    = 0;
            const unsigned int           signalidx    = 1;
            const unsigned int           rotidx       = 2;
            const unsigned int           sfxcidx      = 3;
            const unsigned int           cmdsockoffs  = 4;
            // we need to fix those values here because the
            // acceptedfds/acceptedsfxcfds may change size below - e.g. if
            // clients made a connection. But those (new) fd's won't be in
            // the current list of fd's
            const unsigned int           n_jive5ab    = acceptedfds.size();
            const unsigned int           n_sfxc       = acceptedsfxcfds.size(); 
            const unsigned int           nrfds        = 4 + n_jive5ab/*acceptedfds.size()*/ + n_sfxc/*acceptedsfxcfds.size()*/;
            const unsigned int           nrlistenfd   = 2;
            const unsigned int           listenfds[2] = {listenidx, sfxcidx};
            char const * const           names[2]     = {"jive5ab", "sfxc"};
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
            fds[rotidx].events     = (rotsok>=0 ? POLLIN|POLLPRI|POLLERR|POLLHUP : 0);

            // Position 'sfxcidx' is used for the socket on which we listen
            // for incoming SFXC data reader connections
            fds[sfxcidx].fd        = sfxcsok;
            fds[sfxcidx].events    = POLLIN|POLLPRI|POLLERR|POLLHUP;

            // Loop over the accepted connections
            for(idx=cmdsockoffs, curfd=acceptedfds.begin();
                curfd!=acceptedfds.end(); idx++, curfd++ ) {
                fds[idx].fd     = curfd->first;
                fds[idx].events = POLLIN|POLLPRI|POLLERR|POLLHUP;
            }
            // And append the accepted SFXC client connections
            for(idx=cmdsockoffs+acceptedfds.size(), curfd=acceptedsfxcfds.begin();
                curfd!=acceptedsfxcfds.end(); idx++, curfd++ ) {
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
                if( events&POLLIN ) {
                    environment_values.resize( runtimes.size() );
                    transform( runtimes.begin(), runtimes.end(), 
                               environment_values.begin(), get_runtimeptr );
                    process_rot_broadcast( fds[rotidx].fd, 
                                           environment_values.begin(),
                                           environment_values.end() );
                }
                if( events&POLLHUP || events&POLLERR )
                    DEBUG(0, "ROT-broadcast sokkit is geb0rkt. Therfore delayedplay != werk." << endl);
            }

            // Check the signalsokkit -> if something happened there, we surely
            // MUST break from our loop
            if( (events=fds[signalidx].revents)!=0 ) {
                cerr << "main: stopping [" << eventor(events) << "] - ";
                if( events&POLLIN ) {
                    int    s( 0 );

                    if( ::read( fds[signalidx].fd, &s, sizeof(s) )==sizeof(s) ) {
                        if( s>=0 )
                            cerr << " because of SIG#" << s;
                        else
                            cerr << " because of error in signalthread_fn";
                    } else {
                        cerr << " (fail to read signal value from signalsocket)";
                    }
                }
                cerr << endl;
                delete [] fds;
                break;
            }

            // check for new incoming connections
            for(unsigned int itmp=0; itmp<nrlistenfd; itmp++) {
                const unsigned int fd_idx = listenfds[itmp];

                // If nothing happened or we polled an invalid fd [e.g. no sfxc lissener], we're done quickly
                if( (events=fds[fd_idx].revents)==0 || (events&POLLNVAL))
                    continue;
                // Ok revents not zero: something happened!
                DEBUG(5, "listensok[" << names[itmp] << "] got " << eventor(events) << endl);

                // If the socket's been hung up (can that happen?)
                // we just stop processing commands
                if( events&POLLHUP || events&POLLERR ) {
                    cerr << "main: detected hangup of " << names[itmp] << "socket." << endl;
                    delete [] fds;
                    break;
                }
                if( events&POLLIN ) {
                    // do_accept_incoming may throw but we don't want to shut
                    // down the whole app upon failure of *that*
                    try {
                        fdprops_type::value_type(*acceptor)(int)  = (fd_idx==sfxcidx && sfxc_port==(unsigned short)-1) ?
                                                                     do_accept_incoming_ux : do_accept_incoming;
                        fdprops_type&                     fdp( (fd_idx==sfxcidx) ? acceptedsfxcfds : acceptedfds );
                        fdprops_type::value_type          fd( acceptor(fds[ fd_idx ].fd) );
                        pair<fdprops_type::iterator,bool> insres;

                        // Show what's up
                        DEBUG(5, "incoming " << names[itmp] << " on fd#" << fd.first << " " << fd.second << endl);

                        // And add it to the vector of filedescriptors to watch [take care of which list to put it in]
                        insres = fdp.insert( fd );
                        if( !insres.second ) {
                            cerr << "main: failed to insert entry into " 
                                 << ((fd_idx == listenidx) ? "acceptedfds" : "acceptedsfxcfds") << " -\n"
                                 << "      connection from " << fd.second << "!?";
                            ::close( fd.first );
                            cerr << "Currently in map:" << endl;
                            for(fdprops_type::const_iterator curfdp=fdp.begin(); curfdp!=fdp.end(); curfdp++)
                                cerr << "   fd#" << curfdp->first << " = " << curfdp->second << endl;
                            delete [] fds;
                            break;
                        } else {
                            // When connection is accepted, you're talking
                            // to the default runtime, so do that
                            // bookkeeping here. This only applies to jive5ab clients
                            if( fd_idx==listenidx ) {
                                pair<fdmap_type::iterator, bool> fdminsres = fdmap.insert( make_pair(fd.first, per_fd_data(echo)) );

                                EZASSERT2(fdminsres.second==true, bookkeeping,
                                          EZINFO("accepting fd: an entry for fd#" << fd.first << " already in map?!"));
                                ::observe(default_runtime, fdminsres.first, runtimes);
                            }
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
                // Done dealing with the listening sockets
            }

            // On all other sockets, loox0r for commands!
            // NOTE: here we must not use 'acceptedfds.size()' because we
            //       may have accepted a new client; the 'fds[...]' array
            //       only contains entries for fd's that were already 'active'
            for( idx=cmdsockoffs; idx<nrfds; idx++ ) {
                // If no events, nothing to do!
                if( (events=fds[idx].revents)==0 )
                    continue;

                // only now it makes sense to Do Stuff!
                int                     fd( fds[idx].fd );
                const bool              is_sfxc( idx>=(cmdsockoffs+n_jive5ab) );
                fdmap_type::iterator    fdmptr = fdmap.find( fd );
                fdprops_type&           fdprops( is_sfxc ? acceptedsfxcfds : acceptedfds );
                fdprops_type::iterator  fdptr  = fdprops.find(fd);

                DEBUG(5, "fd#" << fd << " got " << eventor(events) << endl);

                // If fdmptr == fdm.end() this means that the fd did not get
                // added to the "fd" -> "echo/runtime" mapping correctly ...
                // This does not apply to SFXC clients
                if(  (!is_sfxc && fdmptr==fdmap.end()) || fdptr==fdprops.end() ) {
                    ::close( fd );

                    cerr << "main: internal error. fd#" << fd << " is in pollfds \n";
                    // If it wasn't in fdmap, there's no point in
                    // unobserving. Also: don't even attempt to unobserve sfxc 
                    if( fdmptr==fdmap.end() )
                        cerr << "       but not in fdmap" << endl;
                    else if( !is_sfxc )
                        ::unobserve(fd, fdmap, runtimes);
                    // Only erase the fd if it was in acceptedfds
                    if( fdptr==fdprops.end() )
                        cerr << "       but not in acceptedfds" << endl;
                    else
                        fdprops.erase( fdptr );
                    continue;
                }
                // Both fdptr and fdmptr are valid, which is comforting to
                // know! Or it was an SFXC client ...

                // if error occurred or hung up: close fd and remove from
                // list of fd's to monitor
                if( events&POLLHUP || events&POLLERR ) {
                    DEBUG(4, "detected HUP/ERR on fd#" << fd << " [" << fdptr->second << "]" << endl);
                    ::close( fd );

                    // It is no more in the accepted fd's
                    fdprops.erase( fdptr );

                    // Make sure it is not referenced anywhere
                    if( !is_sfxc )
                        ::unobserve(fd, fdmap, runtimes);

                    // Move on to checking next FD
                    continue;
                }


                // if stuff may be read, see what we can make of it
                if( events&POLLIN ) {
                    char                           linebuf[4096];
                    ssize_t                        nread, nwrite;

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
                        // Not part of accepted fd's anymore
                        fdprops.erase( fdptr );
                        // Make sure it is not referenced anywhere
                        if( !is_sfxc )
                            ::unobserve(fd, fdmap, runtimes);
                        continue;
                    }

                    // Handle sfxc client differently
                    if( is_sfxc ) {
                        bool something_went_wrong = false;
                        try {
                            EZASSERT2(nread==sizeof(mk5read_msg), mk5read_exception, 
                                      EZINFO("Binary command size mismatch: expect " << sizeof(mk5read_msg) << ", got " << nread));
                            // mk5read clients always execcute in runtime 0
                            attempt_stream_to_sfxc(fdptr->first, (mk5read_msg*)linebuf, rt0);
                        }
                        catch( std::exception const& e ) {
                            DEBUG(-1, "main/incoming mk5read_msg: " << e.what() << endl);
                            something_went_wrong = true; 
                        }
                        catch( ... ) {
                            DEBUG(-1, "main/incoming mk5read_msg: caught unknown exception." << endl);
                            something_went_wrong = true; 
                        }
                        // Once a command's been received on an accepted
                        // sfxc client sokkit we stop monitoring it
                        fdprops.erase( fdptr );
                        // Clean up if fishy
                        if( something_went_wrong ) {
                            ::close( fdptr->first );
                        }
                        // Done. Do not attempt any further command processing
                        continue;
                    }

                    // This is not SFXC client, must be a jive5ab client.
                    // Thus: make sure line is null-byte terminated
                    linebuf[ nread ] = '\0';

                    // And we need sanitizing variables ...
                    char*                          sptr;
                    char*                          eptr;
                    string                         reply;
                    vector<string>                 commands;
                    vector<string>::const_iterator curcmd;

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
                            << ((commands.size()==1)?(""):("s")) << endl );
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
                        DEBUG((fdmptr->second.echo?2:10000), "Processing command '" << cmd << "'" << endl);

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
                        try {
                            if( keyword == "runtime" ) {
                                // select a runtime to pass to the functions
                                reply += process_runtime_command( qry, args, fdmptr, runtimes);
                            } else if( keyword=="echo" ) {
                                // turn command echoing on or off
                                if( qry ) {
                                    ostringstream tmp;
                                    tmp << "!echo? 0 : " << (fdmptr->second.echo?"on":"off") << " ;";
                                    reply += tmp.str();
                                } else if( args.size()!=2 || !(args[1]=="on" || args[1]=="off") ) {
                                    reply += string("!echo= 8 : expects exactly one parameter 'on' or 'off';") ;
                                } else {
                                    // already verified we have 'on' or 'off'
                                    fdmptr->second.echo = (args[1]=="on");
                                    reply += string("!echo= 0 ;");
                                }
                            } else {
                                mk5commandmap_type& mk5cmds = ( fdmptr->second.runtime==default_runtime ? rt0_mk5cmds : generic_mk5cmds );
                                if( (cmdptr=mk5cmds.find(keyword))==mk5cmds.end() ) {
                                    reply += (string("!")+keyword+((qry)?('?'):('='))+" 7 : ENOSYS - not implemented ;");
                                    continue;
                                }
                                
                                // Check if the runtime we are observing is
                                // still the one when we started observing
                                runtimemap_type::iterator   rt_iter = current_runtime(fdmptr, runtimes);

                                if ( rt_iter == runtimes.end() ) {
                                    reply += string("!")+keyword+" " + QRY(qry) + " 4 : current runtime ('" + fdmptr->second.runtime + "') has been deleted;";
                                }
                                else {
                                    runtime* rteptr = rt_iter->second.rteptr;

                                    try {
                                        reply += cmdptr->second(qry, args, *rteptr);
                                    } 
                                    catch (...) {
                                        // do the protect=off bookkeeping
                                        rteptr->protected_count = max(rteptr->protected_count, 1u) - 1;
                                        throw;
                                    }
                                    // do the protect=off bookkeeping
                                    rteptr->protected_count = max(rteptr->protected_count, 1u) - 1;
                                }
                            }
                        }
                        catch( const Error_Code_6_Exception& e) {
                            reply += string("!")+keyword+" " + QRY(qry) + " 6 : " + e.what() + ";";
                        }
                        catch( const Error_Code_8_Exception& e) {
                            reply += string("!")+keyword+" " + QRY(qry) + " 8 : " + e.what() + ";";
                        }
                        catch( const cmdexception& e ) {
                            reply += string("!")+keyword+" " + QRY(qry) + " 6 : " + e.what() + ";";
                        }
                        catch( const exception& e ) {
                            reply += string("!")+keyword+" " + QRY(qry) + " 4 : " + e.what() + ";";
                        }
                        catch( ... ) {
                            reply += string("!")+keyword+" " + QRY(qry) + " 4 : unknown exception ;";
                        }
                    }

                    if( reply.empty() ) {
                        DEBUG(4, "No command(s) found, no reply sent" << endl);
                        break;
                    }
                    // processed all commands in the string. send the reply
                    DEBUG((fdmptr->second.echo?2:10000), "Reply: " << reply << endl);
                    // do *not* forget the \r\n ...!
                    // HV: 18-nov-2011 see above near 'const bool crlf =...';
                    if( crlf )
                        reply += "\r\n";
                    else
                        reply += "\n";

                    nwrite = ::write(fd, reply.c_str(), reply.size());
                    // if <=0, socket was closed, remove it from the list
                    if( nwrite<=0 ) {
                        if( nwrite<0 ) {
                            lastsyserror_type lse;
                            DEBUG(0, "Error on fd#" << fdptr->first << " ["
                                  << fdptr->second << "] - " << lse << endl);
                        }
                        ::close( fdptr->first );
                        ::unobserve(fdptr->first, fdmap, runtimes);
                        fdprops.erase( fdptr );
                    }
                }
                // done with this fd
            }
            // done all fds
            delete [] fds;
        }
        DEBUG(2, "closing listening socket (ending program)" << endl);
        ::close( listensok );
        ::close( sfxcsok   );

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
                 << evlbi5a::strerror(rv) << endl;
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
    for ( runtimemap_type::iterator rt_iter = runtimes.begin();
          rt_iter != runtimes.end();
          rt_iter++ ) {
        delete rt_iter->second.rteptr;
    }

    // Unlink the unix domain socket - if necessary 
    if( sfxc_lissen==lissen_unix )
        ::unlink(sfxc_option.c_str());
    return 0;
}
