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
#include <threadfns.h>    // for all the processing steps + argument structs
#include <tthreadfns.h>
#include <inttypes.h>     // For SCNu64 and friends
#include <limits.h>
#include <sys/stat.h>

#include <iostream>

using namespace std;

// The disk2net 'guard' or 'finally' function
void disk2netguard_fun(runtime* rteptr) {
    try {
        DEBUG(3, "disk/fill/file2net guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );

        if( rteptr->transfermode==disk2net &&
            (rteptr->disk_state_mask & runtime::play_flag) )
                rteptr->xlrdev.write_state( "Played" );
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "disk2net finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "disk2net finalization threw an unknown exception" << std::endl );        
    }
    rteptr->transfermode = no_transfer;
}



// Support disk2net, file2net and fill2net
string disk2net_fn( bool qry, const vector<string>& args, runtime& rte) {
    static per_runtime<bool>   fill2net_auto_cleanup;
    static per_runtime<string> file_name;
    ostringstream              reply;
    const transfer_type        ctm( rte.transfermode ); // current transfer mode
    const transfer_type        rtm( ::string2transfermode(args[0]) );// requested transfer mode

    EZASSERT2(rtm!=no_transfer, Error_Code_6_Exception,
              EZINFO("unrecognized transfermode " << args[0]));

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is *always* possible, command will register 'busy'
    // if not doing nothing or the requested transfer mode 
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==rtm))

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( ctm!=rtm ) {
            reply << "inactive";
        } else {
            string status = "inactive";
            if ( rte.transfersubmode & run_flag ) {
                status = "active";
            }
            else if ( rte.transfersubmode & connected_flag ) {
                status = "connected";
            }
            // we ARE running so we must be able to retrieve the lasthost
            reply << status
                  << " : " << rte.netparms.host;
            if ( ctm == disk2net ) {
                if ( (rte.transfersubmode & run_flag) && (rte.transfersubmode & connected_flag) ) {
                    uint64_t start = rte.processingchain.communicate(0, &diskreaderargs::get_start).Addr;
                    reply << " : " << start
                          << " : " << rte.statistics.counter(0) + start
                          << " : " << rte.processingchain.communicate(0, &diskreaderargs::get_end);
                }
            }
            else if ( ctm == file2net ) {
                if ( (rte.transfersubmode & run_flag) && (rte.transfersubmode & connected_flag) ) {
                    off_t start = rte.processingchain.communicate(0, &fdreaderargs::get_start);
                    reply << " : " << start
                          << " : " << rte.statistics.counter(0) + start
                          << " : " << rte.processingchain.communicate(0, &fdreaderargs::get_end);               
                } 
            }
            else {
                reply << " : " << rte.statistics.counter(0);
            }
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
    // <connect>
    //
    //  disk2net = connect : <host>
    //     <host> is optional (remembers last host, if any)
    //  file2net = connect : <host> : filename
    //     <host> is optional (remembers last host, if any)
    //  fill2net = connect : <host> [ : [<start>] [ : <inc> ] [: <realtime>] ]
    //     <host> is as with disk2net
    //     <start>, <inc> are the fillpattern start + increment values
    //     both have defaults:
    //        <start>   0x1122334411223344
    //        <inc>     0
    //        which means that by default it creates blocks of
    //        invalid data ["recognized by the Mark5's to be
    //        invalid data"]
    //      <realtime> integer, default '0' (false)
    //          if set to non-zero the framegenerator will honour the
    //          datarate set by the "mode" + "play_rate/clock_set"
    //          command. Otherwise it goes as fast as it can
    //    If a trackformat other than 'none' is set via the "mode=" 
    //    command the fillpattern will generate frames of the correct
    //    size, with the correct syncword at the correct place. ALL
    //    other bytes have been filled with the current bitpattern of
    //    the fillpattern (including pre-syncwordbytes, eg in the vlba
    //    case).
    if( args[1]=="connect" ) {
        recognized = true;
        // if transfermode is already disk2net, we ARE already connected
        // (only {disk|fill|file}2net::disconnect clears the mode to doing nothing)
        if( rte.transfermode==no_transfer ) {
            // build up a new instance of the chain
            chain                   c;
            const string            protocol( rte.netparms.get_protocol() );
            const string            host( OPTARG(2, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // {disk|fill|file}playback has no mode/playrate/number-of-tracks
            // we do offer compression ... :P
            // HV: 08/Dec/2010  all transfers now key their constraints
            //                  off of the set mode. this allows better
            //                  control for all possible transfers
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // stick in a theoretical ipd close to that of 1Gbps -
            // we have NO information as to what the sustained diskspeed
            // is on this Mark5 nor what the linerate of the the link between 
            // this Mark5 and the destination is.
            const unsigned int payload = rte.sizes[constraints::write_size];
            const unsigned int n_bits_per_pkt( payload*8 );
            const unsigned int n_pkt_per_sec( (unsigned int)::ceil(1.0e9/n_bits_per_pkt) );

            rte.netparms.theoretical_ipd  = (int) ::floor(1.0e6 / n_pkt_per_sec);

            // the networkspecifics. 
            if( !host.empty() )
                rte.netparms.host = host;

            // add the steps to the chain. depending on the 
            // protocol we add the correct networkwriter
            if( rtm == disk2net ) {
                // prepare disken/streamstor
                XLRCALL( ::XLRSetMode(GETSSHANDLE(rte), SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRBindOutputChannel(GETSSHANDLE(rte), CHANNEL_PCI) );
                c.add(&diskreader, 10, diskreaderargs(&rte));
            } 
            else if( rtm==file2net ) {
                const string filename( OPTARG(3, args) );
                if ( filename.empty() ) {
                    reply <<  " 8 : need a source file ;";
                    return reply.str();
                }
                // Save file name for later use
                file_name[&rte] = filename;
                // Add a step to the chain (c.add(..)) and register a
                // cleanup function for that step, in one go
                c.register_cancel( c.add(&fdreader, 32, &open_file, filename + ",r", &rte),
                                   &close_filedescriptor);
            }
            else {
                // fill2net
                // Do some more parsing
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

            // if the trackmask is set insert a blockcompressor 
            if( rte.solution )
                c.add(&blockcompressor, 10, &rte);

            // register the cancellationfunction for the networkstep
            // which we will first add ;)
            // it will be called at the appropriate moment
            c.register_cancel(c.add(&netwriter<block>, &net_client, networkargs(&rte)), &close_filedescriptor);

            // Register a finalizer which automatically clears the transfer when done for
            // [file|disk]2net. fill2net *might* have the guardfn, but only
            // if it's set to send a finite amount of fillpattern - see
            // "fill2net=on" below. Make sure the 'fill2net auto cleanup'
            // boolean gets initialized to false.
            fill2net_auto_cleanup[&rte] = false;
            if( fromfile(rtm) || fromdisk(rtm) )
                c.register_final(&disk2netguard_fun, &rte);

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();
                
            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode = rtm;
        
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }

    // <on> : turn on dataflow
    //   disk2net=on[:[<start_byte>][:<end_byte>|+<amount>][:<repeat:0|1>]]
    //   file2net=on[:[<start_byte>][:<end_byte>|+<amount>]
    //   fill2net=on[:<amount of WORDS @ 8-byte-per-word>]
    if( args[1]=="on" ) {
        recognized = true;
        // only allow if transfermode==disk2net && submode hasn't got the running flag
        // set AND it has the connectedflag set
        if( ((rte.transfermode==disk2net  || rte.transfermode==file2net) && rte.transfersubmode&connected_flag)
            && (rte.transfersubmode&run_flag)==false ) {
            bool               repeat = false;
            uint64_t           start;
            uint64_t           end;
            const string       startstr( OPTARG(2, args) );
            const string       endstr( OPTARG(3, args) );
            const string       rptstr( OPTARG(4, args) );

            // Pick up optional extra arguments:
                
            // start-byte #
            if( startstr.empty()==false ) {
                ASSERT2_COND( ::sscanf(startstr.c_str(), "%" SCNu64, &start)==1,
                              SCINFO("start-byte# is out-of-range") );
            }
            else {
                if ( rte.transfermode==disk2net ) {
                    start = rte.pp_current.Addr;
                }
                else {
                    start = 0;
                }
            }
            // end-byte #
            // if prefixed by "+" this means: "end = start + <this value>"
            // rather than "end = <this value>"
            if( endstr.empty()==false ) {
                ASSERT2_COND( ::sscanf(endstr.c_str(), "%" SCNu64, &end)==1,
                              SCINFO("end-byte# is out-of-range") );
                if( endstr[0]=='+' )
                    end += start;
                ASSERT2_COND( ((rte.transfermode == file2net) && (end == 0)) || (end>start), SCINFO("end-byte-number should be > start-byte-number"));
            }
            else {
                if ( rte.transfermode==disk2net ) {
                    end = rte.pp_end.Addr;
                }
                else {
                    // file2net default end value should be the end of the file
                    struct stat   f_stat;

                    ASSERT2_ZERO( ::stat(file_name[&rte].c_str(), &f_stat), SCINFO(" - " << file_name[&rte]));
                    EZASSERT2((f_stat.st_mode&S_IFREG)==S_IFREG, cmdexception, EZINFO(file_name[&rte] << " not a regular file"));
                    end = f_stat.st_size;
                }
            }
            // repeat
            if( (rte.transfermode == disk2net) && (rptstr.empty()==false) ) {
                long int    v = ::strtol(rptstr.c_str(), 0, 0);

                if( (v==LONG_MIN || v==LONG_MAX) && errno==ERANGE )
                    throw xlrexception("value for repeat is out-of-range");
                repeat = (v!=0);
            }
            // now assert valid start and end, if any
            // so the threads, when kicked off, don't have to
            // think but can just *go*!
            if ( (rte.transfermode != file2net) && end<start ) {
                reply << " 6 : end byte should be larger than start byte ;";
                return reply.str();
            }

            if ( rte.transfermode==disk2net ) {
                S_DIR       currec;
                playpointer curlength;
                    
                ::memset(&currec, 0, sizeof(S_DIR));
                // end <= start => either end not specified or
                // neither start,end specified. Find length of recording
                // and play *that*, starting at startbyte#
                XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &currec) );
                curlength = currec.Length;

                // check validity of start,end
                if( start>curlength ||  end>curlength ) {
                    ostringstream  err;
                    err << "start and/or end byte# out-of-range, curlength=" << curlength;
                    throw xlrexception( err.str() );
                }
                    
                // Now communicate all to the appropriate step in the chain.
                // We know the diskreader step is always the first step ..
                // make sure we do the "run -> true" as last one, as that's the condition
                // that will make the diskreader go
                rte.processingchain.communicate(0, &diskreaderargs::set_start,  playpointer(start));
                rte.processingchain.communicate(0, &diskreaderargs::set_end,    playpointer(end));
                rte.processingchain.communicate(0, &diskreaderargs::set_repeat, repeat);
                rte.processingchain.communicate(0, &diskreaderargs::set_run,    true);
            }
            else {
                rte.processingchain.communicate(0, &fdreaderargs::set_start,  off_t(start));
                rte.processingchain.communicate(0, &fdreaderargs::set_end,    off_t(end));
                rte.processingchain.communicate(0, &fdreaderargs::set_run,    true);
            }

            reply << " 0 ;";
        } else if( rte.transfermode==fill2net
                   && (rte.transfersubmode&connected_flag)==true
                   && (rte.transfersubmode&run_flag)==false ) {
            // not running yet!
            // pick up optional <number-of-words>
            if( args.size()>2 && !args[2].empty() ) {
                uint64_t   v;
                ASSERT2_COND( ::sscanf(args[2].c_str(), "%" SCNu64, &v)==1,
                              SCINFO("value for nwords is out of range") );
                // communicate this value to the chain
                DEBUG(1,args[0] << "=" << args[1] << ": set nword to " << v << endl);
                rte.processingchain.communicate(0, &fillpatargs::set_nword, v);

                // Because the user specifies a finite amount of fillpattern
                // to generate, we will register the guard function, which
                // will reset the transfer automagically if it's done
                rte.processingchain.register_final(&disk2netguard_fun, &rte);

                // Indicate that fill2net does auto-cleanup
                fill2net_auto_cleanup[&rte] = true;
            }
            // and turn on the dataflow
            rte.processingchain.communicate(0, &fillpatargs::set_run, true);
            reply << " 0 ;";
        } else {
            // transfermode is either no_transfer or {disk|fill|file}2net, nothing else
            if( rte.transfermode==disk2net||rte.transfermode==fill2net||rte.transfermode==file2net ) {
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
        if( rte.transfermode!=no_transfer ) {
            try {
                // let the runtime stop the threads
                rte.processingchain.stop();
                
                rte.transfersubmode.clr( connected_flag );
                reply << ( (rte.transfermode==fill2net && !fill2net_auto_cleanup[&rte]) ? " 0 ;" : " 1 ;" );
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
                
            if ( rte.transfermode == fill2net ) {
                rte.transfermode = no_transfer;
            }

        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
