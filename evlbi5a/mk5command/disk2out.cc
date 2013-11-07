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
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <iostream>

using namespace std;


// disk2out (alias for 'play') 
// should work on both Mark5a and Mark5B/DOM
typedef per_runtime<pthread_t>    threadmap_type;

string disk2out_fn(bool qry, const vector<string>& args, runtime& rte) {
    // keep a mapping of runtime -> delayed_play thread such that we
    // can cancel it if necessary
    static threadmap_type delay_play_map;

    // automatic variables
    ostringstream       reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query should always be possible, command only when not doing anything
    // or already doing disk2out.
    INPROGRESS(rte, reply,
               !(qry || rte.transfermode==no_transfer || rte.transfermode==disk2out))

    // Good, if query, tell'm our status
    if( qry ) {
        // we do not implement 'arm' so we can only be in one of three states:
        // waiting, off/inactive, on
        if( rte.transfermode==disk2out ) {
            // depending on 'wait' status (implies delayed play) indicate that
            if( rte.transfersubmode&wait_flag )
                reply << " 1 : waiting ;";
            else if ( rte.transfersubmode&run_flag ) {
                // check if we are still playing, otherwise we are halted
                S_DEVSTATUS dev_status;
                XLRCALL( ::XLRGetDeviceStatus(rte.xlrdev.sshandle(), &dev_status) );
                if ( dev_status.Playing ) {
                    reply << " 0 : on ;";
                }
                else {
                    reply << " 0 : halted ;";
                }
            }
            else {
                BOOLEAN option_on;
                XLRCALL( ::XLRGetOption(rte.xlrdev.sshandle(), SS_OPT_PLAYARM, &option_on) );
                if ( option_on ) {
                    UINT buffer_status;
                    XLRCALL( ::XLRGetPlayBufferStatus(rte.xlrdev.sshandle(), &buffer_status) );
                    if ( buffer_status == SS_PBS_FULL ) {
                        reply << " 0 : armed ;";
                    }
                    else if ( buffer_status == SS_PBS_FILLING ) {
                        reply << " 1 : arming ;";
                    }
                    else {
                        reply << " 4 : inconsistent play buffer status while arming (" << buffer_status << ") ;";
                    }
                }
                else {
                    reply << " 4 : play not arming, waiting or playing: undefined state ;";
                }
            }
        } else {
            reply << " 0 : off ;";
        }
        return reply.str();
    }

    // Handle command, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;

    // do the start byte parsing here, as it's required in both "on" and "arm"
    if ( (args[1] == "on") || (args[1] == "arm") ) {
        if ( args[0] == "play" ) {
            // in the case of scan_play we always get the start byte from runtime
            // have to parse it for play
            const string    ppargstr( OPTARG(2, args) );
            // Playpointer given? [only when disk2out/play
            if( ppargstr.empty()==false ) {
                uint64_t v;
                    
                ASSERT2_COND( ::sscanf(ppargstr.c_str(), "%" SCNu64, &v)==1,
                              SCINFO("start-byte# is out-of-range") );
                rte.pp_current.Addr = v;
            }
        }
    }

    // <on>[:<playpointer>[:<ROT>]]
    if( args[1]=="on" ) {
        recognized = true;
        // If ROT is given, then the playback will start at
        // that ROT for the given taskid [aka 'delayed play'].
        // If no taskid set or no rot-to-systemtime mapping
        // known for that taskid we FAIL.
        if( (rte.transfermode==no_transfer) || !((rte.transfersubmode&wait_flag)|(rte.transfersubmode&run_flag)) ) { // not doing anything or arming
            double          rot( 0.0 );
            XLRCODE(SSHANDLE  ss = rte.xlrdev.sshandle());
            const string    rotstr( OPTARG(3, args) );

            // ROT given? (if yes AND >0.0 => delayed play)
            if( rotstr.empty()==false ) {
                threadmap_type::iterator   thrdmapptr;

                rot = ::strtod( rotstr.c_str(), 0 );

                // only allow if >0.0 AND taskid!=invalid_taskid
                ASSERT_COND( (rot>0.0 && rte.current_taskid!=runtime::invalid_taskid) );

                // And there should NOT already be a delayed-play entry for
                // the current 'runtime'
                thrdmapptr = delay_play_map.find( &rte );
                ASSERT2_COND( (thrdmapptr==delay_play_map.end()),
                              SCINFO("Internal error: an entry for the current rte "
                                     "already exists in the delay-play-map.") );
            }

            // Good - independent of delayed or immediate play, we have to set up
            // the Streamstor device the same.
            // If we are armed, we already did that
            BOOLEAN option_on;
            XLRCALL( ::XLRGetOption(ss, SS_OPT_PLAYARM, &option_on) );
            if ( !option_on ) {
                // if this is scan pay set the play limit to the scan length
                if ( args[0] == "scan_play" ) {
                    if ( rte.pp_end < rte.pp_current ) {
                        reply << " 6 : scan start pointer is set beyond scan end pointer ;";
                        return reply.str();
                    }
                    XLRCODE( playpointer l = (rte.pp_end - rte.pp_current) );
                    XLRCALL( ::XLRSetPlaybackLength(ss, l.AddrHi, l.AddrLo) );
                }
                XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRBindInputChannel(ss, 0) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
            }

            // we create the thread always - an immediate play
            // command is a delayed-play with a delay of zero ...
            // afterwards we do bookkeeping.
            sigset_t       oss, nss;
            pthread_t      dplayid;
            dplay_args     thrdargs;
            pthread_attr_t tattr;

            // prepare the threadargument
            thrdargs.rot      = rot;
            thrdargs.rteptr   = &rte;
            thrdargs.pp_start = rte.pp_current;

            // reset statistics counters
            rte.statistics.clear();

            // set up for a detached thread with ALL signals blocked
            ASSERT_ZERO( sigfillset(&nss) );
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
    //  play=off [: <playpointer>]
    //  cancels delayed play/stops playback
    if( args[1]=="off" ) {
        recognized = true;
        if( rte.transfermode==disk2out ) {
            try {
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

                // Update the current playpointer
                rte.pp_current += ::XLRGetPlayLength(sshandle);

                // disable arming and play length (scan play can be stopped with play)
                XLRCALL( ::XLRSetPlaybackLength(sshandle, 0, 0) );
                XLRCALL( ::XLRClearOption(sshandle, SS_OPT_PLAYARM) );
                XLRCALL( ::XLRClearChannels(sshandle) );
                XLRCALL( ::XLRBindOutputChannel(sshandle, 0) );

                if ( rte.disk_state_mask & runtime::play_flag ) {
                    rte.xlrdev.write_state( "Played" );
                }
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop play: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop play, unknown exception ;";
            }
            
            // return to idle status
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

        } else {
            // nothing to stop!
            reply << " 6 : inactive ;";
        }
        // irrespective of what we were doing, if the user said
        // play = off : <playpointer>  we MUST update our current
        // playpointer to <playpointer>. This is, allegedly, the only
        // way to force the system to to data_check at a given position.
        if( args.size()>2 && !args[2].empty() ) {
            uint64_t v;

            ASSERT2_COND( ::sscanf(args[2].c_str(), "%" SCNu64, &v)==1,
                          SCINFO("start-byte# is out-of-range") );
            rte.pp_current = v;
        }
    }
    if (args[1] == "arm") {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            XLRCODE(SSHANDLE  ss = rte.xlrdev.sshandle());

            // if this is scan pay set the play limit to the scan length
            if ( args[0] == "scan_play" ) {
                XLRCODE( playpointer l = rte.xlrdev.getScan(rte.current_scan).length() );
                XLRCALL( ::XLRSetPlaybackLength(ss, l.AddrHi, l.AddrLo) );
            }

            XLRCALL( ::XLRSetOption(ss, SS_OPT_PLAYARM) );
            XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
            XLRCALL( ::XLRBindInputChannel(ss, 0) );
            XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
            XLRCALL( ::XLRPlayback(ss, rte.pp_current.AddrHi, rte.pp_current.AddrLo) );

            rte.transfersubmode.clr_all();
            rte.transfermode = disk2out;
                
            reply << " 0 ;";
        }
        else {
            reply << " 6 : arm already set ;";
        }
            
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}
