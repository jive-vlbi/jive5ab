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
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

using namespace std;

// The disk2net 'guard' or 'finally' function
void disk2netguard_fun(runtime* rteptr, chain::stepid s) {
    try {
        DEBUG(3, "disk/fill/file2net guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );

        if( s!=chain::invalid_stepid )
            rteptr->processingchain.communicate(s, &::close_filedescriptor);

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
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer);
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
            chain::stepid           fdstep = chain::invalid_stepid; // after .run() be able to set 'allow variable block size'
            chain::stepid           netstep = chain::invalid_stepid;
            const string            protocol( rte.netparms.get_protocol() );
            const string            host( OPTARG(2, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               rte.trackbitrate(),
                                               rte.vdifframesize());

            // {disk|fill|file}playback has no mode/playrate/number-of-tracks
            // we do offer compression ... :P
            // HV: 08/Dec/2010  all transfers now key their constraints
            //                  off of the set mode. this allows better
            //                  control for all possible transfers
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            throw_on_insane_netprotocol(rte);

            // After having constrained ourselves, we may safely compute a
            // theoretical IPD
            compute_theoretical_ipd( rte );

            // the networkspecifics. 
            if( !host.empty() )
                rte.netparms.host = host;

            // add the steps to the chain. depending on the 
            // protocol we add the correct networkwriter
            if( rtm==disk2net ) {
                diskreaderargs  dra(&rte);

                // prepare disken/streamstor
                XLRCALL( ::XLRSetMode(GETSSHANDLE(rte), SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRClearChannels(GETSSHANDLE(rte)) );
                XLRCALL( ::XLRBindOutputChannel(GETSSHANDLE(rte), CHANNEL_PCI) );

                // Do we allow variable block size?
                dra.allow_variable_block_size = (!dataformat.valid() || !rte.solution);
                c.add(&diskreader, 10, dra);
            } 
            else if( rtm==file2net ) {
                struct stat   f_stat;
                const string  filename( OPTARG(3, args) );

                if ( filename.empty() ) {
                    reply <<  " 8 : need a source file ;";
                    return reply.str();
                }

                // Save file name for later use
                file_name[&rte] = filename;

                ASSERT2_ZERO( ::stat(file_name[&rte].c_str(), &f_stat), SCINFO(" - " << file_name[&rte]));
                EZASSERT2((f_stat.st_mode&S_IFREG)==S_IFREG, cmdexception, EZINFO(file_name[&rte] << " not a regular file"));

                // do remember the step-id of the reader such that
                // later on we can communicate with it (see below)
                fdstep = c.add(&fdreader, 32, &open_file, filename + ",r", &rte);
                c.register_cancel(fdstep, &close_filedescriptor);
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
            netstep = c.add(&netwriter<block>, &net_client, networkargs(&rte));
            c.register_cancel(netstep, &close_filedescriptor);

            // Register a finalizer which automatically clears the transfer when done for
            // [file|disk]2net. fill2net *might* have the guardfn, but only
            // if it's set to send a finite amount of fillpattern - see
            // "fill2net=on" below. Make sure the 'fill2net auto cleanup'
            // boolean gets initialized to false.
            fill2net_auto_cleanup[&rte] = false;
            if( fromfile(rtm) || fromdisk(rtm) )
                c.register_final(&disk2netguard_fun, &rte, netstep);

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();

            // Now that we're running we can inform the fdreader in case of file2net
            // that it's Ok to allow partial blocks (if we're not doing compression, that is)
            if( rtm==file2net && !rte.solution )
                rte.processingchain.communicate(fdstep, &fdreaderargs::set_variable_block_size, true);

                
            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode = rtm;
       
            // HV/BE 9 dec 2014 disk2net=connect:... should return '1'
            //                  because we cannot guarantee that the 
            //                  connect phase in the chain has already
            //                  completed 
            reply << " " << ((rtm==disk2net)?1:0) << " ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }

    // <on> : turn on dataflow
    //   disk2net=on[:[[+-]<offset>|<start_byte>][:<end_byte>|[+-]<amount>][:<repeat:0|1>]]
    //      <start_byte>   overwrite start value set by "scan_set="
    //      [+-]<offset>   adjust start value set by "scan_set="
    //
    //      <end_byte>     overwrite end value set by "scan_set="
    //      +<amount>      overwrite end value with start + <amount>
    //      -<amount>      adjust end value set by "scan_set=" by -<amount>
    //
    //   file2net=on[:[<start_byte>][:<end_byte>|+<amount>] 
    //       # file2net does not have a separate setting of start/end like
    //       # disk2net has, so we cannot use the "+start" to inform it we
    //       # want it to offset wrt a pre-set start. By adding a 3rd
    //       # argument we can, if we put in the same start/end values, 
    //       # skip wrt to whatever that preset start value was.
    //       # This is necessary for resuming
    //   fill2net=on[:<amount of WORDS @ 8-byte-per-word>]
    if( args[1]=="on" ) {
        recognized = true;
        // only allow if transfermode==disk2net && submode hasn't got the running flag
        // set AND it has the connectedflag set
        if( ((rte.transfermode==disk2net  || rte.transfermode==file2net) && rte.transfersubmode&connected_flag)
            && (rte.transfersubmode&run_flag)==false ) {
            bool               repeat = false;
            // Initialize start and end depending on what we're doing
            int64_t            start  = (rte.transfermode==disk2net ? rte.pp_current.Addr : 0);
            int64_t            end    = (rte.transfermode==disk2net ? rte.pp_end.Addr :
                                         (int64_t)rte.processingchain.communicate(0, &fdreaderargs::get_file_size));
            const string       startstr( OPTARG(2, args) );
            const string       endstr( OPTARG(3, args) );
            const string       rptstr( OPTARG(4, args) );      // position 4 is shared

            // Pick up optional extra arguments:
                
            // start-byte #
            // HV: 11Jun2015 change order a bit. Allow for "+start" to
            //               skip the read pointer wrt to what we already
            //               have
            if( startstr.empty()==false ) {
                char*      eocptr;
                int64_t    v;

                // 19Aug2015: eBob mentioned that there was an inconsistency:
                //            here we test for negative numbers and disallow
                //            totally whilst for file2net the code below
                //            would accept it. So for disk2net we do not
                //            allow '-' (documented behaviour), for file2net
                //            we do allow '-' because that one does not work
                //            with 'scan_set'.
                //            In addition we support '+' for disk2net in
                //            order to support resuming a transfer. This is
                //            non-standard behaviour and will be documented
                //            in the jive5ab command set documentation.
                if( rte.transfermode==disk2net && startstr[0]=='-' ) {
                    reply << " 8 : relative byte number for start is not allowed ;";
                    return reply.str();
                }
            
                // ensure only numbers are given  
                errno = 0; 
                v = ::strtoll(startstr.c_str(), &eocptr, 0);
                ASSERT2_COND(eocptr!=startstr.c_str() && *eocptr=='\0' && errno==0,
                             SCINFO(" value for start is out-of-range"));

                // Depending on which transfer ....
                switch( rte.transfermode ) {
                    case disk2net:
                        // if explicitly signed, it's a relative offset wrt
                        // current start
                        if( startstr[0]=='+' )
                            start += v;
                        else
                            start  = v;
                        break;

                    case file2net:
                        // if negative, it means offset wrt to end-of-file
                        start = v;
                        if( start<0 )
                            start += end;
                        break;

                    default:
                        // should have been caught by 'if( ... )' entering
                        // this branch but we need a default case anyway
                        EZASSERT2(false, cmdexception, EZINFO(" transfer " << rte.transfermode << " does not support startbyte"));
                        break;
                }
            }

            // end-byte #
            // if prefixed by "+" this means: "end = start + <this value>"
            // rather than "end = <this value>"
            if( endstr.empty()==false ) {
                char*    eocptr;
                int64_t  v;

                if( rte.transfermode==disk2net && endstr[0]=='-' ) {
                    reply << " 8 : relative byte number for end is not allowed ;";
                    return reply.str();
                }
              
                errno = 0; 
                v = ::strtoll(endstr.c_str(), &eocptr, 0);
                ASSERT2_COND(eocptr!=endstr.c_str() && *eocptr=='\0' && errno==0,
                             SCINFO(" value for end is out-of-range"));

                // Depending on which transfer ....
                switch( rte.transfermode ) {
                    case disk2net:
                        // if explicitly signed, it's a relative offset wrt
                        // current end
                        if( endstr[0]=='+' )
                            end = start + v;
                        else
                            end  = v;
                        break;

                    case file2net:
                        // if negative, it means offset wrt to end-of-file
                        // ('end' is already initialized with file size)
                        // explicit '+' means offset wrt to start
                        if( v<0 )
                            end += v;
                        else if( endstr[0]=='+' )
                            end = start+v;
                        else 
                            end = v;
                        break;

                    default:
                        // should have been caught by 'if( ... )' entering
                        // this branch but we need a default case anyway
                        EZASSERT2(false, cmdexception, EZINFO(" transfer " << rte.transfermode << " does not support endbyte"));
                        break;
                }
            }

            // repeat
            if( (rte.transfermode == disk2net) && (rptstr.empty()==false) ) {
                char*       eocptr;
                long int    v;
              
                errno = 0; 
                v = ::strtol(rptstr.c_str(), &eocptr, 0);
                EZASSERT2(eocptr!=rptstr.c_str() && *eocptr=='\0' && errno==0, cmdexception,
                          EZINFO(" value for repeat is out-of-range"));
                repeat = (v!=0);
            }

            // Make sure the start/end values are sensible
            //    disk2net: end MUST be >= start
            //    file2net: either end >= start or must be == 0
            //    neither support negative start
            EZASSERT2(start>=0 && ((end>=start) || (rte.transfermode==file2net && end==0)),
                      cmdexception, EZINFO(" start/end byte number " << start << ", " << end << " invalid"));

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
                rte.processingchain.register_final(&disk2netguard_fun, &rte, chain::invalid_stepid);

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
