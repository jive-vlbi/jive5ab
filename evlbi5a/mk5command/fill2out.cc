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
#include <threadfns.h>
#include <iostream>

using namespace std;


// set up fill2out
string fill2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    const bool          is_mk5a( rte.ioboard.hardware() & ioboard_type::mk5a_flag );
    ostringstream       reply;


    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";
        
    // If we aren't doing anything nor the requested transfer is not the one
    // running - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=fill2out ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << "active";
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
        // fill2out=open[:<start>][:<inc>]
        //    <start>   optional fillpattern start value
        //              (default: 0x1122334411223344)
        //    <inc>     optional fillpattern increment value
        //              each frame will get a pattern of
        //              "previous + inc"
        //              (default: 0)
        if( rte.transfermode==no_transfer ) {
            char*                   eocptr;
            chain                   c;
            XLRCODE( SSHANDLE   ss = rte.xlrdev.sshandle());
            fillpatargs             fpargs(&rte);
            const string            start_s( OPTARG(2, args) );
            const string            inc_s( OPTARG(3, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            EZASSERT2(dataformat.valid(), cmdexception,
                      EZINFO("Can only do this if a valid dataformat (mode=) is set"));

            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
            
            // If we're doing net2out on a Mark5B(+) we
            // cannot accept Mark4/VLBA data.
            // A Mark5A+ can accept Mark5B data ("mark5a+ mode")
            if( !is_mk5a )  {
                EZASSERT2(rte.trackformat()==fmt_mark5b, cmdexception,
                          EZINFO("net2out on Mark5B can only accept Mark5B data"));
            }

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
            // we want the transfer to start immediately
            fpargs.run = true;
            // Because we're sending to the output, we have to do it
            // in real-time
            fpargs.realtime = true;
            
            // Start building the chain - generate frames of fillpattern
            // and write to FIFO ...
            c.add(&framepatterngenerator, 32, fpargs);
            c.add(fifowriter, &rte);
            // done :-)

            // switch on recordclock, not necessary for net2disk
            if( is_mk5a )
                rte.ioboard[ mk5areg::notClock ] = 0;

            // now program the streamstor to record from PCI -> FPDP
            XLRCALL( ::XLRSetMode(ss, (CHANNELTYPE)SS_MODE_PASSTHRU) );
            XLRCALL( ::XLRClearChannels(ss) );
            XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

            // program where the output should go
            XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
            XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );

            rte.transfersubmode.clr_all();

            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing
            // Do this before we actually run the chain - something may
            // go wrong and we must cleanup later
            rte.transfermode    = fill2out;

            // install and run the chain
            rte.processingchain = c;
            rte.processingchain.run();

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
            string error_message;
            try {
                // switch off recordclock (if not disk)
                if( is_mk5a )
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
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop streamstor: ") + e.what();
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop streamstor, unknown exception");
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
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
