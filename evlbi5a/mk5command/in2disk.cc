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
#include <dotzooi.h>
#include <scan_label.h>
#include <iostream>
#include <chain.h>

using namespace std;

typedef per_runtime<string> error_cache_type;

// 29-Jun-2014 It was discovered that pressing "^C" would stop the recording
//             but left the Mark5B FHG running. This FHG status is kept
//             across restarts of jive5ab/DIMino. The side effect of this
//             being that the next invocation will see FHG=on and this will
//             affect the "dot?" reading as the code will take the reading
//             from the hardware even though we're not recording!
//             Fix this by having a chain which does the cleanup - i.e.
//             switch the FHG off

// These fake steps will basically do nothing.
void fakeRecordS(outq_type<bool>* /*oqptr*/, sync_type<bool>* args) {
    // Just wait until we're cancelled
    args->lock();
    while( !args->cancelled )
        args->cond_wait();
    args->unlock();
}

void fakeRecordE(inq_type<bool>* iqptr) {
    bool dummy;
    // Producer will never push so this one will
    // block until we're cancelled
    while( iqptr->pop(dummy) ) {}
}

// This is why we're going through the motions of
// creating a chain: being able to register this
// one as cleanup!
struct cleanup_args_type {
    runtime*                    rteptr;
    error_cache_type::iterator  errMsgPtr;

    cleanup_args_type(runtime* r, error_cache_type::iterator emp):
        rteptr( r ), errMsgPtr( emp )
    { EZASSERT2(rteptr, cmdexception, EZINFO("null runtime pointer is disallowed")); }

    private:
        cleanup_args_type();
};

void cleanupRecord(cleanup_args_type cat) {
    runtime*                    rteptr = cat.rteptr;
    ioboard_type::iobflags_type hardware( rteptr->ioboard.hardware() );
    const bool                  m5a    = (hardware & ioboard_type::mk5a_flag);
    const bool                  m5bdim = (hardware & ioboard_type::dim_flag);

    if( m5a || m5bdim ) {
        try {
            DEBUG(2, "cleanupRecord: stop I/O board => streamstor" << endl);
            // Depending on the actual hardware ...
            // stop transferring from I/O board => streamstor
            if( m5a ) {
                rteptr->ioboard[ mk5areg::notClock ] = 1;
            } else {
                // we want to end at a whole second, first pause the ioboard
                rteptr->ioboard[ mk5breg::DIM_PAUSE ] = 1;

                // wait one second, to be sure we got an 1pps
                pcint::timeval_type start( pcint::timeval_type::now() );
                pcint::timediff     tdiff = pcint::timeval_type::now() - start;
                while ( tdiff < 1 ) {
                    ::usleep( (unsigned int)((1 - tdiff) * 1.0e6) );
                    tdiff = pcint::timeval_type::now() - start;
                }

                // then stop the ioboard
                rteptr->ioboard[ mk5breg::DIM_STARTSTOP ] = 0;
                rteptr->ioboard[ mk5breg::DIM_PAUSE ] = 0;
            }
            DEBUG(2, "cleanupRecord: done" << endl);
        }
        catch ( std::exception& e ) {
            cat.errMsgPtr->second += string(" : Failed to stop I/O board : ")+e.what();
            DEBUG(-1, "cleanupRecord: Failed to stop I/O board: " << e.what() << endl);
        }
        catch ( ... ) {
            cat.errMsgPtr->second += string(" : Failed to stop I/O board : unknown exception");
            DEBUG(-1, "cleanupRecord: Failed to stop I/O board: unknown exception" << endl);
        }
    }
}


// Really, in2disk is 'record'. But in lieu of naming conventions ...
// the user won't see this name anyway :)
// Note: do not stick this one in the Mark5B/DOM commandmap :)
// Oh well, you'll get exceptions when trying to execute then
// anyway
// 31-May-2013  HV: Added support for Mark5C
string in2disk_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // This points to the scan being recorded, if any
    static ScanPointer      curscanptr;    
    static error_cache_type errMsg;
    // automatic variables
    ostringstream               reply;
    const transfer_type         ctm( rte.transfermode );
    ioboard_type::iobflags_type hardware( rte.ioboard.hardware() );

    // Verify we are called on an actual *recorder* 
    ASSERT_COND( (hardware&ioboard_type::mk5a_flag ||
                  hardware&ioboard_type::dim_flag ||
                  hardware&ioboard_type::mk5c_flag) );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query should always be available, command only if
    // we're not doing anything or already recording
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==in2disk))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode!=in2disk ) {
            reply << "off";
        } else {
            // 4 possible status messages: on, halted, throttled, overflow and waiting
            S_DEVSTATUS dev_status;
            XLRCALL( ::XLRGetDeviceStatus(rte.xlrdev.sshandle(), &dev_status) );
            if ( dev_status.Recording ) {
                if ( rte.ioboard.hardware()&ioboard_type::mk5a_flag ) {
                    // recording is on, check for throttled (Mark5A checks that before overflow)
                    outputmode_type mode;
                    rte.get_output(mode);
                    rte.get_output(mode); // throttled seems to be always on the first time mode is read from the ioboard
                    if ( mode.throttle ) {
                        reply << "throttled";
                    }
                    else if ( dev_status.Overflow[0] ) {
                        reply << "overflow";
                    }
                    else {
                        reply << "on";
                    }
                } else if ( rte.ioboard.hardware()&ioboard_type::dim_flag ) {
                    // Check Mark5B status
                    if ( dev_status.Overflow[0] || rte.ioboard[mk5breg::DIM_OF] ) {
                        reply << "overflow";
                    }
                    else {
                        reply << "on";
                    }
                } else {
                    // Mark5C status
                    reply << "on";
                }
            }
            else {
                // in recording transfer, but not recording, check which error
                S_DIR dir;
                XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &dir) );
                if ( dir.Full ) {
                    reply << "halted";
                }
                else {
                    reply << "waiting";
                }
            }
            // add the scan name
            reply << " : " << rte.xlrdev.nScans() << " : " << ROScanPointer::strip_asterisk( curscanptr.name() );
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
            chain         c;
            S_DIR         disk;
            string        scan( args[2] );
            string        experiment( OPTARG(3, args) );
            string        station( OPTARG(4, args) );
            string        scanlabel;
            XLRCODE(SSHANDLE    ss( rte.xlrdev.sshandle() ));
            XLRCODE(CHANNELTYPE ch( ((hardware&ioboard_type::mk5c_flag)?CHANNEL_10GIGE:CHANNEL_FPDP_TOP) ) );
            S_DEVINFO     devInfo;

            // If we attempt to record on a Mark5B(+) we must
            // meet these preconditions
            if( hardware&ioboard_type::mk5b_flag ) {
                dot_type            dotclock = get_dot();
                mk5b_inputmode_type curipm;

                // Do not allow to record without a 1PPS source or if it isn't synced
                rte.get_input( curipm );
                EZASSERT2( curipm.selpps && *rte.ioboard[mk5breg::DIM_SUNKPPS], cmdexception,
                           EZINFO("There is not 1PPS source set yet or it is not synchronized") );

                // Verify that we do have a DOT!
                EZASSERT2(dotclock.dot_status==dot_ok, cmdexception, EZINFO("DOT fail - " << dotstatus2str(dotclock.dot_status)));
            }

            ::memset(&devInfo, 0, sizeof(S_DEVINFO));

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

            scanlabel = scan_label::create_scan_label(scan_label::command, scan, experiment, station);
            
            // Depending on Mk5A or Mk5B/DIM ...
            // switch off clock (mk5a) or
            // stop the DFH-generator
            if( hardware&ioboard_type::mk5a_flag )
                rte.ioboard[ mk5areg::notClock ] = 1;
            else if( hardware&ioboard_type::dim_flag )
                rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;

            // Already program the streamstor, do not
            // start Recording otherwise we can't read/write
            // the UserDirectory.
            // Let it record from FPDP -> Disk
            XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
            XLRCALL( ::XLRClearChannels(ss) );
            XLRCALL( ::XLRBindOutputChannel(ss, 0) );
            XLRCALL( ::XLRBindInputChannel(ss, ch) );
            XLRCALL( ::XLRSelectChannel(ss, ch) );

            // HV: Take care of Amazon - as per Conduant's
            //     suggestion. Mind you, this should NOT be
            //     done on the 5C
            XLRCODE( UINT     u32recvMode;)
            XLRCODE( UINT     u32recvOpt;)

            if( (hardware&ioboard_type::mk5c_flag)==false ) {
                if( rte.xlrdev.boardGeneration()<4 ) {
                    // This is either a XF2/V100/VXF2
                    XLRCODE(u32recvMode = SS_FPDP_RECVMASTER;)
                    XLRCODE(u32recvOpt  = SS_OPT_FPDPNRASSERT;)
                } else {
                    // Amazon or Amazon/Express
                    XLRCODE(u32recvMode = SS_FPDPMODE_RECVM;)
                    XLRCODE(u32recvOpt  = SS_DBOPT_FPDPNRASSERT;)
                }
                XLRCALL( ::XLRSetDBMode(ss, u32recvMode, u32recvOpt) );
            }

            curscanptr = rte.xlrdev.startScan( scanlabel );

            // Great, now start recording & kick off the I/O board
            //XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 0) );
            XLRCALL( ::XLRAppend(ss) );

            if( hardware&ioboard_type::mk5a_flag )
                rte.ioboard[ mk5areg::notClock ] = 0;
            else if( hardware&ioboard_type::dim_flag )
                start_mk5b_dfhg( rte );

            // Update global transferstatus variables to
            // indicate what we're doing
            rte.statistics.clear();
            rte.transfermode    = in2disk;
            rte.transfersubmode.clr_all();
            // in2disk is running immediately
            rte.transfersubmode |= run_flag;

            //
            // Create & run fake chain
            // 
            c.add( &fakeRecordS, 1, false );
            c.add( &fakeRecordE );
           
            // make sure error message string (1) exists and (2) is empty
            errMsg[&rte] = string(); 
            c.register_final(&cleanupRecord, cleanup_args_type(&rte, errMsg.find(&rte)));
            rte.processingchain = c;
            rte.processingchain.run();
            
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }
    if( args[1]=="off" ) {
        recognized = true;
        // only allow if transfermode==in2disk && submode has the run flag
        if( rte.transfermode==in2disk ) {
            string error_message;

            // Stop the I/O board sending data
            rte.processingchain.stop();
            rte.processingchain = chain();

            // pick up error message yielded by
            // cleanupRecord final function (if any)
            error_message = errMsg[&rte];
          
            // Are we actually running? 
            if( rte.transfersubmode&run_flag ) { 
                try {
                    // stop the device
                    // As per the SS manual need to call 'XLRStop()'
                    // twice: once for stopping the recording
                    // and once for stopping the device altogether?
                    XLRCODE(SSHANDLE handle = rte.xlrdev.sshandle());
                    XLRCALL( ::XLRStop(handle) );
                    if( rte.transfersubmode&run_flag )
                        XLRCALL( ::XLRStop(handle) );

                    XLRCALL( ::XLRClearChannels(handle) );
                    XLRCALL( ::XLRBindOutputChannel(handle, 0) );
                    
                    rte.xlrdev.finishScan( curscanptr );

                    if ( rte.disk_state_mask & runtime::record_flag ) {
                        rte.xlrdev.write_state( "Recorded" );
                    }

                    rte.pp_current = curscanptr.start();
                    rte.pp_end = curscanptr.start() + curscanptr.length();
                    rte.current_scan = rte.xlrdev.nScans() - 1;
                }
                catch ( std::exception& e ) {
                    error_message += string(" : Failed to stop streamstor: ") + e.what();
                    rte.xlrdev.stopRecordingFailure();
                }
                catch ( ... ) {
                    error_message += string(" : Failed to stop streamstor, unknown exception");
                    rte.xlrdev.stopRecordingFailure();
                }
            }

            // reset global transfermode variables 
            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();

            if ( error_message.empty() ) {
                reply << " 0 ;";
            }
            else {
                reply << " 4" << error_message << " ;";
            }
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

    return reply.str();
}
