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
#include <tthreadfns.h>
#include <inttypes.h>
#include <limits.h>
#include <iostream>

using namespace std;


// Handle disk2file and fill2file
string diskfill2file_fn(bool q, const vector<string>& args, runtime& rte ) {
    static per_runtime<string>  destfilename;

    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode
    const transfer_type rtm( ::string2transfermode(args[0]) ); // requested transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((q)?('?'):('=')) << " ";

    // Queries should be possible always, commands only
    // when doing nothing or when the requested transfer == current transfer
    INPROGRESS(rte, reply, !(q || ctm==no_transfer || ctm==rtm))

    // Good. See what the usr wants
    if( q ) {
        reply << " 0 : ";
        // if current transfer != requested transfer it must be inactive
        if( ctm!=rtm )
            reply << "inactive";
        else
            reply << destfilename[&rte] << " : " << rte.transfersubmode;
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;

    // disk2file = connect : <filename> 
    // fill2file = connect : <filename> [: [<start>] [: [<inc>]] [:<realtime>] ]
    //             <start> = the start value of the 64bit fillpattern
    //                       default: 0x1122334411223344
    //             <inc>   = at each block|frame (taken from the
    //                       global mode; mode==none => block)
    //                       the fillpattern value is incremented
    //                       by this value. 
    //                       default: 0
    //            <realtime> integer, default '0' (false)
    //                   if set to non-zero the framegenerator will honour the
    //                   datarate set by the "mode" + "play_rate/clock_set"
    //                   command. Otherwise it goes as fast as it can
    if( args[1]=="connect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const bool              disk( fromdisk(rtm) );
            const string            filename( OPTARG(2, args) );
            const string            proto( rte.netparms.get_protocol() );
                
            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );

            // Now that we have all commandline arguments parsed we may
            // construct our headersearcher
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            throw_on_insane_netprotocol(rte);

            // add the steps to the chain. depending on the 
            // protocol we add the correct networkwriter
            if( disk ) {
                // prepare disken/streamstor
                XLRCALL( ::XLRSetMode(GETSSHANDLE(rte), SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRClearChannels(GETSSHANDLE(rte)) );
                XLRCALL( ::XLRBindOutputChannel(GETSSHANDLE(rte), CHANNEL_PCI) );
                c.add(&diskreader, 10, diskreaderargs(&rte));
            } else {
                // fill2file: Do some more parsing
                char*         eocptr;
                fillpatargs   fpargs(&rte);
                const string  start_s( OPTARG(3, args) );
                const string  inc_s( OPTARG(4, args) );
                const string  realtime_s( OPTARG(5, args ) );

                if( start_s.empty()==false ) {
                    errno       = 0;
                    fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'start' value") );
                }
                if( inc_s.empty()==false ) {
                    errno      = 0;
                    fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.inc==0 && eocptr==inc_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'inc' value") );
                }
                if( realtime_s.empty()==false ) {
                    long           num;

                    errno = 0;
                    num   = ::strtol(realtime_s.c_str(), &eocptr, 10);
                    ASSERT2_COND( eocptr!=realtime_s.c_str() && *eocptr=='\0' && !(num==0 && errno==ERANGE),
                                  SCINFO("'realtime' should be a decimal number") );
                    fpargs.realtime = (num!=0);
                }
                c.add(&fillpatternwrapper, 10, fpargs);
            }

            // if the trackmask is set insert a blockcompressor or
            // a framer + a framecompressor
            if( rte.solution ) {
                if( dataformat.valid() ) {
                    c.add(&framer<frame>,   10, framerargs(dataformat, &rte));
                    //c.add(&timedecoder,     10, dataformat);
                    c.add(&framecompressor, 10, compressorargs(&rte));
                    c.add(&frame2block,     10);
                } else {
                    c.add(&blockcompressor, 10, &rte);
                }
            }

            // register the cancellationfunction for the filewriter
            // it will be called at the appropriate moment
            c.register_cancel(c.add(&fdwriter<block>, &open_file, filename, &rte), &close_filedescriptor);

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode    = (disk?disk2file:fill2file);

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();

            // Store the current filename for future reference
            destfilename[&rte] = filename;

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }

    //   disk2file=on[:[<start_byte>][:<end_byte>|+<amount>][:<repeat:0|1>]]
    //   fill2file=on[:<amount of WORDS @ 8-byte-per-word>]
    if( args[1]=="on" ) {
        recognized = true;
        // only allow if transfermode==disk2net && submode hasn't got the running flag
        // set AND it has the connectedflag set
        if( rte.transfermode==disk2file && (rte.transfersubmode&run_flag)==false ) {
            bool               repeat = false;
            string             start_s( OPTARG(2, args) );
            string             end_s( OPTARG(3, args) );
            string             repeat_s( OPTARG(4, args) );
            uint64_t           nbyte;
            playpointer        pp_s;
            playpointer        pp_e;

            // Pick up optional extra arguments:
            // note: we do not support "scan_set" yet so
            //       the part in the doc where it sais
            //       that, when omitted, they refer to
            //       current scan start/end.. that no werk

            // start-byte #
            if( !start_s.empty() ) {
                uint64_t v;

                ASSERT2_COND( ::sscanf(start_s.c_str(), "%" SCNu64, &v)==1,
                              SCINFO("start-byte# is out-of-range") );
                pp_s.Addr = v;
            }
            // end-byte #
            // if prefixed by "+" this means: "end = start + <this value>"
            // rather than "end = <this value>"
            if( !end_s.empty() ) {
                uint64_t v;
                   
                ASSERT2_COND( ::sscanf(end_s.c_str(), "%" SCNu64, &v)==1,
                              SCINFO("end-byte# is out-of-range") );
                if( end_s[0]=='+' )
                    pp_e.Addr = pp_s.Addr + v;
                else
                    pp_e.Addr = v;
                ASSERT2_COND(pp_e>pp_s, SCINFO("end-byte-number should be > start-byte-number"));
            }
            // repeat
            if( !repeat_s.empty() ) {
                long int    v = ::strtol(repeat_s.c_str(), 0, 0);

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

                ::memset(&currec, 0, sizeof(S_DIR));
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
            DEBUG(1, "start/end [nbyte]=" <<
                  pp_s << "/" << pp_e << " [" << nbyte << "] " <<
                  "repeat:" << repeat << endl);
            nbyte = nbyte/rte.netparms.get_blocksize() * rte.netparms.get_blocksize();
            if( nbyte<rte.netparms.get_blocksize() )
                throw xlrexception("less than <blocksize> bytes selected to play. no can do");
            pp_e = pp_s.Addr + nbyte;
            DEBUG(1, "Made it: start/end [nbyte]=" <<
                  pp_s << "/" << pp_e << " [" << nbyte << "] " <<
                  "repeat:" << repeat << endl);

            // Now communicate all to the appropriate step in the chain.
            // We know the diskreader step is always the first step ..
            // make sure we do the "run -> true" as last one, as that's the condition
            // that will make the diskreader go
            rte.processingchain.communicate(0, &diskreaderargs::set_start,  pp_s);
            rte.processingchain.communicate(0, &diskreaderargs::set_end,    pp_e);
            rte.processingchain.communicate(0, &diskreaderargs::set_repeat, repeat);
            rte.processingchain.communicate(0, &diskreaderargs::set_run,    true);
            reply << " 0 ;";
        } else if( rte.transfermode==fill2file
                   && (rte.transfersubmode&run_flag)==false ) {
            string  number_s( OPTARG(2, args) );
            // not running yet!
            // pick up optional <number-of-words>
            if( !number_s.empty() ) {
                uint64_t   v;

                ASSERT2_COND( ::sscanf(number_s.c_str(), "%" SCNu64, &v)==1,
                              SCINFO("value for nwords is out of range") );

                // communicate this value to the chain
                DEBUG(1,args[0] << "=" << number_s << ": set nword to " << v << endl);
                rte.processingchain.communicate(0, &fillpatargs::set_nword, v);
            }
            // and turn on the dataflow
            rte.processingchain.communicate(0, &fillpatargs::set_run, true);
            reply << " 0 ;";
        } else {
            // transfermode is either no_transfer or {disk|fill}2file, nothing else
            if( rte.transfermode==disk2file||rte.transfermode==fill2file ) {
                if( rte.transfersubmode&run_flag )
                    reply << " 6 : already running ;";
                else
                    reply << " 6 : not running yet ;";
            } else 
                reply << " 6 : not doing anything ;";
        }
    }
    // Close down the whole thing
    if( args[1]=="disconnect" ) {
        recognized = true;
        if( rte.transfermode!=no_transfer ) {
            string error_message;
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

            if( rte.transfermode==disk2file && (rte.disk_state_mask & runtime::play_flag) ) {
                try {
                    rte.xlrdev.write_state( "Played" );
                }
                catch ( std::exception& e ) {
                    error_message += string(" : Failed to write disk state: ") + e.what();
                }
                catch ( ... ) {
                    error_message += string(" : Failed to write disk state, unknown exception");
                }
            }

            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

            // erase the entry for the current rte
            destfilename.erase( &rte );

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
