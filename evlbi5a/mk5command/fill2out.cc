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
#include <iostream>

using namespace std;

typedef per_runtime<ScanPointer> scanpointer_type;

// The guard function which finalizes the "fill2disk" transfer
void fill2diskguard_fun(runtime* rteptr, scanpointer_type::iterator p) {
    try {
        DEBUG(3, "fill2disk guard function: transfer done" << endl);
        // apparently we need to call stop twice
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        
        // store the results in the user directory
        rteptr->xlrdev.finishScan( p->second );
        
        if ( rteptr->disk_state_mask & runtime::record_flag )
            rteptr->xlrdev.write_state( "Recorded" );

        // 02/Nov/2016 JonQ mentions that fill2disk doesn't
        //             set scan pointers as record=off does
        rteptr->pp_current   = p->second.start();
        rteptr->pp_end       = p->second.start() + p->second.length();
        rteptr->current_scan = rteptr->xlrdev.nScans() - 1;
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "fill2disk guard function/caught exception: " << e.what() << std::endl );
        rteptr->xlrdev.stopRecordingFailure();
    }
    catch ( ... ) {
        DEBUG(-1, "fill2disk guard function/caught unknown exception" << std::endl );        
        rteptr->xlrdev.stopRecordingFailure();
    }
    // No matter how we exited, this has to be done
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
}

void fill2outguard_fun(runtime* rteptr) {
    try {
        DEBUG(3, "fill2out guard function: transfer done" << endl);
        // apparently we need to call stop twice
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "fill2out guard function/caught exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "fill2out guard function/caught unknown exception" << std::endl );        
    }
    // No matter how we exited, this has to be done
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
}

// set up fill2out and fill2disk
string fill2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
    static scanpointer_type scanPointers;
    // automatic variables
    const bool              is_mk5a( rte.ioboard.hardware() & ioboard_type::mk5a_flag );
    ostringstream           reply;
    const transfer_type     ctm( rte.transfermode );             // current transfer mode
    const transfer_type     rtm( string2transfermode(args[0]) ); // requested  ,,     ,,

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Qry is always possible, command only if doing nothing
    // or already doing the requested transfer mode
    // !q && !(ctm==no_transfer || ctm==fill2out) =>
    // !(q || ctm==no_transfer || ctm==fill2out)
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==rtm))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : " << ((ctm==rtm) ? "active" : "inactive");
        // fill2disk is like 'record=on' and thus in the query reply
        // the scan name has to be inserted
        if( ctm==fill2disk )
            reply << " : " << scanPointers[&rte].name();
        // And insert the byte counter, if we're active
        if( ctm==rtm )
            reply << " : " << rte.statistics.counter( 0 );
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;
    // fill2out  = open [ : [<start>] : [<inc>] ]
    // 0           1        2           3
    // fill2disk = on : <scan label> [ : [<start>] : [<inc>] : [<realtime>] ]
    // 0           1    2                3           4         5
    //
    //    <start>    optional fillpattern start value
    //               (default: 0x1122334411223344)
    //    <inc>      optional fillpattern increment value
    //               each frame will get a pattern of
    //               "previous + inc"
    //               (default: 0)
    //    <realtime> flag defaults to '1' 
    if( (rtm==fill2out && args[1]=="open") ||
        (rtm==fill2disk && args[1]=="on") ) {
        recognized = true;

        if( rte.transfermode==no_transfer ) {
            bool                    realtime = true;
            char*                   eocptr;
            chain                   c;
            XLRCODE( SSHANDLE   ss = rte.xlrdev.sshandle());
            const string            start_s( OPTARG(((rtm==fill2out) ? 2 : 3), args) );
            const string            inc_s( OPTARG(((rtm==fill2out) ? 3 : 4), args) );
            const string            scan_s( OPTARG(2, args) );
            const string            realtime_s( OPTARG(5, args) );
            fillpatargs             fpargs(&rte);
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            EZASSERT2(dataformat.valid(), cmdexception,
                      EZINFO("Can only do this if a valid dataformat (mode=) is set"));

            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Both are fill2* transfers and thus support <start>, <inc>
            if( start_s.empty()==false ) {
                errno       = 0;
                fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                // !(A || B) => !A && !B
                EZASSERT2( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                           cmdexception, EZINFO("Failed to parse 'start' value") );
            }
            if( inc_s.empty()==false ) {
                errno      = 0;
                fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                // !(A || B) => !A && !B
                EZASSERT2( !(fpargs.inc==0 && eocptr==inc_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                           cmdexception, EZINFO("Failed to parse 'inc' value") );
            }
           
            // Handle fill2out
            if( rtm==fill2out ) { 
                // If we're doing fill2out on a Mark5B(+) we
                // cannot accept Mark4/VLBA data.
                // A Mark5A+ can accept Mark5B data ("mark5a+ mode")
                if( !is_mk5a )  {
                    EZASSERT2(rte.trackformat()==fmt_mark5b, cmdexception,
                              EZINFO("net2out on Mark5B can only accept Mark5B data"));
                }
                // Can already register the cleanup function
                c.register_final(&fill2outguard_fun, &rte);

            } else if( rtm==fill2disk ) { 
                // We *must* have a scan label, we may have realtime_s set
                EZASSERT2(scan_s.empty()==false, cmdexception, EZINFO("fill2disk MUST have a scan name"));

                string  scanlabel = scan_label::create_scan_label(scan_label::command, scan_s);
        
                if( realtime_s.empty()==false ) {
                    EZASSERT2(realtime_s=="1" || realtime_s=="0", cmdexception, 
                              EZINFO("only a '0' or '1' value is allowed for the realtime parameter") );
                    realtime = (realtime_s=="1");
                }

                // 'open' scan on the disk pack
                scanPointers[&rte] = rte.xlrdev.startScan( scanlabel ); 

                // Can already register the cleanup function
                c.register_final(&fill2diskguard_fun, &rte, scanPointers.find(&rte));
            }

            // we want the transfer to start immediately
            fpargs.run = true;
            // Because we're sending to the output, we have to do it
            // in real-time
            fpargs.realtime = realtime;
            
            // Start building the chain - generate frames of fillpattern
            // and write to FIFO ...
            c.add(&framepatterngenerator, 32, fpargs);
            c.add(fifowriter, &rte);
            // done :-)

            // switch on recordclock, not necessary for net2disk

            // Program streamstor according for the intented data path:
            //      PCI -> FPDP [fill2out]
            //      PCI -> DISK [fill2disk]
            if( rtm==fill2out ) {
                if( is_mk5a )
                    rte.ioboard[ mk5areg::notClock ] = 0;
                XLRCALL( ::XLRSetMode(ss, (CHANNELTYPE)SS_MODE_PASSTHRU) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

                // program where the output should go
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );
            } else {
                // Set up streamstor for recording
                XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );
                // and start the recording
                XLRCALL( ::XLRAppend(ss) );
            }

            rte.transfersubmode.clr_all();

            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing
            // Do this before we actually run the chain - something may
            // go wrong and we must cleanup later
            rte.transfermode    = rtm;

            // install and run the chain
            rte.processingchain = c;
            rte.processingchain.run();

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }
    // fill2out  = close
    // fill2disk = off
    if( (rtm==fill2out && args[1]=="close") ||
        (rtm==fill2disk && args[1]=="off") ) {
        recognized = true;

        // only accept this command if we're active
        // ['atm', acceptable transfermode has already been ascertained]
        if( rte.transfermode!=no_transfer ) {
            string error_message;
            try {
                // switch off recordclock (if not disk)
                if( ctm==fill2out && is_mk5a )
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
            // The cleanup functions will take care of stopping the
            // streamstor and resetting the transfermode to idle
            if ( error_message.empty() ) {
                reply << " 1 ;";
            }
            else {
                reply << " 4" << error_message << " ;";
            }
        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
