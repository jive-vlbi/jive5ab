// implementation of the commands
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
//
// * generic Mk5 commands [Mk5 hardware agnostic]
// * generic jive5a commands [ipd, pdr, tstat, mtu, ...]
// * specializations for
//      - Mk5A
//      - Mk5B flavour agnostic but Mk5B specific
//      - Mk5B/DIM
//      - Mk5B/DOM
// * commandmaps which define which of the commands
//   are allowed for which Mk5 flavour.
//   Currently there's 3 commandmaps:
//      - Mk5A
//      - Mk5B/DIM
//      - Mk5B/DOM
// * Utility functions for Mk5's
//   (eg programming Mk5B/DIM input section for recording:
//    is shared between dim2net and in2disk)
#include <mk5command.h>
#include <dosyscall.h>
#include <threadfns.h>
#include <playpointer.h>
#include <evlbidebug.h>
#include <getsok.h>
#include <streamutil.h>
#include <userdir.h>
#include <busywait.h>

// c++ stuff
#include <map>

// for setsockopt
#include <sys/types.h>
#include <sys/socket.h>

// and for "struct timeb"/ftime()
#include <sys/timeb.h>
#include <sys/time.h>
#include <time.h>
// for log/exp/floor
#include <math.h>
// zignal ztuv
#include <signal.h>

// inet functions
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

// returns the value of s[n] provided that:
//  s.size() > n
//  s[n].empty()==false
// otherwise returns the empty string
#define OPTARG(n, s) \
    (s.size()>n && !s[n].empty())?s[n]:string()

// function prototype for fn that programs & starts the
// Mk5B/DIM disk-frame-header-generator at the next
// available moment.
void start_mk5b_dfhg( runtime& rte, double maxsyncwait = 3.0 );



// "implementation" of the cmdexception
cmdexception::cmdexception( const string& m ):
    __msg( m )
{}

const char* cmdexception::what( void ) const throw() {
    return __msg.c_str();
}
cmdexception::~cmdexception() throw()
{}








//
//
//   The Mark5 commands
//
//



string disk2net_fn( bool qry, const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    // store the socket in here.
    // if we create it but cannot start the threads (or something else
    // goes wrong) then the cleanup code at the end will close the socket.
    // *if* the threads are succesfully started make sure you
    // reset this value to <0 to prevent your socket from being
    // closed 
    int              s = -1;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing disk2net - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=disk2net ) {
        reply << " 1 : _something_ is happening and its NOT disk2net!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.lasthost << " : "
                  << rte.transfersubmode
                  << " : " << rte.pp_current;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        if( args[1]=="connect" ) {
            recognized = true;
            // if transfermode is already disk2net, we ARE already connected
            // (only disk2net::disconnect clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                int          sbuf( rte.netparms.sndbufsize );
                bool         initstartval;
                string       proto( rte.netparms.get_protocol() );
                const bool   rtcp( proto=="rtcp" );
                unsigned int olen( sizeof(rte.netparms.sndbufsize) );

                // make sure we recognize the protocol
                ASSERT_COND( ((proto=="udp")||(proto=="tcp")||(proto=="rtcp")) );

                // good. pick up optional hostname/ip to connect to
                if( !rtcp && args.size()>2 )
                    rte.lasthost = args[2];

                // create socket and connect 
                // if rtcp, create a listening socket instead
                if( rtcp )
                    s = getsok(2630, "tcp");
                else
                    s = getsok(rte.lasthost, 2630, proto);

                // Set sendbufsize
                ASSERT_ZERO( ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sbuf, olen) );

                // before kicking off the threads, transfer some important variables
                // across. The playpointers will be done later on
                initstartval = !rtcp;
                if( rtcp ) {
                    rte.fd       = -1;
                    rte.acceptfd = s;
                } else {
                    rte.fd       = s;
                    rte.acceptfd = -1;
                }

                rte.queue.enable( rte.netparms.nblock );

                // goodie. now start the threads disk2mem and mem2net!
                // start_threads() will throw up if something's fishy
                if( proto=="udp" )
                    rte.start_threads(disk2mem, mem2net_udp);
                else
                    rte.start_threads(disk2mem, mem2net_tcp);

                // Make sure the local temporaries get reset to 0 (or -1)
                // to prevent deleting/foreclosure
                s             = -1;

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = disk2net;
                rte.transfersubmode.clr_all();
                // we are connected and waiting for go
                if( !rtcp )
                    rte.transfersubmode |= connected_flag;
                rte.transfersubmode |= wait_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }

        // <on> : turn on dataflow
        if( args[1]=="on" ) {
            recognized = true;
            // only allow if transfermode==disk2net && submode hasn't got the running flag
            // set AND it has the connectedflag set
            if( rte.transfermode==disk2net && rte.transfersubmode&connected_flag
                && (rte.transfersubmode&run_flag)==false ) {
                bool               repeat = false;
                playpointer        pp_s;
                playpointer        pp_e;
                unsigned long long nbyte;

                // Pick up optional extra arguments:
                // note: we do not support "scan_set" yet so
                //       the part in the doc where it sais
                //       that, when omitted, they refer to
                //       current scan start/end.. that no werk

                // start-byte #
                if( args.size()>2 && !args[2].empty() ) {
                    unsigned long long v;

                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    // if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[2].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("start-byte# is out-of-range");
                    pp_s.Addr = v;
                }
                // end-byte #
                // if prefixed by "+" this means: "end = start + <this value>"
                // rather than "end = <this value>"
                if( args.size()>3 && !args[3].empty() ) {
                    unsigned long long v;
                   
                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    //if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[3].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("end-byte# is out-of-range");
                    if( args[3][0]=='+' )
                        pp_e.Addr = pp_s.Addr + v;
                    else
                        pp_e.Addr = v;
                    ASSERT2_COND(pp_e>pp_s, SCINFO("end-byte-number should be > start-byte-number"));
                }
                // repeat
                if( args.size()>4 && !args[4].empty() ) {
                    long int    v = ::strtol(args[4].c_str(), 0, 0);

                    if( (v==LONG_MIN || v==LONG_MAX) && errno==ERANGE )
                        throw xlrexception("value for repeat is out-of-range");
                    repeat = (v!=0);
                }
                // now compute "real" start and end, if any
                // so the threads, when kicked off, don't have to
                // think but can just *go*!
                if( pp_e.Addr<=pp_s.Addr ) {
                    S_DIR       currec;
                    playpointer curlength;
                    // end <= start => either end not specified or
                    // neither start,end specified. Find length of recording
                    // and play *that*, starting at startbyte#
                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &currec) );
                    curlength = currec.Length;

                    // check validity of start,end
                    if( pp_s>=curlength ||  pp_e>=curlength ) {
                        ostringstream  err;
                        err << "start and/or end byte# out-of-range, curlength=" << curlength;
                        throw xlrexception( err.str() );
                    }
                    // if no end given: set it to the end of the current recording
                    if( pp_e==playpointer(0) )
                        pp_e = curlength;
                }
                // make sure the amount to play is an integral multiple of
                // blocksize
                nbyte = pp_e.Addr - pp_s.Addr;
                DEBUG(1,"start/end [nbyte]=" << pp_s << "/" << pp_e << " [" << nbyte << "]" << endl);
                nbyte = nbyte/rte.netparms.get_blocksize() * rte.netparms.get_blocksize();
                if( nbyte<rte.netparms.get_blocksize() )
                    throw xlrexception("less than <blocksize> bytes selected to play. no can do");
                pp_e = pp_s.Addr + nbyte;
                DEBUG(1,"Made it: start/end [nbyte]=" << pp_s << "/" << pp_e << " [" << nbyte << "]" << endl);
                
                // After we've acquired the mutex, we may set the "real"
                // variable (start) to true, then broadcast the condition.
                PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );
                
                // ok, now transfer options to the threadargs structure
                rte.pp_end   = pp_e;
                rte.repeat   = repeat;
                rte.pp_start = pp_s;
                // do this last such that a spurious wakeup will not
                // make a thread start whilst we're still filling in
                // the arguments!
                rte.run      = true;
                // indicate running state - note: setting the run_flag
                // is what will actually kick the disk2mem thread off
                // not the 'rte.run' value [if doing rtcp the rte.run is
                // set when a connection is made but the thread must
                // really wait for *us* [ie disk2net=on] to actually
                // start transferring.
                rte.transfersubmode.clr( wait_flag ).set( run_flag );
                // now broadcast the startcondition
                PTHREAD2_CALL( ::pthread_cond_broadcast(rte.condition),
                               ::pthread_mutex_unlock(rte.mutex) );

                // And we're done, we may release the mutex
                PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or disk2net, nothing else
                if( rte.transfermode==disk2net ) {
                    if( rte.transfersubmode&connected_flag )
                        reply << " 6 : already running ;";
                    else
                        reply << " 6 : not connected yet ;";
                } else 
                    reply << " 6 : not doing anything ;";
            }
        }

        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing disk2net.
            // Don't care if we were running or not
            if( rte.transfermode==disk2net ) {
                // let the runtime stop the threads
                rte.stop_threads();

                // destroy allocated resources
                if( rte.fd>=0 )
                    ::close( rte.fd );
                // reset global transfermode variables 
                rte.fd           = -1;
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing disk2net ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    // If any of the temporaries is non-default, clean up
    // whatever it was
    if( s>=0 )
        ::close(s);
    return reply.str();
}

// disk2out (alias for 'play')
// should work on both Mark5a and Mark5b/dom
typedef std::map<runtime*, pthread_t> threadmap_type;

string disk2out_fn(bool qry, const vector<string>& args, runtime& rte) {
    // keep a mapping of runtime -> delayed_play thread such that we
    // can cancel it if necessary
    static threadmap_type delay_play_map;
    // automatic variables
    ostringstream    reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing disk2out - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=disk2out ) {
        reply << " 1 : _something_ is happening and its NOT disk2out(play)!!! ;";
        return reply.str();
    }

    // Good, if query, tell'm our status
    if( qry ) {
        // we do not implement 'arm' so we can only be in one of three states:
        // waiting, off/inactive, on
        if( rte.transfermode==disk2out ) {
            // depending on 'wait' status (implies delayed play) indicate that
            if( rte.transfersubmode&wait_flag )
                reply << " 1 : waiting ;";
            else
                reply << " 0 : on ;";
        } else {
            reply << " 0 : off ;";
        }
        return reply.str();
    }

    // Handle command, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <on>[:<playpointer>[:<ROT>]]
        if( args[1]=="on" ) {
            recognized = true;
            // If ROT is given, then the playback will start at
            // that ROT for the given taskid [aka 'delayed play'].
            // If no taskid set or no rot-to-systemtime mapping
            // known for that taskid we FAIL.
            if( rte.transfermode==no_transfer ) {
                double                     rot( 0.0 );
                SSHANDLE                   ss( rte.xlrdev.sshandle() );
                playpointer                startpp;

                // Playpointer given?
                if( args.size()>2 && !args[2].empty() ) {
                    unsigned long long v;

                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    // if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[2].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("start-byte# is out-of-range");
                    startpp.Addr = v;
                }
                // ROT given? (if yes AND >0.0 => delayed play)
                if( args.size()>3 && !args[3].empty() ) {
                    threadmap_type::iterator   thrdmapptr;
//                    task2rotmap_type::iterator rotmapptr;

                    rot = ::strtod( args[3].c_str(), 0 );

                    // only allow if >0.0 AND taskid!=invalid_taskid
                    ASSERT_COND( (rot>0.0 && rte.current_taskid!=runtime::invalid_taskid) );
#if 0
                    // need to ascertain ourselves that a ROT->systemtime mapping
                    // exists for the current taskid
                    rotmapptr = rte.task2rotmap.find( rte.current_taskid );
                    ASSERT2_COND( (rotmapptr!=rte.task2rotmap.end()),
                                  SCINFO("No ROT->systemtime mapping found for task "
                                         << rte.current_taskid) );
#endif
                    // And there should NOT already be a delayed-play entry for
                    // the current 'runtime'
                    thrdmapptr = delay_play_map.find( &rte );
                    ASSERT2_COND( (thrdmapptr==delay_play_map.end()),
                                  SCINFO("Internal error: an entry for the current rte "
                                         "already exists in the delay-play-map.") );
                }

                // Good - independant of delayed or immediate play, we have to set up
                // the Streamstor device the same.
                XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRBindInputChannel(ss, 0) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );

                // so far so good - transfer desired playback-location 
                rte.pp_start = startpp;

                // we create the thread always - an immediate play
                // command is a delayed-play with a delay of zero ...
                // afterwards we do bookkeeping.
                sigset_t       oss, nss;
                pthread_t      dplayid;
                dplay_args     thrdargs;
                pthread_attr_t tattr;

                // prepare the threadargument
                thrdargs.rot    = rot;
                thrdargs.rteptr = &rte;

                // set up for a detached thread with ALL signals blocked
                ASSERT_ZERO( ::sigfillset(&nss) );
                PTHREAD_CALL( ::pthread_attr_init(&tattr) );
                PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE) );
                PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &nss, &oss) );
                PTHREAD_CALL( ::pthread_create(&dplayid, &tattr, delayed_play_fn, &thrdargs) );
                // good. put back old sigmask + clean up resources
                PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &oss, 0) );
                PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );

                // save the threadid in the mapping.
                // play=off will clean it
                std::pair<threadmap_type::iterator, bool> insres;
                insres = delay_play_map.insert( make_pair(&rte, dplayid) );
                ASSERT2_COND(insres.second==true, SCINFO("Failed to insert threadid into map?!"));

                // Update running status:
                rte.transfermode = disk2out;
                rte.transfersubmode.clr_all();

                // deping on immediate or delayed playing:
                rte.transfersubmode.set( (rot>0.0)?(wait_flag):(run_flag) );

                // and form response [if delayed play => return code is '1'
                // i.s.o. '0']
                reply << " " << ((rot>0.0)?1:0) << " ;";
            } else {
                // already doing it!
                reply << " 6 : already ";
                if( rte.transfersubmode&wait_flag )
                    reply << " waiting ";
                else
                    reply << " playing ";
                reply << ";";
            }
        }
        //  play=off
        //  cancels delayed play/stops playback
        if( args[1]=="off" ) {
            recognized = true;
            if( rte.transfermode==disk2out ) {
                SSHANDLE                 sshandle( rte.xlrdev.sshandle() );
                threadmap_type::iterator thrdmapptr;

                // okiedokie, cancel & join the thread (if any)
                thrdmapptr = delay_play_map.find( &rte );
                if( thrdmapptr!=delay_play_map.end() ) {
                    // check if thread still there and cancel it if yes.
                    // NOTE: no auto-throwing on error as the
                    // thread may already have gone away.
                    if( ::pthread_kill(thrdmapptr->second, 0)==0 )
                        ::pthread_cancel(thrdmapptr->second);
                    // now join the thread
                    PTHREAD_CALL( ::pthread_join(thrdmapptr->second, 0) );

                    // and remove the current dplay_map entry
                    delay_play_map.erase( thrdmapptr );
                }
                // somehow we must call stop twice if the
                // device is actually playing
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(sshandle) );
                XLRCALL( ::XLRStop(sshandle) );

                // return to idle status
                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;
                reply << " 0 ;";
            } else {
                // nothing to stop!
                reply << " 4 : inactive ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }

    return reply.str();
}

// set/query the taskid
string task_id_fn(bool qry, const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        const unsigned int tid = rte.current_taskid;

        reply << " 0 : ";
        if( tid==runtime::invalid_taskid )
            reply << "none";
        else 
            reply << tid;
        reply << " ;";
        return reply.str();
    }

    // check if argument given and if we're not doing anything
    if( args.size()<2 ) {
        reply << " 8 : no taskid given ;";
        return reply.str();
    }

    if( rte.transfermode!=no_transfer ) {
        reply << " 6 : cannot set/change taskid during " << rte.transfermode << " ;";
        return reply.str();
    }

    // Gr8! now we can get the actual taskid
    rte.current_taskid = (unsigned int)::strtol(args[1].c_str(), 0, 0);
    reply << " 0 ;";

    return reply.str();
}


// set up net2out and net2disk
string net2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // This points to the scan being recorded, if any
    static ScanPointer      scanptr;    
    static UserDirectory    ud;

    // automatic variables
    bool                atm; // acceptable transfer mode
    const bool          disk( args[0]=="net2disk" );
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // store the socket in here.
    // if we create it but cannot start the threads (or something else
    // goes wrong) then the cleanup code at the end will close the socket.
    // *if* the threads are succesfully started make sure you
    // reset this value to <0 to prevent your socket from being
    // closed 
    int              s = -1;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer ||
           (disk && ctm==net2disk) ||
           (!disk && ctm==net2out));

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            if( rte.transfersubmode&run_flag )
                reply << "active";
            else if( rte.transfersubmode&wait_flag )
                reply << "waiting";
            else
                reply << rte.transfersubmode;
            reply << " : " << rte.nbyte_from_mem;
        }
        // this displays the flags that are set, in HRF
        //reply << " : " << rte.transfersubmode;
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <open>
        if( args[1]=="open" ) {
            recognized = true;
            // if transfermode is already net2out, we ARE already doing this
            // (only net2out::close clears the mode to doing nothing)
            // Supports 'rtcp' now [reverse tcp: receiver initiates connection
            // rather than sender, which is the default. Usefull for bypassing
            // firewalls enzow].
            // Multicast is detected by the condition:
            //   rte.lasthost == of the multicast persuasion.
            //
            // Note: net2out=open supports an optional argument: the ipnr.
            // Which is either the host to connect to (if rtcp) or a 
            // multicast ip-address which will be joined.
            //
            // net2out=open;        // sets up receiving socket based on net_protocol
            // net2out=open:<ipnr>; // implies either 'rtcp' if net_proto==rtcp,
            // connects to <ipnr>. If netproto!=rtcp, sets up receiving socket to
            // join multicast group <ipnr>.
            //
            // net2disk MUST have a scanname and may have an optional
            // ipaddress for rtcp or connecting to a multicast group:
            // net2disk=open:<scanname>[:<ipnr>]
            if( rte.transfermode==no_transfer ) {
                bool             initstartval;
                SSHANDLE         ss( rte.xlrdev.sshandle() );
                const string&    proto( rte.netparms.get_protocol() );
                const bool       rtcp( proto=="rtcp" );
                struct in_addr   multicast;
                transfer_submode tsm;

                // for now, only accept tcp, udp and rtcp
                ASSERT_COND( ((proto=="udp")||(proto=="tcp")||(proto=="rtcp")) );

                // depending on disk or out, the 2nd arg is optional or not
                if( disk && (args.size()<3 || args[2].empty()) )
                    THROW_EZEXCEPT(cmdexception, " no scanname given");

                // pick up optional ip-address, if given.
                if( (!disk && args.size()>2) || (disk && args.size()>3) )
                    rte.lasthost = args[(disk?3:2)];

                // also, if writing to disk, we should ascertain that
                // the disks are ready-to-go
                if( disk ) {
                    S_DIR         disk;
                    SSHANDLE      ss( rte.xlrdev.sshandle() );
                    S_DEVINFO     devInfo;

                    // Verify that there are disks on which we *can*
                    // record!
                    XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                    ASSERT_COND( devInfo.NumDrives>0 );

                    // and they're not full or writeprotected
                    XLRCALL( ::XLRGetDirectory(ss, &disk) );
                    ASSERT_COND( !(disk.Full || disk.WriteProtected) );
                }

                // if proto!=rtcp and a hostname is given
                // and it is a multicast-address, switch on
                // multicasting!
                multicast.s_addr = INADDR_ANY;
                if( !rtcp && !rte.lasthost.empty() ) {
                    struct in_addr   ip;

                    // go from ascii -> inet_addr
                    inet_aton( rte.lasthost.c_str(), &ip );
                    // and test if itza multicast addr. If so,
                    // transfer the address to the multicast-ipaddr
                    // d'oh! inet_aton() returns the bytes in host-order
                    if( IN_MULTICAST(ntohl(ip.s_addr)) )
                        multicast = ip;
                }

                // create socket and start listening.
                // If netparms.proto==tcp we put socket into the
                // rte's acceptfd field so it will do the waiting
                // for connect for us (and kick off the threads
                // as soon as somebody make a conn.)
                // If we're doing rtcp, we should be a client
                // and hence attempt to open the connection.
                if( rtcp )
                    s = getsok(rte.lasthost, 2630, "tcp");
                else
                    s = getsok(2630, proto);


                // switch on recordclock, not necessary for recording
                // to disk
                if( !disk )
                    rte.ioboard[ mk5areg::notClock ] = 0;

                // now program the streamstor to record from PCI -> FPDP
                XLRCALL( ::XLRSetMode(ss, (disk?SS_MODE_SINGLE_CHANNEL:SS_MODE_PASSTHRU)) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

                // program where the output should go
                if( disk ) {
                    // must prepare the userdir
                    ud  = UserDirectory( rte.xlrdev );
                    ScanDir& scandir( ud.scanDir() );

                    scanptr = scandir.getNextScan();

                    // new recording starts at end of current recording
                    // note: we have already ascertained that:
                    // disk => args[2] exists && !args[2].empy()
                    scanptr.name( args[2] );
                    scanptr.start( ::XLRGetLength(ss) );
                    scanptr.length( 0ULL );

                    // write the userdirectory
                    ud.write( rte.xlrdev );

                    // and start the recording
                    XLRCALL( ::XLRAppend(ss) );
                } else {
                    XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                    XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                    XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
                    XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );
                }

                // If multicast detected, join the group and
                // throw up if it fails. By the looks of the docs
                // we do not have to do a lot more than a group-join.
                // The other options are irrelevant for us.
                if( multicast.s_addr!=INADDR_ANY ) {
                    struct ip_mreq   mcjoin;

                    // interested in MC traffik on any interface
                    mcjoin.imr_multiaddr        = multicast;
                    mcjoin.imr_interface.s_addr = INADDR_ANY;
                    ASSERT_ZERO( ::setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                              &mcjoin, sizeof(mcjoin)) );
                    // done!
                }
                // before kicking off the threads,
                // transfer some important variables across
                // and pre-set the submode. Will only be
                // set in the runtime IF the threads actually
                // seem to start ok. Otherwise the runtime
                // is left untouched
                initstartval = false;
                if( proto=="udp" || rtcp ) {
                    rte.fd       = s;
                    rte.acceptfd = -1;
                    // udp threads and rtcp don't have to "wait-for-start"
                    // they're already 'connected'.
                    initstartval = true;
                    tsm.set( connected_flag ).set( run_flag );
                } else  {
                    // tcp - wait for incoming
                    rte.fd       = -1;
                    rte.acceptfd = s;
                    tsm.set( wait_flag );
                }

                // enable the queue
                rte.queue.enable( rte.netparms.nblock );

                // goodie. now start the threads net2mem and mem2streamstor!
                // start_threads() will throw up if something's fishy
                // NOTE: we should change the net2mem policy based on
                // reliable or lossy transport.
                rte.start_threads(net2mem, mem2streamstor, initstartval);

                // Make sure the local temporaries get reset to 0 (or -1)
                // to prevent deleting/foreclosure
                s             = -1;

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = (disk?net2disk:net2out);
                rte.transfersubmode = tsm;
                // depending on the protocol...
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        // <close>
        if( args[1]=="close" ) {
            recognized = true;

            // only accept this command if we're active
            // ['atm', acceptable transfermode has already been ascertained]
            if( rte.transfermode!=no_transfer ) {
                // switch off recordclock (if not disk)
                if( !disk )
                    rte.ioboard[ mk5areg::notClock ] = 1;

                // Ok. stop the threads
                rte.stop_threads();

                // close the socket(s)
                if( rte.fd )
                    ::close( rte.fd );
                if( rte.acceptfd )
                    ::close( rte.acceptfd );
                // reset the 'state' variables
                rte.fd       = -1;
                rte.acceptfd = -1;
                // And tell the streamstor to stop recording
                // Note: since we call XLRecord() we MUST call
                //       XLRStop() twice, once to stop recording
                //       and once to, well, generically stop
                //       whatever it's doing
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                // Update bookkeeping in case of net2disk
                if( disk ) {
                    S_DIR    diskDir;
                    ScanDir& sdir( ud.scanDir() );

                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &diskDir) );
               
                    // Note: appendlength is the amount of bytes 
                    // appended to the existing recording using
                    // XLRAppend().
                    scanptr.length( diskDir.AppendLength );

                    // update the record-pointer
                    sdir.recordPointer( diskDir.Length );

                    // and update on disk
                    ud.write( rte.xlrdev );
                }

                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;

                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing " << args[0] << " yet ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    // If any of the temporaries is non-default, clean up
    // whatever it was
    if( s>=0 )
        ::close(s);
    return reply.str();
}

// can also be called as in2fork in which case we
// 1) need an extra argument in addition to the ip-address for
//    '=connect'; namely the scanname
// 2) write to disk & send over the network at the same time
string in2net_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // needed for diskrecording - need to remember across fn calls
    static ScanPointer    scanptr;
    static UserDirectory  ud;

    // automatic variables
    ostringstream    reply;
    // store the socket in here.
    // if we create it but cannot start the threads (or something else
    // goes wrong) then the cleanup code at the end will close the socket.
    // *if* the threads are succesfully started make sure you
    // reset this value to <0 to prevent your socket from being
    // closed 
    int                 s( -1 );
    bool                atm; // acceptable transfer mode
    const bool          fork( args[0]=="in2fork" );
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Test if the current transfermode is acceptable for this
    // function: either doing nothing, in2fork or in2net
    // (and depending on 'fork' or not we accept those)
    atm = (ctm==no_transfer ||
           (!fork && ctm==in2net) ||
           (fork && ctm==in2fork) );

    // good, if we shouldn't even be here, get out
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.lasthost << (fork?"f":"") << " : " << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        if( args[1]=="connect" ) {
            recognized = true;
            // if transfermode is already in2{net|fork}, we ARE already connected
            // (only in2{net|fork}::disconnect clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                int          sbuf( rte.netparms.sndbufsize );  
                bool         initstartval;
                string       proto( rte.netparms.get_protocol() );
                SSHANDLE     ss( rte.xlrdev.sshandle() );
                const bool   rtcp( proto=="rtcp" );
                unsigned int olen( sizeof(rte.netparms.sndbufsize) );

                // assert recognized protocol
                ASSERT_COND( ((proto=="udp")||(proto=="tcp")||(proto=="rtcp")) );

                // good. pick up optional hostname/ip to connect to
                // unless it's rtcp
                if( !rtcp && args.size()>2 && !args[2].empty() )
                    rte.lasthost = args[2];

                // in2fork requires extra arg: the scanname
                // NOTE: will throw up if none given!
                // Also perform some extra sanity checks needed
                // for disk-recording
                if( fork ) {
                    S_DIR         disk;
                    SSHANDLE      ss( rte.xlrdev.sshandle() );
                    S_DEVINFO     devInfo;

                    if(args.size()<=3 || args[3].empty())
                        THROW_EZEXCEPT(cmdexception, "No scannanme given for in2fork!");

                    // Verify that there are disks on which we *can*
                    // record!
                    XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                    ASSERT_COND( devInfo.NumDrives>0 );

                    // and they're not full or writeprotected
                    XLRCALL( ::XLRGetDirectory(ss, &disk) );
                    ASSERT_COND( !(disk.Full || disk.WriteProtected) );
                } 

                // create socket and connect 
                // if rtcp, create a listening socket instead
                if( rtcp )
                    s = getsok(2630, "tcp");
                else {
                    struct in_addr   ip;

                    s = getsok(rte.lasthost, 2630, proto);
                    // if we did connect to a multicast ip, change
                    // the default TTL from 1 => 30. 1 is definitely
                    // too much. 30 seems reasonably Ok on LightPaths.
                    if( inet_aton(rte.lasthost.c_str(), &ip)!=0 &&
                        IN_MULTICAST(ntohl(ip.s_addr)) ) {
                        unsigned char  newttl( 30 );

                        // okay, we did connex0r to a multicast addr.
                        // Possibly, failing to set the ttl is not fatal
                        // but we _do_ warn the user that their data
                        // may not actually arrive!
                        if( ::setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL,
                                         &newttl, sizeof(newttl))!=0 ) {
                            DEBUG(-1, "WARN: Failed to set MulticastTTL to "
                                      << newttl << endl);
                            DEBUG(-1, "Your data may or may not arrive, " 
                                      << "depending on LAN or WAN" << endl);
                        }
                    }
                }

                // Set sendbufsize. For rtcp it doesn't matter; it's
                // only the accepting socket. the receivebufsize will
                // be set upon connect.
                ASSERT_ZERO( ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sbuf, olen) );

                // switch off clock
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

                DEBUG(2,"connect: notclock: " << hex_t(*notclock)
                        << " SF: " << hex_t(*suspendf) << endl);
                notclock = 1;
                DEBUG(2,"connect: notclock: " << hex_t(*notclock)
                        << " SF: " << hex_t(*suspendf) << endl);

                // now program the streamstor to record from FPDP -> PCI
                XLRCALL( ::XLRSetMode(ss, (fork?SS_MODE_FORK:SS_MODE_PASSTHRU)) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetDBMode(ss, SS_FPDP_RECVMASTER, SS_DBOPT_FPDPNRASSERT) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_RECVMASTER, SS_OPT_FPDPNRASSERT) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );

                // Start the recording. depending or fork or !fork
                // we have to:
                // * update the scandir on the discpack (if fork'ing)
                // * call a different form of 'start recording'
                //   to make sure that disken are not overwritten
                if( fork ) {
                    ud  = UserDirectory( rte.xlrdev );
                    ScanDir& scandir( ud.scanDir() );

                    scanptr = scandir.getNextScan();

                    // new recording starts at end of current recording
                    scanptr.name( args[3] );
                    scanptr.start( ::XLRGetLength(ss) );
                    scanptr.length( 0ULL );

                    // write the userdirectory
                    ud.write( rte.xlrdev );

                    // when fork'ing we do not attempt to record for ever
                    // (WRAP_ENABLE==1) otherwise the disken could be overwritten
                    XLRCALL( ::XLRAppend(ss) );
                } else {
                    // in2net can run indefinitely
                    XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );
                }

                // before kicking off the threads, transfer some important variables
                // across.
                // When doing rtcp, we add the socket as a acceptsocket
                initstartval = true;
                if( rtcp ) {
                    rte.fd       = -1;
                    rte.acceptfd = s;
                    initstartval = false;
                } else {
                    rte.fd       = s;
                    rte.acceptfd = -1;
                }
                rte.queue.enable( rte.netparms.nblock );

                // goodie. now start the threads fifo2mem and mem2net!
                // start_threads() will throw up if something's fishy
                if( proto=="udp" )
                    rte.start_threads(fifo2mem, mem2net_udp, initstartval);
                else
                    rte.start_threads(fifo2mem, mem2net_tcp, initstartval);

                // Make sure the local temporaries get reset to 0 (or -1)
                // to prevent deleting/foreclosure
                s             = -1;

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = (fork?in2fork:in2net);
                rte.transfersubmode.clr_all();
                // we are connected and waiting for go
                if( !rtcp )
                    rte.transfersubmode |= connected_flag;
                rte.transfersubmode |= wait_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        // <on> : turn on dataflow
        if( args[1]=="on" ) {
            recognized = true;
            // only allow if transfermode==in2net && submode hasn't got the running flag
            // set (could be restriced to only allow if submode has wait or pause)
            // AND we must be connected
            if( rte.transfermode!=no_transfer && rte.transfermode&connected_flag 
                && (rte.transfersubmode&run_flag)==false ) {
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspend  = rte.ioboard[ mk5areg::SF ];

                // After we've acquired the mutex, we may set the 
                // variable (start) to true, then broadcast the condition.
                PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );

                // now switch on clock
                DEBUG(2,"on: notclock: " << hex_t(*notclock)
                        << " SF: " << hex_t(*suspend) << endl);
                notclock = 0;
                suspend  = 0;
                DEBUG(2,"on: notclock: " << hex_t(*notclock)
                        << " SF: " << hex_t(*suspend) << endl);
                rte.run      = true;
                // now broadcast the startcondition
                PTHREAD2_CALL( ::pthread_cond_broadcast(rte.condition),
                               ::pthread_mutex_unlock(rte.mutex) );

                // And we're done, we may release the mutex
                PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
                // indicate running state
                rte.transfersubmode.clr( wait_flag ).clr( pause_flag ).set( run_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer, in2net, or in2fork, nothing else
                if( rte.transfermode!=no_transfer )
                    if( rte.transfersubmode&connected_flag )
                        reply << " 6 : already running ;";
                    else
                        reply << " 6 : not yet connected ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        if( args[1]=="off" ) {
            recognized = true;
            // only allow if transfermode=={in2net|in2fork} && submode has the run flag
            if( rte.transfermode!=no_transfer && (rte.transfersubmode&run_flag)==true ) {
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspend  = rte.ioboard[ mk5areg::SF ];

                // We don't have to get the mutex; we just turn off the 
                // record clock on the inputboard
                DEBUG(2,"off: notclock: " << hex_t(*notclock)
                        << " SF: " << hex_t(*suspend) << endl);
                notclock = 1;
                DEBUG(2,"off: notclock: " << hex_t(*notclock)
                        << " SF: " << hex_t(*suspend) << endl);

                // indicate paused state
                rte.transfersubmode.clr( run_flag ).set( pause_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or {in2net|in2fork}, nothing else
                if( rte.transfermode!=no_transfer )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing in2net.
            // Don't care if we were running or not
            if( rte.transfermode!=no_transfer ) {
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];

                // turn off clock
                DEBUG(2,"disconnect: notclock: " << hex_t(*notclock) << endl);
                notclock = 1;
                DEBUG(2,"disconnect: notclock: " << hex_t(*notclock) << endl);

                // let the runtime stop the threads
                rte.stop_threads();

                // stop the device
                // As per the SS manual need to call 'XLRStop()'
                // twice: once for stopping the recording
                // and once for stopping the device altogether?
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                // Need to do bookkeeping if in2fork was active
                if( fork ) {
                    S_DIR    diskDir;
                    ScanDir& sdir( ud.scanDir() );

                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &diskDir) );
               
                    // Note: appendlength is the amount of bytes 
                    // appended to the existing recording using
                    // XLRAppend().
                    scanptr.length( diskDir.AppendLength );

                    // update the record-pointer
                    sdir.recordPointer( diskDir.Length );

                    // and update on disk
                    ud.write( rte.xlrdev );
                }

                // destroy allocated resources
                if( rte.fd>=0 )
                    ::close( rte.fd );
                // could be that we were still waiting for incoming
                // connection
                if( rte.acceptfd>=0 )
                    ::close( rte.acceptfd );
                // reset global transfermode variables 
                rte.fd           = -1;
                rte.acceptfd     = -1;
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing " << args[0] << " ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    // If any of the temporaries is non-default, clean up
    // whatever it was
    if( s>=0 )
        ::close(s);
    return reply.str();
}

string getlength_fn( bool, const vector<string>&, runtime& rte ) {
    ostringstream  reply;
    S_DIR          curDir;

    try {
        UserDirectory  ud( rte.xlrdev );
        const ScanDir& sd( ud.scanDir() );

        for( unsigned int i=0; i<sd.nScans(); ++i ) {
            DEBUG(0, sd[i] << endl);
        }
    }
    catch( ... ) 
    {}

    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &curDir) );
    reply << "!getlength = 0 : L" << curDir.Length << " : AL" << curDir.AppendLength
          << ": XLRGL" << ::XLRGetLength(rte.xlrdev.sshandle()) << " ;";
    return reply.str();
}

string erase_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    UserDirectory ud( rte.xlrdev );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
       reply << "0 : " << ((ud==UserDirectory())?(""):("not ")) << "erased ;";
       return reply.str();
    }

    // Ok must be command.
    // Erasen met die hap!
    ud = UserDirectory();
    ud.write( rte.xlrdev );
    XLRCALL( ::XLRErase(rte.xlrdev.sshandle(), SS_OVERWRITE_NONE) );
    reply << " 0;";
    return reply.str();
}

// Really, in2disk is 'record'. But in lieu of naming conventions ...
// the user won't see this name anyway :)
// Note: do not stick this one in the Mark5B/DOM commandmap :)
// Oh well, you'll get exceptions when trying to execute then
// anyway
string in2disk_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // This points to the scan being recorded, if any
    static UserDirectory    userdir;
    static ScanPointer      curscanptr;    
    // automatic variables
    ostringstream               reply;
    ioboard_type::iobflags_type hardware( rte.ioboard.hardware() );

    // If we're not supposed to be here!
    ASSERT_COND( (hardware&ioboard_type::mk5a_flag || hardware&ioboard_type::dim_flag) );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing record - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=in2disk ) {
        reply << " 1 : _something_ is happening and its NOT in2disk!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // record=<on>:<scanlabel>[:[<experiment-name>][:[<station-code]][:[<source>]]
        // so we require at least three elements in args:
        //      args[0] = command itself (record, in2disk, ...)
        //      args[1] = "on"
        //      args[2] = <scanlabel>
        // As per Mark5A.c the optional fields - if any - will be reordered in
        // the name as:
        // experiment_station_scan_source
        if( args[1]=="on" ) {
            ASSERT2_COND( args.size()>=3, SCINFO("not enough parameters to command") );
            recognized = true;
            // if transfermode is already in2disk, we ARE already recording
            // so we disallow that
            if( rte.transfermode==no_transfer ) {
                S_DIR         disk;
                string        scan( args[2] );
                string        experiment( OPTARG(3, args) );
                string        station( OPTARG(4, args) );
                string        source( OPTARG(5, args) );
                string        scanlabel;
                SSHANDLE      ss( rte.xlrdev.sshandle() );
                S_DEVINFO     devInfo;

                // Verify that there are disks on which we *can*
                // record!
                XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                ASSERT_COND( devInfo.NumDrives>0 );

                // Should check bank-stuff:
                //   * if we are in bank-mode
                //   * if so, if the current bank
                //     is available
                //     and not write-protect0red
                //  ...
                // Actually, the 'XLRGetDirectory()' tells us
                // most of what we want to know!
                // [it knows about banks etc and deals with that
                //  silently]
                XLRCALL( ::XLRGetDirectory(ss, &disk) );
                ASSERT_COND( !(disk.Full || disk.WriteProtected) );

                // construct the scanlabel
                if( !experiment.empty() )
                    scanlabel = experiment;
                if( !station.empty() ) {
                    if( !scanlabel.empty() )
                        station = "_"+station;
                    scanlabel += station;
                }
                if( !scan.empty() ) {
                    if( !scanlabel.empty() )
                        scan = "_"+scan;
                    scanlabel += scan;
                }
                // and finally, optionally, the source
                if( !source.empty() ) {
                    if( !scanlabel.empty() )
                        source = "_"+source;
                    scanlabel += source;
                }
                // Now then. If the scanlabel is *still* empty
                // we give it the value of '+'
                if( scanlabel.empty() )
                    scanlabel = "+";

                // Depending on Mk5A or Mk5B/DIM ...
                // switch off clock (mk5a) or
                // stop the DFH-generator
                if( hardware&ioboard_type::mk5a_flag )
                    rte.ioboard[ mk5areg::notClock ] = 1;
                else
                    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;

                // Already program the streamstor, do not
                // start Recording otherwise we can't read/write
                // the UserDirectory.
                // Let it record from FPDP -> Disk
                XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetDBMode(ss, SS_FPDP_RECVMASTER, SS_DBOPT_FPDPNRASSERT) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_RECVMASTER, SS_OPT_FPDPNRASSERT) );

                // Update the UserDirectory, at least we know the
                // streamstor programmed Ok. Still, a few things could
                // go wrong but we'll leave that for later ...
                userdir  = UserDirectory( rte.xlrdev );
                ScanDir& scandir( userdir.scanDir() );

                curscanptr = scandir.getNextScan();

                // new recording starts at end of current recording
                curscanptr.name( scanlabel );
                curscanptr.start( ::XLRGetLength(ss) );
                curscanptr.length( 0ULL );

                // write the userdirectory
                userdir.write( rte.xlrdev );

                // Great, now start recording & kick off the I/O board
                //XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 0) );
                XLRCALL( ::XLRAppend(ss) );

                if( hardware&ioboard_type::mk5a_flag )
                    rte.ioboard[ mk5areg::notClock ] = 0;
                else
                    start_mk5b_dfhg( rte );

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = in2disk;
                rte.transfersubmode.clr_all();
                // in2disk is running immediately
                rte.transfersubmode |= run_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        if( args[1]=="off" ) {
            recognized = true;
            // only allow if transfermode==in2disk && submode has the run flag
            if( rte.transfermode==in2disk && (rte.transfersubmode&run_flag)==true ) {
                S_DIR    diskDir;
                ScanDir& sdir( userdir.scanDir() );
                SSHANDLE handle( rte.xlrdev.sshandle() );

                // Depending on the actual hardware ...
                // stop transferring from I/O board => streamstor
                if( hardware&ioboard_type::mk5a_flag )
                    rte.ioboard[ mk5areg::notClock ] = 1;
                else
                    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;

                // stop the device
                // As per the SS manual need to call 'XLRStop()'
                // twice: once for stopping the recording
                // and once for stopping the device altogether?
                XLRCALL( ::XLRStop(handle) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(handle) );

                // reset global transfermode variables 
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();

                // Need to do bookkeeping
                XLRCALL( ::XLRGetDirectory(handle, &diskDir) );
               
                // Note: appendlength is the amount of bytes 
                // appended to the existing recording using
                // XLRAppend().
                curscanptr.length( diskDir.AppendLength );

                // update the record-pointer
                sdir.recordPointer( diskDir.Length );

                // and update on disk
                userdir.write( rte.xlrdev );

                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or in2disk, nothing else
                if( rte.transfermode==in2disk )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}

// From a 'struct tm', compute the Modified Julian Day, cf.
//      http://en.wikipedia.org/wiki/Julian_day
// The Julian day number can be calculated using the following formulas:
// The months January to December are 1 to 12. Astronomical year numbering is used, thus 1 BC is
// 0, 2 BC is −1, and 4713 BC is −4712. In all divisions (except for JD) the floor function is
// applied to the quotient (for dates since 1 March −4800 all quotients are non-negative, so we
// can also apply truncation).
double tm2mjd( const struct tm& tref ) {
    double    a, y, m, jd;

    // As per localtime(3)/gmtime(3), the tm_mon(th) is according to
    // 0 => Jan, 1 => Feb etc
    a   = ::floor( ((double)(14-(tref.tm_mon+1)))/12.0 );

    // tm_year is 'years since 1900'
    y   = (tref.tm_year+1900) + 4800 - a;

    m   = (tref.tm_mon+1) + 12*a - 3;

    // tm_mday is 'day of month' with '1' being the first day of the month.
    // i think we must use the convention that the first day of the month is '0'?
    // This is, obviously, assuming that the date mentioned in 'tref' is 
    // a gregorian calendar based date ...
    jd  = (double)tref.tm_mday + ::floor( (153.0*m + 2.0)/5.0 ) + 365.0*y
          + ::floor( y/4.0 ) - ::floor( y/100.0 ) + ::floor( y/400.0 ) - 32045.0;
    // that concluded the 'integral day part'.

    // Now add the time-of-day, as a fraction
    jd += ( ((double)(tref.tm_hour - 12))/24.0 +
            ((double)(tref.tm_min))/1440.0 +
            (double)tref.tm_sec );

    // finally, return the mjd
    return (jd-2400000.5);
}

int jdboy (int year) {
  int jd, y;
  
  y = year + 4799;
  jd = y * 365 + y / 4 - y / 100 + y / 400 - 31739;
  
  return jd;
}

// encode an unsigned integer into BCD
// (we don't support negative numbahs)
unsigned int bcd(unsigned int v) {
    // we can fit two BCD-digits into each byte
    unsigned int       rv( 0 );
    const unsigned int nbcd_digits( 2*sizeof(unsigned int) );

    for( unsigned int i=0, pos=0; i<nbcd_digits; ++i, pos+=4 ) {
        rv |= ((v%10)<<pos);
        v  /= 10;
    }
    return rv;
}

// go from bcd => 'normal integer'
unsigned int unbcd(unsigned int v) {
    // we can fit two BCD-digits into each byte
    const unsigned int  nbcd_digits( 2*sizeof(unsigned int) );

    unsigned int  rv( 0 );
    unsigned int  factor( 1 );
    for( unsigned int i=0; i<nbcd_digits; ++i, factor*=10 ) {
        rv += ((v&0xf)*factor);
        v >>= 4;
    }
    return rv;

}


// dim2net = in2net equivalent for Mk5B/DIM
string dim2net_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    ostringstream    reply;

    // store the socket in here.
    // if we create it but cannot start the threads (or something else
    // goes wrong) then the cleanup code at the end will close the socket.
    // *if* the threads are succesfully started make sure you
    // reset this value to <0 to prevent your socket from being
    // closed 
    int              s = -1;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing in2net - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=in2net ) {
        reply << " 1 : _something_ is happening and its NOT in2net!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.lasthost << " : " << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        if( args[1]=="connect" ) {
            recognized = true;
            // if transfermode is already in2net, we ARE already connected
            // (only in2net::disconnect clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                int          sbuf( rte.netparms.sndbufsize );  
                string       proto( rte.netparms.get_protocol() );
                SSHANDLE     ss( rte.xlrdev.sshandle() );
                unsigned int olen( sizeof(rte.netparms.sndbufsize) );

                // assert recognized protocol
                ASSERT_COND( ((proto=="udp")||(proto=="tcp")) );

                // good. pick up optional hostname/ip to connect to
                if( args.size()>2 )
                    rte.lasthost = args[2];

                // create socket and connect 
                s = getsok(rte.lasthost, 2630, proto);

                // Set sendbufsize
                ASSERT_ZERO( ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sbuf, olen) );

                // Do we need to set up anything in the DIM at this time?
                // Don't think so: most of the stuff we require is done
                // by 'play_rate' and 'mode'. Only when we receive an 'in2net=on'
                // we need to set the thing off!

                // now program the streamstor to record from FPDP -> PCI
                XLRCALL( ::XLRSetMode(ss, SS_MODE_PASSTHRU) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetDBMode(ss, SS_FPDP_RECVMASTER, SS_DBOPT_FPDPNRASSERT) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_RECVMASTER, SS_OPT_FPDPNRASSERT) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );

                // before kicking off the threads, transfer some important variables
                // across.
                rte.fd     = s;
                rte.queue.enable( rte.netparms.nblock );

                // goodie. now start the threads fifo2mem and mem2net!
                // start_threads() will throw up if something's fishy
                if( proto=="udp" )
                    rte.start_threads(fifo2mem, mem2net_udp);
                else
                    rte.start_threads(fifo2mem, mem2net_tcp);

                // Make sure the local temporaries get reset to 0 (or -1)
                // to prevent deleting/foreclosure
                s             = -1;

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = in2net;
                rte.transfersubmode.clr_all();
                // we are connected and waiting for go
                rte.transfersubmode |= connected_flag;
                rte.transfersubmode |= wait_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        // <on> : turn on dataflow
        if( args[1]=="on" ) {
            recognized = true;
            // only allow if transfermode==in2net && submode hasn't got the running flag
            // set (could be restriced to only allow if submode has wait or pause)

            // first check if the transfer was paused
            if( rte.transfermode==in2net && (rte.transfersubmode&pause_flag)==true ) {
                // Good. Unpause the DIM. Will restart datatransfer on next 1PPS
                rte.ioboard[ mk5breg::DIM_PAUSE ] = 0;
                // And let the flags represent this new state
                rte.transfersubmode.clr( pause_flag ).set( run_flag );
            } else if( rte.transfermode==in2net && (rte.transfersubmode&run_flag)==false ) {
                // start the hardware!
                start_mk5b_dfhg( rte );

                // Ok. Now the H/W is set up, all that's left is to
                // kick off the threads - that is to say: they must
                // be informed that it's about time they actually
                // start doing some work.

                // After we've acquired the mutex, we may set the 
                // variable (start) to true, then broadcast the condition.
                PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );

                rte.run      = true;
                // now broadcast the startcondition
                PTHREAD2_CALL( ::pthread_cond_broadcast(rte.condition),
                               ::pthread_mutex_unlock(rte.mutex) );

                // And we're done, we may release the mutex
                PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
                // indicate running state
                rte.transfersubmode.clr( wait_flag ).clr( pause_flag ).set( run_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or in2net, nothing else
                if( rte.transfermode==in2net )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        if( args[1]=="off" ) {
            recognized = true;
            // only allow if transfermode==in2net && submode has the run flag
            if( rte.transfermode==in2net && (rte.transfersubmode&run_flag)==true ) {
                // We don't have to get the mutex; we just pause the DIM
                rte.ioboard[ mk5breg::DIM_PAUSE ] = 1;

                // indicate paused state
                rte.transfersubmode.clr( run_flag ).set( pause_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or in2net, nothing else
                if( rte.transfermode==in2net )
                    reply << " 6 : not running yet;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing in2net.
            // Don't care if we were running or not
            if( rte.transfermode==in2net ) {
                // Stop the H/W [or rather: make sure 'startstop'==0, so
                // it's in a known, not-started state ;)]
                rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;

                // let the runtime stop the threads
                rte.stop_threads();

                // Stop the streamstor device
                // As per the SS manual need to call 'XLRStop()'
                // twice: once for stopping the recording
                // and once for stopping the device altogether?
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                // destroy allocated resources
                if( rte.fd>=0 )
                    ::close( rte.fd );
                // reset global transfermode variables 
                rte.fd           = -1;
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing in2net ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    // If any of the temporaries is non-default, clean up
    // whatever it was
    if( s>=0 )
        ::close(s);
    return reply.str();
}

// The 1PPS source command for Mk5B/DIM
string pps_source_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // Pulse-per-second register value in HumanReadableFormat
    static const string pps_hrf[4] = { "none", "altA", "altB", "vsi" };
    const unsigned int  npps( sizeof(pps_hrf)/sizeof(pps_hrf[0]) );
    // variables
    unsigned int                 selpps;
    ostringstream                oss;
    ioboard_type::mk5bregpointer pps = rte.ioboard[ mk5breg::DIM_SELPP ];

    oss << "!" << args[0] << (qry?('?'):('='));
    if( qry ) {
        oss << " 0 : " << pps_hrf[ *pps ] << " ;";
        return oss.str();
    }
    // It was a command. We must have (at least) one argument [the first, actually]
    // and it must be non-empty at that!
    if( args.size()<2 || args[1].empty() ) {
        oss << " 3 : Missing argument to command ;";
        return oss.str();
    }
    // See if we recognize the pps string
    for( selpps=0; selpps<npps; ++selpps )
        if( ::strcasecmp(args[1].c_str(), pps_hrf[selpps].c_str())==0 )
            break;
    if( selpps==npps ) {
        oss << " 4 : Unknown PPS source '" << args[1] << "' ;";
    } else {
        // write the new PPS source into the hardware
        pps = selpps;
        oss << " 0 ;";
    }
    return oss.str();
}


// mtu function
string mtu_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));
    if( q ) {
        oss << " 0 : " << np.get_mtu() << " ;";
    }  else {
        // if command, must have argument
        if( args.size()>=2 && args[1].size() ) {
            unsigned int  m = (unsigned int)::strtol(args[1].c_str(), 0, 0);

            np.set_mtu( m );
            oss << " 0 ;";
        } else {
            oss << " 1 : Missing argument to command ;";
        }
    }
    return oss.str();
}

// netstat. Tells (actual) blocksize, mtu and datagramsize
string netstat_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream        oss;
    const netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('=')) << " = 0";
    // first up: datagramsize
    oss << "  : dg=" << np.get_datagramsize();
    // blocksize
    oss << " : bs=" << np.get_blocksize();

    oss << " ;";
    return oss.str();
}

// the tstat function
string tstat_fn(bool, const vector<string>&, runtime& rte ) {
    double                    dt;
    const double              fifosize( 512 * 1024 * 1024 );
    unsigned long             fifolen;
    ostringstream             reply;
    unsigned long long        bytetomem_cur;
    unsigned long long        bytefrommem_cur;
    static struct timeb       time_cur;
    static struct timeb*      time_last( 0 );
    static unsigned long long bytetomem_last;
    static unsigned long long bytefrommem_last;

    // Take a snapshot of the values & get the time
    bytetomem_cur   = rte.nbyte_to_mem;
    bytefrommem_cur = rte.nbyte_from_mem;
    ftime( &time_cur );
    fifolen = ::XLRGetFIFOLength(rte.xlrdev.sshandle());

    if( !time_last ) {
        time_last  = new struct timeb;
        *time_last = time_cur;
    }

    // Compute 'dt'. If it's too small, do not even try to compute rates
    dt = (time_cur.time + time_cur.millitm/1000.0) - 
         (time_last->time + time_last->millitm/1000.0);

    if( dt>0.1 ) {
        double tomem_rate   = (((double)(bytetomem_cur-bytetomem_last))/(dt*1.0E6))*8.0;
        double frommem_rate = (((double)(bytefrommem_cur-bytefrommem_last))/(dt*1.0E6))*8.0;
        double fifolevel    = ((double)fifolen/fifosize) * 100.0;

        reply << "!tstat = 0 : "
              // dt in seconds
              << format("%6.2lfs", dt) << " "
              // device -> memory rate in 10^6 bits per second
              << rte.tomem_dev << ">M " << format("%8.4lfMb/s", tomem_rate) << " "
              // memory -> device rate in 10^6 bits per second
              << "M>" << rte.frommem_dev << " " << format("%8.4lfMb/s", frommem_rate) << " "
              // and the FIFO-fill-level
              << "F" << format("%4.1lf%%", fifolevel)
              << " ;";
    } else {
        reply << "!tstat = 1 : Retry - we're initialized now ;";
    }

    // Update statics
    *time_last           = time_cur;
    bytetomem_last       = bytetomem_cur;
    bytefrommem_last     = bytefrommem_cur;
    return reply.str();
}

string evlbi_fn(bool, const vector<string>& args, runtime& rte ) {
    ostringstream reply;

    reply << "!" << args[0] << "? 0 : " << rte.evlbi_stats << " ;";
    return reply.str();
}

string reset_fn(bool, const vector<string>&, runtime& rte ) {
    rte.reset_ioboard();
    return "!reset = 0 ;";
}


// specialization for Mark5B/DIM
// We do *not* test for DIM; others should've
// checked for us
// mode={ext|tvg|ramp}:<bitstreammask>[:<decimation ratio>[:<fpdpmode>]]
// fpdpmode not supported by this code.
// We allow 'tvg+<num>' to set a specific tvg mode. See runtime.h for 
// details. Default will map to 'tvg+1' [internal tvg]
string mk5bdim_mode_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    mk5b_inputmode_type curipm;

    // Wether this is command || query, we need the current inputmode
    rte.get_input( curipm );

    // This part of the reply we can already form
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        int    decimation;
        // Decimation = 2^j
        decimation = (int)::round( ::exp(curipm.j * M_LN2) );
        reply << "0 : " << curipm.datasource << " : " << hex_t(curipm.bitstreammask)
              << " : " << decimation << " ;";
        return reply.str();
    }

    // Must be the mode command. Only allow if we're not doing a transfer
    if( rte.transfermode!=no_transfer ) {
        reply << "4: cannot change during " << rte.transfermode << " ;";
        return reply.str();
    }
    // We require at least two non-empty arguments
    // ('data source' and 'bitstreammask')
    if( args.size()<3 ||
        args[1].empty() || args[2].empty() ) {
        reply << "3: must have at least two non-empty arguments ;";
        return reply.str(); 
    }
    // Start off with an empty inputmode.
    int                     tvgmode;
    mk5b_inputmode_type     ipm( mk5b_inputmode_type::empty );

    // Get the current inputmode. _some_ parameters must be left the same.
    // For (most, but not all) non-boolean parameters we have 'majik' values
    // indicating 'do not change this setting' but for booleans (and some other
    // 'verbatim' values that impossible).
    // So we just copy the current value(s) of those we want to keep unmodified.

    // use 'clock_set' to modify these!
    ipm.selcgclk  = curipm.selcgclk; 
    ipm.seldim    = curipm.seldim;
    ipm.seldot    = curipm.seldot;

    ipm.userword  = curipm.userword;
    ipm.startstop = curipm.startstop;
    ipm.tvrmask   = curipm.tvrmask;
    ipm.gocom     = curipm.gocom;

    // Other booleans (fpdp2/tvgsel a.o. are explicitly set below)
    // or are fine with their current default

    // Argument 1: the datasource
    // If the 'datasource' is "just" tvg, this is taken to mean "tvg+1"
    ipm.datasource     = ((args[1]=="tvg")?(string("tvg+1")):(args[1]));

    DEBUG(2, "Got datasource " << ipm.datasource << endl);

    // Now check what the usr wants
    if( ipm.datasource=="ext" ) {
        // aaaaah! Usr want REAL data!
        ipm.tvg        = 0;
        ipm.tvgsel     = false;
    } else if( ipm.datasource=="ramp" ) {
        // Usr want incrementing test pattern. Well, let's not deny it then!
        ipm.tvg        = 7;
        ipm.tvgsel     = true;
    } else if( ::sscanf(ipm.datasource.c_str(), "tvg+%d", &tvgmode)==1 ) {
        // Usr requested a specific tvgmode.
        ipm.tvg        = tvgmode;
        // Verify that we can do it

        // tvgmode==0 implies external data which contradicts 'tvg' mode.
        // Also, a negative number is out-of-the-question
        ASSERT2_COND( ipm.tvg>=1 && ipm.tvg<=8, SCINFO(" Invalid TVG mode number requested") );

        ipm.tvgsel     = true;

        // these modes request FPDP2, verify the H/W can do it
        if( ipm.tvg==3 || ipm.tvg==4 || ipm.tvg==5 || ipm.tvg==8 ) {
           ASSERT2_COND( rte.ioboard.hardware()&ioboard_type::fpdp_II_flag,
                         SCINFO(" requested TVG mode needs FPDP2 but h/w does not support it") );
           // do request FPDP2
           ipm.fpdp2   = true;
        }
    } else {
        reply << "3: Unknown datasource " << args[1] << " ;";
        return reply.str();
    }

    // Argument 2: the bitstreammask in hex.
    // Be not _very_ restrictive here. "The user will know
    // what he/she is doing" ... HAHAHAAA (Famous Last Words ..)
    // The 'set_input()' will do the parameter verification so
    // that's why we don't bother here
    ipm.bitstreammask  = ::strtoul( args[2].c_str(), 0, 16 );

    // Optional argument 3: the decimation.
    // Again, the actual value will be verified before it is sent to the H/W
    // The decimation is 'j', not 'k'! Bah!
    // Also: the argument is/should be given as one of: 1,2,4,8,16
    // the 'j' value is the exponent we must write into the H/W.
    if( args.size()>=4 && !args[3].empty() ) {
        int     i_decm;
        double  decm_req( ::strtod(args[3].c_str(), 0) ), decm_closest;

        // from the double value, find the closest exponent
        // of 2 that yields the requested decimation.
        i_decm       = (int)::round( ::log(decm_req)/M_LN2 );
        decm_closest = ::exp(i_decm * M_LN2);

        // We only allow decimation up to 16 [0 < i_decm <= 4]
        ASSERT2_COND( (i_decm>=0 && i_decm<=4),
                      SCINFO(" Requested decimation is not >=1 and <=16") );
        // And it must be a power of two!
        ASSERT2_COND( ::fabs(decm_req - decm_closest)<=0.01,
                      SCINFO(" Requested decimation is not a power of 2") );

        // Great. Now transfer the integer value to the h/w
        ipm.j = i_decm;
    }

    // Optional argument 4: d'oh, don't do anything

    // Make sure other stuff is in correct setting
    ipm.gocom         = false;

    rte.set_input( ipm );

    reply << "0 ; ";
    // Return answer to caller
    return reply.str();
}

// specialization for Mark5A(+)
string mk5a_mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;

    // query can always be done
    if( qry ) {
        inputmode_type  ipm;
        outputmode_type opm;

        rte.get_input( ipm );
        rte.get_output( opm );

        reply << "!" << args[0] << "? 0 : "
              << ipm.mode << " : " << ipm.ntracks << " : "
              << opm.mode << " : " << opm.ntracks << " : "
              << (opm.synced?('s'):('-')) << " : " << opm.numresyncs
              << " ;";
        return reply.str();
    }

    // Command only allowed if doing nothing
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "!" << args[0] << "= 3 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    inputmode_type  ipm( inputmode_type::empty );
    outputmode_type opm( outputmode_type::empty );

    reply.str( string() );

    // first argument. the actual 'mode'
    if( args.size()>=2 && args[1].size() ) {
        opm.mode = ipm.mode = args[1];
    }

    // 2nd: number of tracks
    if( args.size()>=3 && args[2].size() ) {
        unsigned long v = ::strtoul(args[2].c_str(), 0, 0);

        if( v<=0 || v>64 )
            reply << "!" << args[0] << "= 8 : ntrack out-of-range ("
                  << args[2] << ") usefull range <0, 64] ;";
        else
            ipm.ntracks = opm.ntracks = (int)v;
    }

    try {
        // set mode to h/w
        rte.set_input( ipm );
        rte.set_output( opm );
    }
    catch( const exception& e ) {
        reply << "!" << args[0] << "= 8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << "!" << args[0] << "= 8 : Caught unknown exception! ;";
    }

    // no reply yet indicates "ok"
    if( reply.str().empty() )
        reply << "!" << args[0] << "= 0 ;";
    return reply.str();
}

// Mark5A(+) playrate function
string playrate_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";
    if( qry ) {
        double          clkfreq;
        outputmode_type opm;

        rte.get_output( opm );

        clkfreq  = opm.freq;
        clkfreq *= 9.0/8.0;

        // need implementation of table
        // listed in Mark5ACommands.pdf under "playrate" command
        reply << "0 : " << opm.freq << " : " << clkfreq << " : " << clkfreq << " ;";
        return reply.str();
    }

    // if command, we require 'n argument
    // for now, we discard the first argument but just look at the frequency
    if( args.size()<3 ) {
        reply << "3 : not enough arguments to command ;";
        return reply.str();
    }
    // If there is a frequency given, program it
    if( args[2].size() ) {
        outputmode_type   opm( outputmode_type::empty );

        opm.freq = ::strtod(args[2].c_str(), 0);
        DEBUG(2, "Setting clockfreq to " << opm.freq << endl);
        rte.set_output( opm );
    }
    // indicate success
    reply << " 0 ;";
    return reply.str();
}

// Mark5BDIM clock_set (replaces 'play_rate')
string clock_set_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream       reply;
    mk5b_inputmode_type curipm;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Get current inputmode
    rte.get_input( curipm );

    if( qry ) {
        double              clkfreq;
        
        // Get the 'K' registervalue: f = 2^(k+1)
        // Go from e^(k+1) => 2^(k+1)
        clkfreq = ::exp( ((double)(curipm.k+1))*M_LN2 );

        reply << "0 : " << clkfreq 
              << " : " << ((curipm.selcgclk)?("int"):("ext"))
              << " : " << curipm.clockfreq << " ;";
        return reply.str();
    }

    // if command, we require two non-empty arguments.
    // clock_set = <clock freq> : <clock source> [: <clock-generator-frequency>]
    if( args.size()<3 ||
        args[1].empty() || args[2].empty() ) {
        reply << "3 : must have at least two non-empty arguments ; ";
        return reply.str();
    }

    // Verify we recognize the clock-source
    ASSERT2_COND( args[2]=="int"||args[2]=="ext",
                  SCINFO(" clock-source " << args[2] << " unknown, use int or ext") );

    // We already got the current input mode.
    // Modify it such that it reflects the new clock settings.

    // If there is a frequency given, inspect it and transform it
    // to a 'k' value [and see if that _can_ be done!]
    int      k;
    string   warning;
    double   f_req, f_closest;

    f_req     = ::strtod(args[1].c_str(), 0);
    ASSERT_COND( (f_req>=0.0) );

    // can only do 2,4,8,16,32,64 MHz
    // cf IOBoard.c:
    // (0.5 - 1.0 = -0.5; the 0.5 gives roundoff)
    //k         = (int)(::log(f_req)/M_LN2 - 0.5);
    // HV's own rendition:
    k         = (int)::round( ::log(f_req)/M_LN2 ) - 1;
    f_closest = ::exp((k + 1) * M_LN2);
    // Check if in range [0<= k <= 5] AND
    // requested f close to what we can support
    ASSERT2_COND( (k>=0 && k<5),
            SCINFO(" Requested frequency " << f_req << " <2 or >64 is not allowed") );
    ASSERT2_COND( (::fabs(f_closest - f_req)<0.01),
            SCINFO(" Requested frequency " << f_req << " is not a power of 2") );

    curipm.k         = k;

    // We do not alter the programmed clockfrequency, unless the
    // usr requests we do (if there is a 3rd argument,
    // it's the clock-generator's clockfrequency)
    curipm.clockfreq = 0;
    if( args.size()>=4 && !args[3].empty() )
        curipm.clockfreq = ::strtod( args[3].c_str(), 0 );

    // We already verified that the clocksource is 'int' or 'ext'
    // 64MHz *implies* using the external VSI clock; the on-board
    // clockgenerator can only do 40MHz
    // If the user says '64MHz' with 'internal' clock we just warn
    // him/her ...
    curipm.selcgclk = (args[2]=="int");
    if( k==5 && curipm.selcgclk )
        warning = "64MHz with internal clock will not fail but timecodes will be bogus";

    // Depending on internal or external clock, select the PCI interrupt source
    // (maybe it's valid to set both but I don't know)
    curipm.seldim = !curipm.selcgclk;
    curipm.seldot = curipm.selcgclk;

    // Send to hardware
    rte.set_input( curipm );
    reply << " 0";
    if( !warning.empty() )
        reply << " : " << warning;
    reply << " ;";
    return reply.str();
}



// Expect:
// net_protcol=<protocol>[:<socbufsize>[:<blocksize>[:<nblock>]]
// Note: socbufsize will set BOTH send and RECV bufsize
string net_protocol_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream  reply;
    netparms_type& np( rte.netparms );

    if( qry ) {
        reply << "!" << args[0] << "? 0 : "
              << np.get_protocol() << " : " ;
        if( np.rcvbufsize==np.sndbufsize )
            reply << np.rcvbufsize;
        else
            reply << "Rx " << np.rcvbufsize << ", Tx " << np.sndbufsize;
        reply << " : " << np.get_blocksize()
              << " : " << np.nblock 
              << " ;";
        return reply.str();
    }
    // do not allow to change during transfers
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }
    // Not query. Pick up all the values that are given
    // If len(args)<=1 *and* it's not a query, that's a syntax error!
    if( args.size()<=1 )
        return string("!net_protocol = 3 : Empty command (no arguments given, really) ;");

    // Make sure the reply is RLY empty [see before "return" below why]
    reply.str( string() );

    // See which arguments we got
    // #1 : <protocol>
    if( args.size()>=2 && !args[1].empty() )
        np.set_protocol( args[1] );

    // #2 : <socbuf size> [we set both send and receivebufsizes to this value]
    if( args.size()>=3 && !args[2].empty() ) {
        long int   v = ::strtol(args[2].c_str(), 0, 0);

        // Check if it's a sensible "int" value for size, ie >0 and <=INT_MAX
        if( v<=0 || v>INT_MAX ) {
            reply << "!" << args[0] << " = 8 : <socbuf size> out of range <=0 or >= INT_MAX ; ";
        } else {
            np.rcvbufsize = np.sndbufsize = (int)v;
        }
    }

    // #3 : <workbuf> [the size of the blocks used by the threads]
    //      Value will be adjusted to accomodate an integral number of
    //      datagrams.
    if( args.size()>=4 && !args[3].empty() ) {
        unsigned long int   v = ::strtoul(args[3].c_str(), 0, 0);

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v<=UINT_MAX ) {
            np.set_blocksize( (unsigned int)v );
        } else {
            reply << "!" << args[0] << " = 8 : <workbufsize> out of range (too large) ;";
        }
    }

    // #4 : <nbuf>
    if( args.size()>=5 && !args[4].empty() ) {
        unsigned long int   v = ::strtoul(args[4].c_str(), 0, 0);

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v>0 && v<=UINT_MAX )
            np.nblock = (unsigned int)v;
        else
            reply << "!" << args[0] << " = 8 : <nbuf> out of range - 0 or too large ;";
    }
    if( args.size()>5 )
        DEBUG(1,"Extra arguments (>5) ignored" << endl);

    // If reply is still empty, the command was executed succesfully - indicate so
    if( reply.str().empty() )
        reply << "!" << args[0] << " = 0 ;";
    return reply.str();
}


// status? [only supports query. Can't be bothered to complain
// if someone calls it as a command]
string status_fn(bool, const vector<string>&, runtime& rte) {
    // flag definitions for readability and consistency
    const unsigned int record_flag   = 0x1<<6; 
    const unsigned int playback_flag = 0x1<<8; 
    // automatic variables
    unsigned int       st;
    ostringstream      reply;

    // compile the hex status word
    st = 1; // 0x1 == ready
    switch( rte.transfermode ) {
        case in2disk:
            st |= record_flag;
            break;
        case disk2net:
            st |= playback_flag;
            st |= (0x1<<14); // bit 14: disk2net active
            break;
        case in2net:
            st |= record_flag;
            st |= (0x1<<16); // bit 16: in2net active/waiting
            break;
        case net2out:
            st |= playback_flag;
            st |= (0x1<<17); // bit 17: net2out active
            break;
        default:
            // d'oh
            break;
    }
    reply << "!status? 0 : " << hex_t(st) << " ;";
    return reply.str();
}

string debug_fn( bool , const vector<string>& args, runtime& rte ) {
    rte.ioboard.dbg();
    return string("!")+args[0]+"= 0 ;";
}

string interpacketdelay_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    if( qry ) {
        reply << " 0 : " << rte.netparms.interpacketdelay <<  " usec ;";
        return reply.str();
    }

    // if command, we must have an argument
    if( args.size()<2 || args[1].empty() ) {
        reply << " 3 : Command must have argument ;";
        return reply.str();
    }

    // Great. Now 'pars0r' the argument
    // If it contains 'k' or 'M' we interpret that as
    // base-10 multiples (not base 1024, as in "1kB==1024bytes")
    // default unit is 'none'

    try {
        unsigned int   ipd;

        ASSERT_COND( (::sscanf(args[1].c_str(), "%u", &ipd)==1) );

        // great. install new value
        // Before we do that, grab the mutex, as other threads may be
        // using this value ...
        PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );
        rte.netparms.interpacketdelay = ipd;
        PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
        reply << " 0 ;";
    }
    catch( const exception& e ) {
        reply << " 8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 8 : Caught unknown exception ;";
    }
    return reply.str();
}

string packetdroprate_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    if( qry ) {
        reply << " 0 : ";
        if( rte.packet_drop_rate )
            reply << "1/" << rte.packet_drop_rate;
        else
            reply << " none ";
        reply << " ;";

        return reply.str();
    }

    // if command, we must have an argument
    if( args.size()<2 || args[1].empty() ) {
        reply << " 3 : Command must have argument ;";
        return reply.str();
    }

    // Great. Now 'pars0r' the argument
    // If it contains 'k' or 'M' we interpret that as
    // base-10 multiples (not base 1024, as in "1kB==1024bytes")
    // default unit is 'none'

    try {
        unsigned int   pdr;

        ASSERT_COND( (::sscanf(args[1].c_str(), "%u", &pdr)==1) );

        // great. install new value. As this value may be used
        // by a running thread, we, as a courtesy, grab the mutex
        // before clobbering the value
        PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );
        rte.packet_drop_rate = (unsigned long long int)pdr;
        PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
        reply << " 0 ;";
    }
    catch( const exception& e ) {
        reply << " 8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 8 : Caught unknown exception ;";
    }
    return reply.str();
}


// udp helper function => can set policy to use of how
// to deal with UDP datagrams out-of-order etc
string udphelper_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream                     reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    if( qry ) {
        reply << "0 : " << rte.netparms.udphelper << " ;";
        return reply.str();
    }

    // if command, we require at least an argument!
    if( args.size()<2 ) {
        reply << "3 : Command must have argument ;";
        return reply.str();
    }

    try {
        const udphelper_maptype&          helpermap( udphelper_map() );
        udphelper_maptype::const_iterator newhelper;

        if( (newhelper=helpermap.find(args[1]))==helpermap.end() ) {
            reply << "3 : Unknown helper '" << args[1] << "' ;";
        } else {
            // great, the helper the usr requested does exist.
            // now install it
            PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );
            rte.netparms.udphelper = args[1];
            PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
            reply << "0 ;";
        }
    }
    catch( const exception& e ) {
        reply << "8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << "8 : Caught unknown exception ;";
    }
    return reply.str();
}

string skip_fn( bool q, const vector<string>& args, runtime& rte ) {
	long long      nskip;
	ostringstream  reply;
	
	reply << "!" << args[0] << (q?('?'):('='));

	if( q ) {
		reply << " 0 : " << rte.lastskip << " ;";
		return reply.str();
	}

    // Not a query. Only allow skip if doing a 
    // transfer to which it sensibly applies:
    if( rte.transfermode!=net2out ) {
        reply << " 6 : it does not apply to " << rte.transfermode << " ;";
        return reply.str();
    }

    // We rilly need an argument
	if( args.size()<2 || args[1].empty() ) {
		reply << " 3 : Command needs argument! ;";
		return reply.str();
	}

    // Now see how much to skip
	nskip    = ::strtol(args[1].c_str(), 0, 0);

    // Attempt to do the skip. Return value is always
    // positive so must remember to get the sign right
    // before testing if the skip was achieved
    rte.lastskip = ::XLRSkip( rte.xlrdev.sshandle(),
                              abs(nskip), (nskip>=0) );
    if( nskip<0 )
        rte.lastskip = -rte.lastskip;

    // If the achieved skip is not the expected skip ...
    reply << " 0";
    if( rte.lastskip!=nskip )
        reply << " : Requested skip was not achieved";
    reply << " ;";
    return reply.str();
}

// This one works both on Mk5B/DIM and Mk5B/DOM
// (because it looks at the h/w and does "The Right Thing (tm)"
string led_fn(bool q, const vector<string>& args, runtime& rte) {
	ostringstream                reply;
    ioboard_type::iobflags_type  hw = rte.ioboard.hardware();
    ioboard_type::mk5bregpointer led0;
    ioboard_type::mk5bregpointer led1;
	
	reply << "!" << args[0] << (q?('?'):('='));

    // only check mk5b flag. it *could* be possible that
    // only the mk5b flag is set and neither of dim/dom ...
    // the ioboard.cc code should make sure that this
    // does NOT occur for best operation
    if( !(hw&ioboard_type::mk5b_flag) ) {
        reply << " 8 : This is not a Mk5B ;";
        return reply.str();
    }
    // Ok, depending on dim or dom, let the registers for led0/1
    // point at the correct location
    if( hw&ioboard_type::dim_flag ) {
        led0 = rte.ioboard[mk5breg::DIM_LED0];
        led1 = rte.ioboard[mk5breg::DIM_LED1];
    } else {
        led0 = rte.ioboard[mk5breg::DOM_LED0];
        led1 = rte.ioboard[mk5breg::DOM_LED1];
    }

	if( q ) {
        mk5breg::led_color            l0, l1;

        l0 = (mk5breg::led_color)*led0;
        l1 = (mk5breg::led_color)*led1;
		reply << " 0 : " << l0 << " : " << l1 << " ;";
		return reply.str();
	}

    // for DOM we must first enable the leds?
    if( hw&ioboard_type::dom_flag )
        rte.ioboard[mk5breg::DOM_LEDENABLE] = 1;

    if( args.size()>=2 && args[1].size() ) {
        led0 = ::atoi(args[1].c_str());
    }
    if( args.size()>=3 && args[2].size() ) {
        led1 = ::atoi(args[2].c_str());
    }
    reply << " 0 ; ";
    return reply.str();
}

string dtsid_fn(bool , const vector<string>& args, runtime& rte) {
	ostringstream                reply;
    ioboard_type::iobflags_type  hw = rte.ioboard.hardware();

	reply << "!" << args[0] << "? 0 : ";
	if( hw&ioboard_type::mk5a_flag )
		reply << "mark5A";
	else if( hw&ioboard_type::mk5b_flag )
		reply << "mark5b";
	else
		reply << "-";
	reply << " : - : - : - : - ;";
	return reply.str();
}


string scandir_fn(bool, const vector<string>&, runtime& rte ) {
    ostringstream   reply;
    UserDirectory   ud( rte.xlrdev );

    reply << "Read userdir, layout '" << ud.getLayout() << "'";
    if( ud.getLayout()!=UserDirectory::UnknownLayout ) {
        const ScanDir& sd( ud.scanDir() );

        reply << " it has " << sd.nScans() << " recorded scans. ";
        if( sd.nScans() )
            reply << sd[0];
    }
    return reply.str();
}

// wait for 1PPS-sync to appear. This is highly Mk5B
// spezifik so if you call this onna Mk5A itz gonna FAIL!
// Muhahahahaa!
//  pps=* [force resync]  (actual argument ignored)
//  pps?  [report 1PPS status]
string pps_fn(bool q, const vector<string>& args, runtime& rte) {
    double             dt;
    ostringstream      reply;
    const double       syncwait( 3.0 ); // max time to wait for PPS, in seconds
    struct timeval     start, end;
    const unsigned int selpp( *rte.ioboard[mk5breg::DIM_SELPP] );

	reply << "!" << args[0] << (q?('?'):('='));

    // if there's no 1PPS signal set, we do nothing
    if( selpp==0 ) {
        reply << " 6 : No 1PPS signal set (use 1pps_source command) ;";
        return reply.str();
    }

    // good, check if query
    if( q ) {
        const bool  sunk( *rte.ioboard[mk5breg::DIM_SUNKPPS] );
        const bool  e_sync( *rte.ioboard[mk5breg::DIM_EXACT_SYNC] );
        const bool  a_sync( *rte.ioboard[mk5breg::DIM_APERTURE_SYNC] );

        // check consistency: if not sunk, then neither of exact/aperture
        // should be set (i guess), nor, if sunk, may both be set
        // (the pps is either exact or outside the window but not both)
        reply << " 0 : " << (!sunk?"NOT ":"") << " synced ";
        if( e_sync )
            reply << " [not incident with DOT1PPS]";
        if( a_sync )
            reply << " [> 3 clocks off]";
        reply << " ;";
#if 0
        if((!sunk && (e_sync || a_sync)) || (sunk && e_sync && a_sync)) {
            reply << " 6 : ARG - (!sunk && (e||a)) || (sunk && e && a) ["
                  << sunk << ", " << e_sync << ", " << a_sync << "] ;";
        } else {
            reply << " 0 : " << (!sunk?"NOT ":"") << " synced ";
            if( e_sync )
                reply << " [not incident with DOT1PPS]";
            if( a_sync )
                reply << " [> 3 clocks off]";
            reply << " ;";
        }
#endif
        return reply.str();
    }

    // ok, it was command.
    // trigger a sync attempt, wait for some time [3 seconds?]
    // at maximum for the PPS to occur, clear the PPSFLAGS and
    // then display the systemtime at which the sync occurred

    // Note: the poll-loop below might be implementen rather 
    // awkward but I've tried to determine the time-of-sync
    // as accurate as I could; therefore I really tried to 
    // remove as much unknown time consumption systemcalls
    // as possible.
    register bool      sunk;
    const unsigned int wait_per_iter = 2; // 2 microseconds/iteration
    unsigned long int  max_loops = ((unsigned long int)(syncwait*1.0e6)/wait_per_iter);

    // Pulse SYNCPPS to trigger zynchronization attempt!
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 1;
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 0;

    // now wait [for some maximum amount of time]
    // for SUNKPPS to transition to '1'
    ::gettimeofday(&start, 0);
    while( max_loops-- ) {
        if( (sunk=*rte.ioboard[mk5breg::DIM_SUNKPPS])==true )
            break;
        // Ok, SUNKPPS not 1 yet.
        // sleep a bit and retry
        busywait( wait_per_iter );
    };
    ::gettimeofday(&end, 0);
    dt = ((double)end.tv_sec + (double)end.tv_usec/1.0e6) -
        ((double)start.tv_sec + (double)start.tv_usec/1.0e6);

    if( !sunk ) {
        reply << " 4 : Failed to sync to 1PPS within " << dt << "seconds ;";
    } else {
        char      tbuf[128];
        double    frac_sec;
        struct tm gmt;

        // As per Mark5B-DIM-Registers.pdf Sec. "Typical sequence of operations":
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 1;
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 0;

        // convert 'timeofday' at sync to gmt
        ASSERT_NZERO( ::gmtime_r(&end.tv_sec, &gmt) );
        frac_sec = end.tv_usec/1.0e6;
        ::strftime(tbuf, sizeof(tbuf), "%a %b %d %H:%M:", &gmt);
        reply << " 0 : sync @ " << tbuf
              << format("%0.8lfs", ((double)gmt.tm_sec+frac_sec)) << " [GMT]" << " ;";
    }
    return reply.str();
}


// report time of last generated disk-frame
// DOES NO CHECK AT ALL if a recording is running!
string dot_fn(bool q, const vector<string>& args, runtime& rte) {
    ioboard_type& iob( rte.ioboard );
    ostringstream reply;

	reply << "!" << args[0] << (q?('?'):('='));
    if( !q ) {
        reply << " 4 : Only available as query ;";
        return reply.str();
    }

    // Good, fetch the hdrwords from the last generated DISK-FRAME
    // and decode the hdr.
    // HDR2:   JJJSSSSS   [day-of-year + seconds within day]
    // HDR3:   SSSS****   [fractional seconds]
    //    **** = 16bit CRC
    double       h, m, s;
    unsigned int doy;
    unsigned int hdr2 = ((*iob[mk5breg::DIM_HDR2_H]<<16)|(*iob[mk5breg::DIM_HDR2_L]));
    unsigned int hdr3 = ((*iob[mk5breg::DIM_HDR3_H]<<16)|(*iob[mk5breg::DIM_HDR3_L]));

    // hdr2>>(5*4) == right-shift hdr2 by 5BCD digits @ 4bits/BCD digit
    doy = unbcd((hdr2>>(5*4)));
    s    = (double)unbcd(hdr2&0x000fffff) + (double)unbcd(hdr3>>(4*4));
    h    = (unsigned int)(s/3600.0);
    s   -= (h*3600);
    m    = (unsigned int)(s/60.0);
    s   -= (m*60);

    // Now form the whole reply
    reply << " = 0 : "
          << doy << " "  // day-of-year
          << h << ":" << m << ":" << s << " " // time
          << ";";
    return reply.str();
}



// Set up the Mark5B/DIM input section to:
//   * sync to 1PPS (if a 1PPS source is set)
//   * set the time at the next 1PPS
//   * start generating on the next 1PPS
// Note: so *if* this function executes completely,
// it will start generating diskframes.
//
// This is done to ascertain the correct relation
// between DOT & data.
//
// dfhg = disk-frame-header-generator
//
// 'maxsyncwait' is the amount of time in seconds the system
// should at maximum wait for a 1PPS to appear.
// Note: if you said "1pps_source=none" then this method
// doesn't even try to wait for a 1pps, ok?
void start_mk5b_dfhg( runtime& rte, double maxsyncwait ) {
    const double    syncwait( maxsyncwait ); // Max. time to wait for 1PPS
    const double    minttns( 0.7 ); // minimum time to next second (in seconds)
    // (best be kept >0.0 and <1.0 ... )

    // Okie. Now it's time to start prgrm'ing the darn Mk5B/DIM
    // This is a shortcut: we rely on the Mk5's clock to be _quite_
    // accurate. We have to set the DataObservingTime at the next 1PPS
    // before we kick off the data-frame-header-generator.
    // Make sure we are not too close to the next integral second:
    // we need some processing time (computing JD, transcode to BCD
    // write into registers etc).
    time_t                      tmpt;
    double                      ttns; // time-to-next-second, delta-t
    double                      mjd;
    struct tm                   gmtnow;
    unsigned int                tmjdnum; // truncated MJD number
    unsigned int                nsssomjd;// number of seconds since start of mjd
    struct timeval              localnow;
    mk5b_inputmode_type         curipm;
    mk5breg::regtype::base_type time_h, time_l;

    // Ere we start - see if the 1PPS is actwerly zynched!
    // That is to say: we get the current inputmode and see
    // if there is a 1PPS source selected. If the PPS source is 'None',
    // obviously, there's little point in trying to zynkronize!
    rte.get_input( curipm );

    // Trigger reset of all DIM statemachines. As per
    // the docs, this 'does not influence any settable
    // DIM parameter' (we hope)
    rte.ioboard[ mk5breg::DIM_RESET ] = 1;
    rte.ioboard[ mk5breg::DIM_RESET ] = 0;
    // selpps=0 => No PPS source
    if( curipm.selpps ) {
        double         dt;
        struct timeval start;
        struct timeval end;

        // Pulse SYNCPPS to trigger zynchronization attempt!
        rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 1;
        rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 0;

        // now wait [for some maximum amount of time]
        // for SUNKPPS to transition to '1'
        dt = 0.0;
        ::gettimeofday(&start, 0);
        do {
            if( *rte.ioboard[mk5breg::DIM_SUNKPPS] )
                break;
            // Ok, SUNKPPS not 1 yet.
            // sleep a bit and retry
            usleep(10);
            ::gettimeofday(&end, 0);
            dt = ((double)end.tv_sec + (double)end.tv_usec/1.0e6) -
                ((double)start.tv_sec + (double)start.tv_usec/1.0e6);
        } while( dt<syncwait );

        // If dt>=syncwait, this indicates we don't have a synched 1PPS signal?!
        ASSERT2_COND( dt<syncwait, SCINFO(" - 1PPS failed to sync"));
    }

    // As per Mark5B-DIM-Registers.pdf Sec. "Typical sequence of operations":
    rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 1;
    rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 0;

    // Great. Now wait until we reach a time which is sufficiently before 
    // the next integral second
    do {
        ::gettimeofday(&localnow, 0);
        // compute time-to-next-(integral)second
        ttns = 1.0 - (double)(localnow.tv_usec/1.0e6);
    } while( ttns<minttns );

    // Good. Now be quick about it.
    // We know what the DOT will be (...) at the next 1PPS.
    // Transform localtime into GMT, get the MJD of that,
    // transform that to "VLBA-JD" (MJD % 1000) and finally
    // transform *that* into B(inary)C(oded)D(ecimal) and
    // write it into the DIM
    // Note: do NOT forget to increment the tv_sec value 
    // because we need the next second, not the one we're in ;)
    // and set the tv_usec value to '0' since ... well .. it
    // will be the time at the next 1PPS ...
    tmpt = (time_t)(localnow.tv_sec + 1);
    ::gmtime_r( &tmpt, &gmtnow );

    // Get the MJD daynumber
    //mjd = tm2mjd( gmtnow );
    mjd = jdboy( gmtnow.tm_year+1900 ) + gmtnow.tm_yday;
    DEBUG(2, "Got mjd for next 1PPS: " << mjd << endl);
    tmjdnum  = (((unsigned int)::floor(mjd)) % 1000);
    nsssomjd = gmtnow.tm_hour * 3600 + gmtnow.tm_min*60 + gmtnow.tm_sec;

    // Now we must go to binary coded decimal
    unsigned int t1, t2;
    t1 = bcd( tmjdnum );
    // if we multiply nseconds-since-start-etc by 1000
    // we fill the 8 bcd-digits nicely
    // [there's ~10^5 seconds in a day]
    // (and we could, if nsssomjd were 'double', move to millisecond
    // accuracy)
    t2 = bcd( nsssomjd * 1000 );
    // Transfer to the correct place in the start_time
    // JJJS   SSSS
    // time_h time_l
    time_h  = ((mk5breg::regtype::base_type)(t1 & 0xfff)) << 4;

    // Get the highest bcd digit from the 'seconds-since-start-of-mjd'
    // and move it into the lowest bcd of the high-word of START_TIME
    time_h |= (mk5breg::regtype::base_type)(t2 >> ((2*sizeof(t2)-1)*4));

    // the four lesser most-significant bcd digits of the 
    // 'seconds-since-start etc' go into the lo-word of START_TIME
    // This discards the lowest three bcd-digits.
    time_l  = (mk5breg::regtype::base_type)(t2 >> ((2*sizeof(t2)-5)*4));

    DEBUG(2, "Writing BCD StartTime H:" << hex_t(time_h) << " L:" << hex_t(time_l) << endl);

    // Fine. Bung it into the DIM
    rte.ioboard[ mk5breg::DIM_STARTTIME_H ] = time_h;
    rte.ioboard[ mk5breg::DIM_STARTTIME_L ] = time_l;

    // Now we issue a SETUP, wait for at least '135 data-clock-cycles'
    // before releasing it. We'll approximate this by just sleeping
    // 10ms.
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 1;
    ::usleep( 10000 );
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 0;

    // Weehee! Start the darn thing on the next PPS!
    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 1;

    return;
}









//
//
//    HERE we build the actual command-maps
//
//
const mk5commandmap_type& make_mk5a_commandmap( void ) {
    static mk5commandmap_type mk5commands = mk5commandmap_type();

    if( mk5commands.size() )
        return mk5commands;

    // Fill the map!
    pair<mk5commandmap_type::iterator, bool>  insres;

    // disk2net
    insres = mk5commands.insert( make_pair("disk2net", disk2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command disk2net into commandmap");

    // in2net + in2fork [same function, different behaviour]
    insres = mk5commands.insert( make_pair("in2net", in2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command in2net into commandmap");
    insres = mk5commands.insert( make_pair("in2fork", in2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command in2fork into commandmap");

    insres = mk5commands.insert( make_pair("record", in2disk_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command record/in2disk into commandmap");

    // net2out + net2dis [same function, different behaviour]
    insres = mk5commands.insert( make_pair("net2out", net2out_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command net2out into commandmap");
    insres = mk5commands.insert( make_pair("net2disk", net2out_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command net2disk into commandmap");

    // net_protocol
    insres = mk5commands.insert( make_pair("net_protocol", net_protocol_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command net_protocol into commandmap");

    // mode
    insres = mk5commands.insert( make_pair("mode", mk5a_mode_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command mode into commandmap");

    // play
    insres = mk5commands.insert( make_pair("play", disk2out_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command play into commandmap");

    // play_rate
    insres = mk5commands.insert( make_pair("play_rate", playrate_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command play_rate into commandmap");

    // status
    insres = mk5commands.insert( make_pair("status", status_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command status into commandmap");

    // dtsid
    insres = mk5commands.insert( make_pair("dts_id", dtsid_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dts_id into commandmap");

    // task_id
    insres = mk5commands.insert( make_pair("task_id", task_id_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command task_id into commandmap");

    // skip
    insres = mk5commands.insert( make_pair("skip", skip_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command skip into commandmap");

    // Not official mk5 commands but handy sometimes anyway :)
    insres = mk5commands.insert( make_pair("dbg", debug_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dbg into commandmap");

    insres = mk5commands.insert( make_pair("reset", reset_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command reset into commandmap");

    insres = mk5commands.insert( make_pair("tstat", tstat_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command tstat into commandmap");

    insres = mk5commands.insert( make_pair("netstat", netstat_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command netstat into commandmap");

    insres = mk5commands.insert( make_pair("mtu", mtu_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command mtu into commandmap");

    insres = mk5commands.insert( make_pair("evlbi", evlbi_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command evlbi into commandmap");

    insres = mk5commands.insert( make_pair("ipd", interpacketdelay_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command ipd into commandmap");

    insres = mk5commands.insert( make_pair("pdr", packetdroprate_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command pdr into commandmap");

    insres = mk5commands.insert( make_pair("udphelper", udphelper_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command udphelper into commandmap");

    insres = mk5commands.insert( make_pair("scandir", scandir_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command scandir into commandmap");

    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
    return mk5commands;
}

// Build the Mk5B DIM commandmap
const mk5commandmap_type& make_dim_commandmap( void ) {
    static mk5commandmap_type mk5commands = mk5commandmap_type();

    if( mk5commands.size() )
        return mk5commands;

    // Fill the map!
    pair<mk5commandmap_type::iterator, bool>  insres;

    // Hardware-specific functions
    insres = mk5commands.insert( make_pair("led", led_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command led into DIMcommandmap");

    insres = mk5commands.insert( make_pair("mode", mk5bdim_mode_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command mode into DIMcommandmap");

    insres = mk5commands.insert( make_pair("clock_set", clock_set_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command clock_set into DIMcommandmap");

    insres = mk5commands.insert( make_pair("in2net", dim2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command in2net into DIMcommandmap");

    insres = mk5commands.insert( make_pair("record", in2disk_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command record/in2disk into DIMcommandmap");

    insres = mk5commands.insert( make_pair("1pps_source", pps_source_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command 1pps_source into DIMcommandmap");

    // report PPS sync state/force PPS resync
    insres = mk5commands.insert( make_pair("pps", pps_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command pps into DIMcommandmap");

    // report last time generated by the DFHG
    insres = mk5commands.insert( make_pair("dot", dot_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dot into DIMcommandmap");

    // These commands are hardware-agnostic
    insres = mk5commands.insert( make_pair("skip", skip_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command skip into DIMcommandmap");

    insres = mk5commands.insert( make_pair("status", status_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command status into DIMcommandmap");

    // dtsid
    insres = mk5commands.insert( make_pair("dts_id", dtsid_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dts_id into DIMcommandmap");

    insres = mk5commands.insert( make_pair("tstat", tstat_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command tstat into DIMcommandmap");

    insres = mk5commands.insert( make_pair("netstat", netstat_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command netstat into DIMcommandmap");

    insres = mk5commands.insert( make_pair("mtu", mtu_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command mtu into DIMcommandmap");

    insres = mk5commands.insert( make_pair("evlbi", evlbi_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command evlbi into DIMcommandmap");

    insres = mk5commands.insert( make_pair("ipd", interpacketdelay_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command ipd into DIMcommandmap");

    insres = mk5commands.insert( make_pair("pdr", packetdroprate_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command pdr into DIMcommandmap");

    insres = mk5commands.insert( make_pair("udphelper", udphelper_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command udphelper into DIMcommandmap");

    insres = mk5commands.insert( make_pair("net_protocol", net_protocol_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command net_protocol into DIMcommandmap");

    insres = mk5commands.insert( make_pair("scandir", scandir_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command scandir into DIMcommandmap");

    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
    return mk5commands;
}

const mk5commandmap_type& make_dom_commandmap( void ) {
    static mk5commandmap_type mk5commands = mk5commandmap_type();

    if( mk5commands.size() )
        return mk5commands;

    // Fill the map!
    pair<mk5commandmap_type::iterator, bool>  insres;

    insres = mk5commands.insert( make_pair("scandir", scandir_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command scandir into DOMcommandmap");

    // dtsid
    insres = mk5commands.insert( make_pair("dts_id", dtsid_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dts_id into DOMcommandmap");

    // taskid
    insres = mk5commands.insert( make_pair("task_id", task_id_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command task_id into DOMcommandmap");

    return mk5commands;
}
