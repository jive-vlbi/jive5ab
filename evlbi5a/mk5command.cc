// implementation of the commands
#include <mk5command.h>
#include <dosyscall.h>
#include <threadfns.h>
#include <playpointer.h>
#include <evlbidebug.h>
#include <getsok.h>
#include <streamutil.h>

// for setsockopt
#include <sys/types.h>
#include <sys/socket.h>

// and for "struct timeb"/ftime()
#include <sys/timeb.h>

using namespace std;


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
                string       proto( rte.netparms.get_protocol() );
                unsigned int olen( sizeof(rte.netparms.sndbufsize) );

                // make sure we recognize the protocol
                ASSERT_COND( ((proto=="udp")||(proto=="tcp")) );

                // good. pick up optional hostname/ip to connect to
                if( args.size()>2 )
                    rte.lasthost = args[2];

                // create socket and connect 
                s = getsok(rte.lasthost, 2630, proto);

                // Set sendbufsize
                ASSERT_ZERO( ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sbuf, olen) );

                // before kicking off the threads, transfer some important variables
                // across. The playpointers will be done later on
                rte.fd     = s;

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
            // set
            if( rte.transfermode==disk2net && (rte.transfersubmode&run_flag)==false ) {
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
                // now broadcast the startcondition
                PTHREAD2_CALL( ::pthread_cond_broadcast(rte.condition),
                               ::pthread_mutex_unlock(rte.mutex) );

                // And we're done, we may release the mutex
                PTHREAD_CALL( ::pthread_mutex_unlock(rte.mutex) );
                // indicate running state
                rte.transfersubmode.clr( wait_flag ).set( run_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or disk2net, nothing else
                if( rte.transfermode==disk2net )
                    reply << " 6 : already running ;";
                else 
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

// set up net2out
string net2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
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

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=net2out ) {
        reply << " 1 : _something_ is happening and its NOT net2out!!! ;";
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
            if( rte.transfermode==no_transfer ) {
                bool             initstartval;
                SSHANDLE         ss( rte.xlrdev.sshandle() );
                const string&    proto( rte.netparms.get_protocol() );
                transfer_submode tsm;

                // for now, only accept tcp or udp
                ASSERT_COND( ((proto=="udp")||(proto=="tcp")) );

                // create socket and start listening.
                // If netparms.proto==tcp we put socket into the
                // rte's acceptfd field so it will do the waiting
                // for connect for us (and kick off the threads
                // as soon as somebody make a conn.)
                s = getsok(2630, proto);

                // switch on recordclock
                rte.ioboard[ mk5areg::notClock ] = 0;

                // now program the streamstor to record from PCI -> FPDP
                XLRCALL( ::XLRSetMode(ss, SS_MODE_PASSTHRU) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );

                // before kicking off the threads,
                // transfer some important variables across
                // and pre-set the submode. Will only be
                // set in the runtime IF the threads actually
                // seem to start ok. Otherwise the runtime
                // is left untouched
                initstartval = false;
                if( proto=="udp" ) {
                    rte.fd       = s;
                    rte.acceptfd = -1;
                    // udp threads don't have to "wait-for-start"
                    initstartval = true;
                    tsm.set( connected_flag ).set( run_flag );
                }
                else  {
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
                rte.transfermode    = net2out;
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

            // only accept this command if we're doing
            // net2out
            if( rte.transfermode==net2out ) {
                // switch off recordclock
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

                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;

                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing net2out yet ;";
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

string in2net_fn( bool qry, const vector<string>& args, runtime& rte ) {
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

                // switch off clock
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

                DEBUG(2,"connect: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspendf) << endl);
                notclock = 1;
                DEBUG(2,"connect: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspendf) << endl);

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

                // goodie. now start the threads disk2mem and mem2net!
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
            if( rte.transfermode==in2net && (rte.transfersubmode&run_flag)==false ) {
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspend  = rte.ioboard[ mk5areg::SF ];

                // After we've acquired the mutex, we may set the 
                // variable (start) to true, then broadcast the condition.
                PTHREAD_CALL( ::pthread_mutex_lock(rte.mutex) );

                // now switch on clock
                DEBUG(2,"on: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);
                notclock = 0;
                suspend  = 0;
                DEBUG(2,"on: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);
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
                // transfermode is either no_transfer or disk2net, nothing else
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
                ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
                ioboard_type::mk5aregpointer  suspend  = rte.ioboard[ mk5areg::SF ];

                // We don't have to get the mutex; we just turn off the 
                // record clock on the inputboard
                DEBUG(2,"off: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);
                notclock = 1;
                DEBUG(2,"off: notclock: " << hex_t(*notclock) << " SF: " << hex_t(*suspend) << endl);

                // indicate paused state
                rte.transfersubmode.clr( run_flag ).set( pause_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or disk2net, nothing else
                if( rte.transfermode==in2net )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing disk2net.
            // Don't care if we were running or not
            if( rte.transfermode==in2net ) {
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


// specialization for Mark5B
string mark5b_mode_fn( bool , const vector<string>& , runtime& ) {
    return "mode = 7 : ENOSYS (for Mark5B) ;";
}

// specialization for Mark5A(+)
string mark5a_mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
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
            reply << "!" << args[0] << "= 8 : ntrack out-of-range (" << args[2] << ") usefull range <0, 64] ;";
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

// This is merely a 'delegator' => looks at the hardware and
// dispatches to either the Mark5A or Mark5B version
string mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
    if( rte.ioboard.hardware()&ioboard_type::mk5a_flag )
        return mark5a_mode_fn(qry, args, rte);
    else if( rte.ioboard.hardware()&ioboard_type::mk5b_flag )
        return mark5b_mode_fn(qry, args, rte);

    // If we end up here, we don't know what hardware there is in this
    // Mark5. Tell caller so much ...
    ostringstream reply;
    reply << "!" << args[0] << (qry?('?'):('=')) << " 7 : Unsupported hardware "
          << rte.ioboard.hardware() << " ;";
    return reply.str();
}

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


//
//
//    HERE we build the actual map
//
//
const mk5commandmap_type& make_mk5commandmap( void ) {
    static mk5commandmap_type mk5commands = mk5commandmap_type();

    if( mk5commands.size() )
        return mk5commands;

    // Fill the map!
    pair<mk5commandmap_type::iterator, bool>  insres;

    // disk2net
    insres = mk5commands.insert( make_pair("disk2net", disk2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command disk2net into commandmap");

    // in2net
    insres = mk5commands.insert( make_pair("in2net", in2net_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command in2net into commandmap");

    // net2out
    insres = mk5commands.insert( make_pair("net2out", net2out_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command in2net into commandmap");

    // net_protocol
    insres = mk5commands.insert( make_pair("net_protocol", net_protocol_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command disk2net into commandmap");

    // mode
    insres = mk5commands.insert( make_pair("mode", mode_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command mode into commandmap");

    // play_rate
    insres = mk5commands.insert( make_pair("play_rate", playrate_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command playrate into commandmap");

    // status
    insres = mk5commands.insert( make_pair("status", status_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command status into commandmap");

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

    insres = mk5commands.insert( make_pair("led", led_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command led into commandmap");
    return mk5commands;
}
