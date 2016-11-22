// Copyright (C) 2007-2013 Harro Verkouter
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
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <scan_label.h>
#include <threadfns.h>
#include <limits.h>
#include <iostream>

using namespace std;


unsigned int bufarg_getbufsize(chain* c, chain::stepid s) {
    return c->communicate(s, &buffererargs::get_bufsize);
}


struct n2o_data_type {
    curry_type    oldthunk;
    ScanPointer   scanptr;
    chain::stepid servostep;
    chain::stepid netstep;
    chain::stepid diskstep;

    n2o_data_type() :
        servostep( chain::invalid_stepid ),
        netstep( chain::invalid_stepid ),
        diskstep( chain::invalid_stepid )
    {}
};

typedef per_runtime<n2o_data_type>  n2o_data_store;


void n2o_guard_function(runtime* rteptr, n2o_data_store::iterator p) {
    // Do automatic cleanup?
    try {
        n2o_data_type&   n2o( p->second);

        rteptr->processingchain.communicate(n2o.netstep, &close_filedescriptor);

        // If a servo step was inserted, we must put back the old thunk
        if( n2o.servostep!=chain::invalid_stepid ) {
            rteptr->set_bufsizegetter( n2o.oldthunk );
            n2o.oldthunk = curry_type();
        }
        n2o.servostep = chain::invalid_stepid;
        n2o.netstep   = chain::invalid_stepid;
        // do not set disk stepid to invalid just yet;
        // someone may be polling for the byte number to
        // see if the transfer has 'finished' (ie byte number
        // not incrementing any more)
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "net2out/disk guard caught an exception: " << e.what() << std::endl );
        rteptr->transfermode = no_transfer;
    }
    catch ( ... ) {
        DEBUG(-1, "net2out/disk guard caught an unknown exception" << std::endl );        
        rteptr->transfermode = no_transfer;
    }
}

void n2o_reset_lasthost(runtime* rteptr, const string oldhost) {
    rteptr->netparms.host = oldhost;
}


// set up net2out, net2disk, net2fork
string net2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
    static n2o_data_store n2o_data;
    // automatic variables
    const bool            is_mk5a( rte.ioboard.hardware() & ioboard_type::mk5a_flag );
    ostringstream         reply;
    const transfer_type   ctm( rte.transfermode ); // current transfer mode
    const transfer_type   rtm = string2transfermode(args[0]); // requested transfer mode
    const bool            disk = todisk(rtm);
    const bool            out  = toio(rtm);

    EZASSERT2(rtm==net2out || rtm==net2disk || rtm==net2fork, cmdexception,
              EZINFO("Requested transfermode " << args[0] << " not serviced by this function"));

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query always possible, command only if doing nothing or the requested
    // transfer mode == current transfer mode
    // Thus we're busy if:
    //      !qry && !(ctm==no_transfer || ctm==rtm)  =>
    //      !(qry || (ctm==no_transfer || ctm==rtm)) =>
    //      !(qry || ctm==no_transfer || ctm==rtm)
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==rtm))

    // Good. See what the usr wants
    if( qry ) {
        n2o_data_store::const_iterator  ptr = n2o_data.find( &rte );
        reply << " 0 : ";
        if( ctm!=rtm ) {
            // We're not doing the requested transfer so it gonna be
            // inactive per se
            reply << "inactive : 0 ";
            if( disk && ptr!=n2o_data.end() )
                reply << ": " << ptr->second.scanptr.name();
        } else {
            // Assert that we have n2o_data
            EZASSERT( ptr!=n2o_data.end(), cmdexception );
            const n2o_data_type& n2oref( ptr->second );

            // Display status
            if( rte.transfersubmode&run_flag )
                reply << "active";
            else if( rte.transfersubmode&wait_flag )
                reply << "waiting";
            else
                reply << rte.transfersubmode  /*"?"*/;

            // If doing something to disk, add scan info
            if( disk )
                reply << " : " << n2oref.scanptr.index() << " : " << n2oref.scanptr.name();

            // jive5ab addition: number of bytes to disk [fifo, really]
            reply << " : " << rte.statistics.counter(n2oref.diskstep);
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

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
        // net2out=open[:<ipnr>][:<nbytes>]
        //   net2out=open;        // sets up receiving socket based on net_protocol
        //   net2out=open:<ipnr>; // implies either 'rtcp' if net_proto==rtcp,
        //        connects to <ipnr>. If netproto!=rtcp, sets up receiving socket to
        //        join multicast group <ipnr>, if <ipnr> is multicast
        //   <nbytes> : optional 3rd argument. if set and >0 it
        //              indicates the amount of bytes that will
        //              be buffered in memory before data will be
        //              passed further downstream
        //
        // net2disk MUST have a scanname and may have an optional
        // ipaddress for rtcp or connecting to a multicast group:
        //    net2disk=open:<scanname>[:<ipnr>]
        //
        // net2fork is a combination of net2disk and net2out:
        //    net2fork=open:<scanname>[:<ipnr>][:<nbytes>]
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            XLRCODE(SSHANDLE        ss( rte.xlrdev.sshandle() ));
            const string            arg2( OPTARG(2, args) );
            const string            nbyte_str( OPTARG((rtm == net2fork ? 4 : 3), args) );
            const string            oldhost( rte.netparms.host );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            // If we're doing net2out on a Mark5B(+) we
            // cannot accept Mark4/VLBA data.
            // A Mark5A+ can accept Mark5B data ("mark5a+ mode")
            if( rtm==net2out && !is_mk5a )  {
                EZASSERT2(rte.trackformat()==fmt_mark5b, cmdexception,
                          EZINFO("net2out on Mark5B can only accept Mark5B data"));
            }

            // Constrain the transfer sizes based on the three basic
            // parameters
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            throw_on_insane_netprotocol(rte);

            // depending on disk or out, the 2nd arg is optional or not
            if( disk && (args.size()<3 || args[2].empty()) )
                THROW_EZEXCEPT(cmdexception, " no scanname given");

            // save the current host (already done above, 'oldhost'). clear the value.
            // we'll put the original value back later. See registered final
            // function "n2o_reset_lasthost()" below.
            rte.netparms.host.clear();

            // we may write our own value in there (optional 2nd parameter)
            // but most of the times it must be empty. 
            // getsok() uses that value to ::bind() to if it's
            // non-empty. For us that's only important if it's a
            // multicast we want to receive.

            // pick up optional ip-address, if given.
            if( (!disk && args.size()>2) || (disk && args.size()>3) )
                rte.netparms.host = args[(unsigned int)(disk?3:2)];

            // also, if writing to disk, we should ascertain that
            // the disks are ready-to-go
            if( disk ) {
                S_DIR         disk_dir;
                S_DEVINFO     devInfo;

                ::memset(&disk_dir, 0, sizeof(S_DIR));
                ::memset(&devInfo, 0, sizeof(S_DEVINFO));

                // Verify that there are disks on which we *can*
                // record!
                XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                EZASSERT( devInfo.NumDrives>0, cmdexception );

                // and they're not full or writeprotected
                XLRCALL( ::XLRGetDirectory(ss, &disk_dir) );
                EZASSERT( !(disk_dir.Full || disk_dir.WriteProtected), cmdexception );
            }

            // Good, all precondtions have been tested, now it's time to
            // make sure we have a n2o_data thingy to store stuff into
            n2o_data_store::iterator    ptr = n2o_data.insert( make_pair(&rte, n2o_data_type()) ).first;
            n2o_data_type&              n2o( ptr->second );

            // Start building the chain

            // Read from network
            n2o.netstep = c.add(&netreader, 32, &net_server, networkargs(&rte));
            c.register_cancel(n2o.netstep, &close_filedescriptor);

            // if necessary, decompress
            if( rte.solution )
                c.add(&blockdecompressor, 10, &rte);

            // optionally buffer
            // for net2out we may optionally have to buffer 
            // an amount of bytes. Check if <nbytes> is
            // set and >0
            //  note: (!a && !b) <=> !(a || b)
            if( !(disk || nbyte_str.empty()) || (rtm == net2fork) ) {
                unsigned long b = 0;

                if( !nbyte_str.empty() ) {
                    char*         eocptr;

                    // strtoul(3)
                    //   * before calling, set errno=0
                    //   -> result == ULONG_MAX + errno == ERANGE
                    //        => input value too big
                    //   -> result == 0 + errno == EINVAL
                    //        => no conversion whatsoever
                    // !(a && b) <=> (!a || !b)
                    errno = 0;
                    b     = ::strtoul(nbyte_str.c_str(), &eocptr, 0);
                    EZASSERT2( !(b==ULONG_MAX && errno==ERANGE) &&
                                 !(b==0         && errno==EINVAL) &&
                                 eocptr!=nbyte_str.c_str() &&
                                 *eocptr=='\0' && 
                                 b>0 && b<UINT_MAX,
                               cmdexception,
                               EZINFO("Invalid amount of bytes " << nbyte_str << " (1 .. " << UINT_MAX << ")") );
                }

                // We now know that 'b' has a sane value 
                n2o.servostep = c.add(&bufferer, 10, buffererargs(&rte, (unsigned int)b));
                    
                // Now install a 'get_buffer_size()' thunk in the rte
                // We store the previous one so's we can put it back
                // when we're done.
                n2o.oldthunk = rte.set_bufsizegetter( makethunk(&bufarg_getbufsize, n2o.servostep) );
            }

            // and write the result
            n2o.diskstep = c.add(fifowriter, &rte);
            // done :-)

            // switch on recordclock, not necessary for net2disk
            if( out && is_mk5a )
                rte.ioboard[ mk5areg::notClock ] = 0;

            // now program the streamstor to record from PCI -> FPDP
            XLRCALL( ::XLRSetMode(ss, (CHANNELTYPE)(disk?(out?SS_MODE_FORK:SS_MODE_SINGLE_CHANNEL):SS_MODE_PASSTHRU)) );
            XLRCALL( ::XLRClearChannels(ss) );
            XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

            // program where the output should go
            if( out ) {
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
            }
            if( disk ) {
                // must prepare the userdir
                string scan_label = scan_label::create_scan_label(scan_label::command, arg2);
                n2o.scanptr = rte.xlrdev.startScan( scan_label );
                // and start the recording
                XLRCALL( ::XLRAppend(ss) );
            }
            else {
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );
            }
                
            rte.transfersubmode.clr_all();
            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing
            // Do this before we actually run the chain - something may
            // go wrong and we must cleanup later
            rte.transfermode    = rtm;

            // Register some final function(s)
            c.register_final(&n2o_reset_lasthost, &rte, oldhost);
            c.register_final(&n2o_guard_function, &rte, ptr);

            // install and run the chain
            rte.processingchain = c;
            rte.processingchain.run();

            // Set variable block size, if appropriate
            rte.processingchain.communicate(n2o.netstep, &fdreaderargs::set_variable_block_size,
                                            (!rte.solution || !dataformat.valid()));

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
            string   error_message;
            try {
                // switch off recordclock
                if( out && is_mk5a )
                    rte.ioboard[ mk5areg::notClock ] = 1;
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop record clock: ") + e.what();
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop record clock, unknown exception");
            }

            try {
                // Ok. stop the threads
                rte.processingchain.stop();
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop processing chain: ") + e.what();
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop processing chain, unknown exception");
            }

            try {
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
                    n2o_data_store::iterator ptr = n2o_data.find( &rte );
                    if( ptr!=n2o_data.end() ) {
                        rte.xlrdev.finishScan( ptr->second.scanptr );

                        // comply with documentation ... (22 Nov 2016 ... 8-/ )
                        rte.pp_current   = ptr->second.scanptr.start();
                        rte.pp_end       = ptr->second.scanptr.start() + ptr->second.scanptr.length();
                        rte.current_scan = rte.xlrdev.nScans() - 1;
                    } else
                        error_message += " : Failed to finish scan (no runtime-specific data?!)";
                }

                if ( disk && (rte.disk_state_mask & runtime::record_flag) ) {
                    rte.xlrdev.write_state( "Recorded" );
                }
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop streamstor: ") + e.what();
                if( disk ) {
                    rte.xlrdev.stopRecordingFailure();
                }
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop streamstor, unknown exception");
                if( disk ) {
                    rte.xlrdev.stopRecordingFailure();
                }
            }
                
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

            if ( error_message.empty() ) {
                reply << " 0 ;";
            }
            else {
                reply << " 4" << error_message << " ;";
            }
        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( args[1]=="skip" && rtm==net2fork ) {
        const string  bytes_string( OPTARG(2, args) );

        recognized = true;

        if( bytes_string.empty() ) {
            reply << " 8 : skip requires an extra argument (number of bytes) ;";
            return reply.str();
        }

        char*   endptr;
        int64_t n_bytes = strtoll(bytes_string.c_str(), &endptr, 0);
            
        if( *endptr=='\0' && 
            ((n_bytes!=std::numeric_limits<int64_t>::max() && n_bytes!=std::numeric_limits<int64_t>::min()) || errno!=ERANGE) ) {
            // Ok, we have parsed a valid number. Now we MUST find the
            // runtime specific information!
            n2o_data_store::iterator ptr = n2o_data.find( &rte );

            EZASSERT2(ptr!=n2o_data.end(), cmdexception, EZINFO("Failed to find runtime specific information"));

            if( n_bytes<0 ) {
                rte.processingchain.communicate( ptr->second.servostep, &buffererargs::add_bufsize, (unsigned int)-n_bytes );
            }
            else {
                rte.processingchain.communicate( ptr->second.servostep, &buffererargs::dec_bufsize, (unsigned int)n_bytes );
            }
            reply << " 0 ;";
        }
        else {
            reply << " 8 : failed to parse number of bytes from '" << bytes_string << "' ;";
        }

    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
