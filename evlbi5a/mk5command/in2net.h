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
#ifndef EVLBI5A_IN2NET_H
#define EVLBI5A_IN2NET_H

#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <mk5command/mk5functions.h> // for in2disk_fn command forwarding
#include <mk5command/in2netsupport.h>
#include <threadfns.h>
#include <tthreadfns.h>
#include <carrayutil.h>
#include <interchainfns.h>
#include <dotzooi.h>
#include <scan_label.h>
#include <iostream>


// A templated "in2net" function (which can also be called as in2fork,
// in2file or record).
// Since the steps and states an in2net/in2fork/in2file transfer must go through/can
// be in are identical on both mark5a/mark5b it makes sense to abstract that
// out. The only thing they differ in is in which registers in the IOBoard
// they access/address in order to make the transfer start/stop/resume etc.
//
// It is templated on the actual mark5 for which this function applies (see
// the in2net_transfer<> just above this).
// You may obtain a function pointer to an instantiated function (this is,
// after all, a template) by:
//     &in2net_fn<[hardwareenum]>;
// e.g.:
//   
//     fptr = &in2net_fn<mark5a>;
//
// The actual transfers:
//
//  in2net=connect:<ip|host> [ : <strict> ]
//       initiate a networktransfer to the mentioned ip|host 
//
//  in2fork=connect:<ip|host>:<scanname> [ : <strict> ]
//       initiate networktransfer to ip|host AND prepare for recording to
//       local disk, adding a new scan named <scanname> to the UserDirectory
//       on the disk
//
//  in2file=connect:/path/to/some/file [: <openmode>] [ : <strict> ]
//       with <openmode>:
//          w   truncate file, create if not exist yet
//          a   append to file, create if not exist yet
//          n   create new file, fail if exist  (default)
//
//       IF a dataformat is set AND compression is requested, THEN a
//       framesearcher is inserted into the streamprocessor unconditionally.
//       The framesearcher only checks for the appearance
//       of the syncword of the expected dataformat 
//
//  in2mem=on
//
//  in2memfork=on:<scanname>
//
//
//  UPDATE 26-Jun-2013
//    There are far too many "if"s in the body and it's not always clear 
//    which transfer modes expose which kind of behaviour.
//
//    The commands can be separated into two kinds of behaviours, the
//    "immediate" commands and the two-stage ones.
//
//    The immediate commands only support "<command>=on[:<optional args>]"
//    and "<command>=off". The data flow starts immediately and is
//    unpausable.
//
//    The two-stage commands have: 
//    "<command>=connect:<connect parameters>" (data does not flow, the 
//    transfer is just set up).
//    "<command>=on" - to start the data flow
//    "<command>=off" to pause the data flow
//    "<command>=disconnect" this terminates the transfer
//
//    We have decided that:
//      in2memfork, in2mem, [both alternatives for "record"] are "immediate"
//
//      in2net, in2file, in2fork have the pausable behaviour; they have
//      separate set-up and "go!" stages
//
//
//  NOTE NOTE NOTE NOTE NOTE NOTE
//
//    when running in in2fork mode the recording mode is slightly different:
//    WRAP_ENABLE is off (in2net=>WRAP_ENABLE=on). what this means is that
//    in2net can run forever (it wraps) but in2fork does NOT since if
//    WRAP_ENABLE == true and writing to disk == true, then the disk will be
//    overwritten when it's full since it'll continue recording at the beginning
//    of the disk.
template <unsigned int Mark5>
std::string in2net_fn( bool qry, const std::vector<std::string>& args, runtime& rte ) {
    // needed for diskrecording - need to remember across fn calls
    static ScanPointer                scanptr;
    static per_runtime<std::string>   last_filename;
    static per_runtime<chain::stepid> fifostep;
    static const transfer_type        supported[] = {in2fork, in2net, in2file, in2mem, in2memfork};

    // From args[0] we find out the requested transfer mode.
    // Take care of remapping the "record" command to something else
    // if we're called like that
    const transfer_type rtm( string2transfermode((args[0]=="record"?"in2memfork":args[0])) );
    const bool          immediate( rtm==in2mem || rtm==in2memfork );
    const bool          m5c = rte.ioboard.hardware() & ioboard_type::mk5c_flag;
    const bool          m5a = rte.ioboard.hardware() & ioboard_type::mk5a_flag;
    const bool          m5b = rte.ioboard.hardware() & ioboard_type::mk5b_flag;
    std::ostringstream  reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // as a safe guard, in2memfork will be handled by in2disk when the 
    // requested data rate is higher than the StreamStor can handle,
    // if in2disk is handling a record command, forward futher record commands
    // and queries to that function
    if ( (ctm == in2disk) && (args[0] == "record") ) {
        return in2disk_fn( qry, args, rte );
    }

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Check to see if the requested transfer is supported by this function
    if( !find_element(rtm, supported) ) {
        reply << " 2 : " << args[0] << " is not supported by this implementation ;";
        return reply.str();
    }

    // Query is always possible, command only if doing nothing or if the
    // requested transfer mode == current transfer mode
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==rtm))

    if( qry ) {
        reply << " 0 : ";

        if ( args[0] == "record" ) { // when record has been mapped to in2memfork, we need to simulate the record reply
            if( rtm!=ctm ) {
                reply << "off";
            } else {
                // 5 possible status messages: on, halted, throttled, overflow and waiting
                S_DEVSTATUS dev_status;
                XLRCALL( ::XLRGetDeviceStatus(rte.xlrdev.sshandle(), &dev_status) );
                if ( dev_status.Recording ) {
                    // NOTE: these decisions should be made based on
                    //       detected hardware rather than on template
                    //       parameter. It's possible to run on a Mark5
                    //       system w/o hardware support.
                    if( m5a ) {
                        // recording is on, check for throttled (Mark5A checks that before overflow)
                        outputmode_type mode;
                        rte.get_output(mode);

                        // throttled seems to be always on the first time mode is read from the ioboard
                        rte.get_output(mode);
                        if ( mode.throttle ) {
                            reply << "throttled";
                        }
                        else if ( dev_status.Overflow[0] ) {
                            reply << "overflow";
                        }
                        else {
                            reply << "on";
                        }
                    }
                    else if( m5b ) {
                        if ( dev_status.Overflow[0] || rte.ioboard[mk5breg::DIM_OF] ) {
                            reply << "overflow";
                        }
                        else {
                            reply << "on";
                        }
                    } else if( m5c ) {
                        if ( dev_status.Overflow[0] )
                            reply << "overflow";
                        else
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
                reply << " : " << rte.xlrdev.nScans() << " : " << ROScanPointer::strip_asterisk( scanptr.name() );
            }
        }
        else {
            // For in2net + in2file the first parameter is the current/last 
            // host- or filename. The second parameter in the reply is the 
            // activity status
            if( rtm==in2net || in2net==in2fork ) {
                reply << rte.netparms.host << (isfork(rtm)?"f":"") << " : ";
            } else if( rtm==in2file ) {
                reply << last_filename[&rte] << " : ";
            }

            // If the requested transfer mode is not the current transfer
            // mode, then the requested mode is _certainly_ inactive
            if( rtm != ctm ) {
                reply << "inactive";
            } else {
                // Ok, requested transfer mode == current transfer mode.
                // Thus it MUST be active
                reply << "active" << " : " << rte.statistics.counter(fifostep[&rte]) << " : " << rte.transfersubmode;
            }
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;

    // <connect> only applies to the non-immediate transfers
    // <on> applies to the immediate transfers. 
    // We handle those cases as if it were a "connect" + "on" in one go
    if( (args[1]=="connect" && !immediate) || 
        (args[1] =="on" && immediate) ) {
        recognized = true;
        // if transfermode is already NOT no_transfer, we ARE already connected
        // and only the "disconnect" or "off" will clear the transfer mode
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            std::string             filename, scanname, filemode = "n";
            const bool              rtcp    = (rte.netparms.get_protocol()=="rtcp");
            XLRCODE(SSHANDLE        ss      = rte.xlrdev.sshandle());
            XLRCODE(CHANNELTYPE     inputch = in2net_transfer<Mark5>::inputchannel());

            if ( args[0] == "record" && 
                 (rte.ntrack() * rte.trackbitrate() > rte.xlrdev.maxForkDataRate()) ) {
                // if StreamStor is not capable of forking the requested data rate
                // fall back to plain old record aka in2disk
                std::string record_reply = in2disk_fn(qry, args, rte);
                // add an extra warning to the reply
                return record_reply.substr(0, record_reply.rfind(';')) +
                    ": falling back to plain recording as requested data rate is higher than StreamStor can handle while forking ;";
            }


            // good. pick up optional hostname/ip to connect to
            // unless it's rtcp
            if( rtm==in2net || rtm==in2fork ) {
                const std::string host = OPTARG(2, args);
                if( !host.empty() ) {
                    if( !rtcp )
                        rte.netparms.host = host;
                    else
                        DEBUG(0, args[0] << ": WARN! Ignoring supplied host '" << host << "'!" << std::endl);
                }
            } else if( tofile(rtm) ) {
                const std::string    option = OPTARG(3, args);

                filename = OPTARG(2, args);
                ASSERT2_COND( filename.empty()==false, SCINFO("in2file MUST have a filename as argument"));
                // save for later use [in the query]
                last_filename[ &rte ] = filename;

                if( !option.empty() )
                    filemode = option;

                // We've saved the unmodified file name but our
                // file-opening-function expects the file open mode
                // to be appended to the filename as ",<mode>"
                filename += (std::string(",")+filemode);
            }

            // in2fork requires extra arg: the scanname
            // NOTE: will throw up if none given!
            // Also perform some extra sanity checks needed
            // for disk-recording
            if( isfork(rtm) ) {
                S_DIR         disk;
                S_DEVINFO     devInfo;

                ::memset(&disk, 0, sizeof(S_DIR));
                ::memset(&devInfo, 0, sizeof(S_DEVINFO));

                const unsigned int arg_position = (toqueue(rtm) ? 2 : 3);
                scanname = OPTARG(arg_position, args);

                EZASSERT2( scanname.empty()==false, Error_Code_6_Exception,
                           EZINFO("Forking mode MUST have a scan name") );

                // Verify that there are disks on which we *can*
                // record!
                XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                ASSERT_COND( devInfo.NumDrives>0 );

                // and they're not full or writeprotected
                XLRCALL( ::XLRGetDirectory(ss, &disk) );
                ASSERT_COND( !(disk.Full || disk.WriteProtected) );
            } 

            // If we attempt to record on a Mark5B(+) we must
            // meet these preconditions
            if( rte.ioboard.hardware() & ioboard_type::mk5b_flag ) {
                dot_type            dotclock = get_dot();
                mk5b_inputmode_type curipm;

                // Do not allow to record without a 1PPS source or if it isn't synced
                rte.get_input( curipm );
                EZASSERT2( curipm.selpps && *rte.ioboard[mk5breg::DIM_SUNKPPS], cmdexception,
                           EZINFO("There is not 1PPS source set yet or it is not synchronized") );

                // Verify that we do have a DOT!
                EZASSERT2(dotclock.dot_status==dot_ok, cmdexception, EZINFO("DOT fail - " << dotstatus2str(dotclock.dot_status)));
            }

            in2net_transfer<Mark5>::setup(rte);

            // now program the streamstor to record from FPDP -> PCI
            XLRCALL( ::XLRSetMode(ss, (isfork(rtm)?SS_MODE_FORK:SS_MODE_PASSTHRU)) );
            XLRCALL( ::XLRClearChannels(ss) );
            XLRCALL( ::XLRBindInputChannel(ss, inputch) );
            XLRCALL( ::XLRSelectChannel(ss, inputch) );
            XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_PCI) );

            // Code courtesy of Cindy Gold of Conduant Corp.
            //   Have to distinguish between old boards and 
            //   new ones (most notably the Amazon based boards)
            //   (which are used in Mark5B+ and Mark5C)
            //
            // May 2013: Mark5C doesn't have Daughterboard so must
            //           skip programming of that. Alternatively:
            //           only on systems that have an I/O board
            //           this code must run
            if( m5a || m5b ) {
                XLRCODE(UINT     u32recvMode);
                XLRCODE(UINT     u32recvOpt);

                // Check. Now program the FPDP channel
                XLRCALL( ::XLRSelectChannel(ss, inputch) );

                if( rte.xlrdev.boardGeneration()<4 ) {
                    // This is either a XF2/V100/VXF2
                    XLRCODE(u32recvMode = SS_FPDP_RECVMASTER);
                    XLRCODE(u32recvOpt  = SS_OPT_FPDPNRASSERT);
                } else {
                    // Amazon or Amazon/Express
                    XLRCODE(u32recvMode = SS_FPDPMODE_RECVM);
                    XLRCODE(u32recvOpt  = SS_DBOPT_FPDPNRASSERT);
                }
                XLRCALL( ::XLRSetDBMode(ss, u32recvMode, u32recvOpt) );
            }

            // Start the recording. depending or fork or !fork
            // we have to:
            // * update the scandir on the discpack (if fork'ing)
            // * call a different form of 'start recording'
            //   to make sure that disken are not overwritten
            //
            // On the Mark5C we cannot already start the recording.
            // We do that in the
            // in2net_transfer<Mark5>::start()/stop()/pause()/resume()
            // hooks (the specializations for Mark5 == mark5c)
            // Immediate modes can start always and the not-immediate
            // ones only if not on a Mark5C
            if( isfork(rtm) ) {
                std::string scan_label = scan_label::create_scan_label(scan_label::command, scanname);
                scanptr = rte.xlrdev.startScan( scan_label );

                // when fork'ing we do not attempt to record for ever
                // (WRAP_ENABLE==1) otherwise the disken could be overwritten
                if( !m5c ) {
                    XLRCALL( ::XLRAppend(ss) );
                }
            } else {
                // in2net can run indefinitely
                // 18/Mar/2011 - As per communication with Cindy Gold
                //               of Conduant Corp. (the manuf. of the
                //               Mark5-en) MODE_PASSTHRU should imply
                //               WRAP_ENABLE==false. Or rather:
                //               the wording was "wrap-enable was never
                //               meant to apply to nor tested in
                //               passthru mode"
                if( !m5c ) {
                    XLRCALL( ::XLRRecord(ss, XLR_WRAP_DISABLE/*XLR_WRAP_ENABLE*/, 1) );
                }
            }

            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // constrain sizes based on network parameters and optional
            // compression. If this is the Mark5A version of 
            // in2{net|fork} it can only yield mark4/vlba data and for
            // these formats the framesize/offset is irrelevant for
            // compression since each individual bitstream has full
            // headerinformation.
            // If, otoh, we're running on a mark5b we must look for
            // frames first and compress those.
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            throw_on_insane_netprotocol(rte);
                
            // come up with a theoretical ipd
            compute_theoretical_ipd(rte);
                
            // The hardware has been configured, now start building
            // the processingchain.

            // All these transfers start with a fiforeader.
            // Those transfers that need to go immediately, indicate so
            fiforeaderargs   fra( &rte );

            fra.run        = immediate;
            fifostep[&rte] = c.add(&fiforeader, 10, fra);

            if( toqueue(rtm) ) {
                c.add(&queue_writer, queue_writer_args(&rte));
            } else {
                // If compression requested then insert that step now
                if( rte.solution ) {
                    // In order to prevent bitshift (when the datastream
                    // does not start exactly at the start of a dataframe)
                    // within a dataframe (leading to throwing away the
                    // wrong bitstream upon compression) we MUST always
                    // insert a framesearcher.
                    // This guarantees that only intact frames are sent
                    // to the compressor AND the compressor knows exactly
                    // where all the bits of the bitstreams are
                    compressorargs cargs( &rte );

                    DEBUG(0, args[0] << ": enabling compressor " << dataformat << std::endl);
                    if( dataformat.valid() ) {
                        c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                        c.add(&framecompressor, 10, compressorargs(&rte));
                    } else {
                        c.add(&blockcompressor, 10, &rte);
                    }
                }

                // Write to file or to network
                if( tofile(rtm) ) {
                    c.register_cancel(c.add(&fdwriter<block>, &open_file, filename, &rte),
                                      &close_filedescriptor);
                } else  {
                    // and finally write to the network
                    c.register_cancel(c.add(&netwriter<block>, &net_client, networkargs(&rte)),
                                      &close_filedescriptor);
                }
            }

            rte.transfersubmode.clr_all();
            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing
            rte.transfermode    = rtm;

            // Kick off immediate transfers
            if( immediate ) {
                rte.transfersubmode.clr_all().set(run_flag);
                in2net_transfer<Mark5>::start(rte);
            }

            // The very last thing we do is to start the
            // system - running the chain may throw up and we shouldn't
            // be in an indefinite state
            rte.processingchain = c;
            rte.processingchain.run();

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }
    // <on> : turn on dataflow, only allowed for non immediate commands
    if( args[1]=="on" && !immediate ) {
        const transfer_submode tsm( rte.transfersubmode );

        recognized = true;
        // && has the connected flag +
        //   does not have the broken flag +
        //   either not started yet (!runflag && !pauseflag) OR
        //   started but paused (runflag && pause)
        if( rte.transfermode==no_transfer ) {
            reply << " 6 : not doing anything ;";
        } else {
            // Great, we're potentially doing something
            if( (tsm&broken_flag)==true ) {
                reply << " 4 : transfer is broken, check terminal output ;";
            } else {
                // If we're not broken but also not connected
                // there's little to do
                if( (tsm&connected_flag)==false ) {
                    reply << " 6 : not yet connected ;";
                } else {
                    // Phew. We're doing something, connected, ánd not
                    // broken. Now check if we have a consistent
                    // run+pause state - only acceptable is if both are
                    // equal
                    // (Either:
                    //      !run && !pause   .OR.
                    //      run  &&  pause
                    if( (tsm&run_flag)==(tsm&pause_flag) ) {
                        // If not running yet, start the transfer.
                        // Otherwise we were already running and all we
                        // need to do is re-enable the inputclock.
                        if( !(rte.transfersubmode&run_flag) ) {
                            in2net_transfer<Mark5>::start(rte);
                            // Note: the fiforeader will set the "run" flag
                            rte.processingchain.communicate(fifostep[&rte], &fiforeaderargs::set_run, true);
                        } else {
                            // resume the hardware
                            in2net_transfer<Mark5>::resume(rte);
                        }

                        // no matter which transfer we were doing, we must clear the
                        // pauseflag
                        rte.transfersubmode.clr( pause_flag );
                        reply << " 0 ;";
                    } else {
                        // inconsistent state
                        if( tsm&run_flag )
                            reply << " 6 : not paused ;";
                        else
                            reply << " 6 : not running ;";
                    }
                }
            }
        }
    }
    // <off> == pause for non-immediate transfers
    if( args[1]=="off" && !immediate ) {
        recognized = true;
        // only allow if submode has the run and not the pause flag
        if( rte.transfermode!=no_transfer ) {
            if( (rte.transfersubmode&broken_flag)==false ) {
                if( (rte.transfersubmode&run_flag)==true ) {
                    if( (rte.transfersubmode&pause_flag)==false ) {
                        // Pause the recording
                        in2net_transfer<Mark5>::pause(rte);

                        // indicate paused state
                        rte.transfersubmode.set( pause_flag );
                        reply << " 0 ;";
                    } else {
                        // already paused
                        reply << " 6 : already paused ;";
                    }
                } else {
                    // not running yet!
                    reply << " 6 : not running yet ;";
                }
            } else {
                reply << " 4 : transfer is broken, check terminal output ;";
            }
        } else {
            // not doing a transfer
            // transfermode is either no_transfer or {in2net|in2fork}, nothing else
            reply << " 6 : not doing " << args[0] << " at all;";
        }
    }
    // <disconnect> (non-immediate) and <off> for an immediate command
    // finalize the transfer
    if( (args[1]=="disconnect" && !immediate) ||
        (args[1]=="off" && immediate) ) {
        recognized = true;
        // Only allow if we're doing in2net.
        // Don't care if we were running or not
        if( rte.transfermode!=no_transfer ) {
            std::string error_message;
            try {
                if( rte.transfermode==in2memfork && m5b ) {
                    // if we are actually recording on a mark5b, we need to stop on the second tick, first pause the ioboard
                    rte.ioboard[ mk5breg::DIM_PAUSE ] = 1;

                    // wait one second, to be sure we got a 1pps
                    pcint::timeval_type start( pcint::timeval_type::now() );
                    pcint::timediff     tdiff = pcint::timeval_type::now() - start;
                    while ( tdiff < 1 ) {
                        ::usleep( (unsigned int)((1 - tdiff) * 1.0e6) );
                        tdiff = pcint::timeval_type::now() - start;
                    }

                    // then stop the ioboard
                    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;
                    rte.ioboard[ mk5breg::DIM_PAUSE ] = 0;
                }
                else {
                    // whatever we were doing make sure it's stopped
                    in2net_transfer<Mark5>::stop(rte);
                }
            }
            catch ( std::exception& e ) {
                error_message += std::string(" : Failed to stop I/O board: ") + e.what();
            }
            catch ( ... ) {
                error_message += std::string(" : Failed to stop I/O board, unknown exception");
            }

            try {
                // do a blunt stop. at the sending end we do not care that
                // much processing every last bit still in our buffers
                rte.processingchain.stop();
            }
            catch ( std::exception& e ) {
                error_message += std::string(" : Failed to stop processing chain: ") + e.what();
            }
            catch ( ... ) {
                error_message += std::string(" : Failed to stop processing chain, unknown exception");
            }

            try {
                // stop the device
                // As per the SS manual need to call 'XLRStop()'
                // twice: once for stopping the recording
                // and once for stopping the device altogether?
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                // Need to do bookkeeping if in2fork was active
                if( isfork(rtm) ) {
                    rte.xlrdev.finishScan( scanptr );
                    rte.pp_current = scanptr.start();
                    rte.pp_end = scanptr.start() + scanptr.length();
                    rte.current_scan = rte.xlrdev.nScans() - 1;

                    if( rte.disk_state_mask & runtime::record_flag )
                        rte.xlrdev.write_state( "Recorded" );
                }

            }
            catch ( std::exception& e ) {
                error_message += std::string(" : Failed to stop streamstor: ") + e.what();
                if ( isfork(rtm) ) {
                    rte.xlrdev.stopRecordingFailure();
                }
            }
            catch ( ... ) {
                error_message += std::string(" : Failed to stop streamstor, unknown exception");
                if ( isfork(rtm) ) {
                    rte.xlrdev.stopRecordingFailure();
                }
            }
            

            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();

            if ( error_message.empty() ) {
                reply << " 0 ;";
            }
            else {
                reply << " 4" << error_message << " ;";
            }
        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

#endif
