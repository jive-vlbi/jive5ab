// implementation of the commands
// Copyright (C) 2007-2008 Harro Verkouter
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
//
// * generic Mk5 commands [Mk5 hardware agnostic]
// * generic jive5a commands [ipd, pdr, tstat, mtu, ...]
// * specializations for
//      - Mk5A
//      - Mk5B flavour agnostic but Mk5B specific
//      - Mk5B/DIM
//      - Mk5B/DOM
// * commandmaps which define which of the commands
//   are allowed for which Mk5 flavour.
//   Currently there's 3 commandmaps:
//      - Mk5A
//      - Mk5B/DIM
//      - Mk5B/DOM
// * Utility functions for Mk5's
//   (eg programming Mk5B/DIM input section for recording:
//    is shared between dim2net and in2disk)
#include <mk5command.h>
#include <dosyscall.h>
#include <threadfns.h>
#include <playpointer.h>
#include <evlbidebug.h>
#include <getsok.h>
#include <streamutil.h>
#include <userdir.h>
#include <busywait.h>
#include <dotzooi.h>
#include <dayconversion.h>
#include <ioboard.h>
#include <stringutil.h>
#include <trackmask.h>
#include <sciprint.h>
#include <version.h>
#include <jive5a_bcd.h>
#include <timezooi.h>
#include <interchainfns.h>
#include <data_check.h>
#include <mk5_exception.h>

// c++ stuff
#include <map>
#include <string>
#include <iostream>
#include <fstream>
#include <string>

// for setsockopt
#include <sys/types.h>
#include <sys/socket.h>

// and for "struct timeb"/ftime()
#include <sys/timeb.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>  // ::unlink()
// for log/exp/floor
#include <math.h>
// zignal ztuv
#include <signal.h>

// inet functions
#include <netinet/in.h>
#include <arpa/inet.h>

// open(2)
#include <fcntl.h>

// for POSIX strcasecmp
#include <strings.h>

// for [u]intN_t and stuff
#include <stdint.h>
#include <inttypes.h>

// LLONG_MAX and friends
#include <limits.h>

// for VSN verification
#include <regex.h>

using namespace std;


// Create the code for the exception
DEFINE_EZEXCEPT(cmdexception)

#define KB  (1024)
#define MB  (KB*KB)

#define GETSSHANDLE(rte) (rte.xlrdev.sshandle())

// Since the actual functions typically operate on a runtime environment
// and sometimes they need to remember something, it makes sense to do
// this on a per-runtime basis. This struct allows easy per-runtime
// saving of state.
// Usage:
//   per_runtime<string>  lasthost;
//   ...
//   swap(rte.netparms.host, lasthost[&rte]);
//   cout << lasthost[&rte] << endl;
//   lasthost[&rte] = "foo.bar.bz";
template <typename T>
struct per_runtime:
    public std::map<runtime const*, T>
{};

// returns the value of s[n] provided that:
//  s.size() > n
// otherwise returns the empty string
#define OPTARG(n, s) \
    ((s.size()>n)?s[n]:string())

// help function to transfer between user interface and ioboard values
mk5areg::regtype::base_type track2register( unsigned int track ) {
    if ( 2 <= track && track <= 33 ) {
        return track - 2;
    }
    if ( 102 <= track && track <= 133 ) {
        return track - 102 + 32;
    }
    THROW_EZEXCEPT(cmdexception, "track (" << track << ") not in allowed ranges [2, 32] or [102,133]");
}

unsigned int register2track( mk5areg::regtype::base_type reg ) {
    if ( reg < 32 ) {
        return reg + 2;
    }
    else {
        return reg + 102 - 32;
    }
}




// function prototype for fn that programs & starts the
// Mk5B/DIM disk-frame-header-generator at the next
// available moment.
void start_mk5b_dfhg( runtime& rte, double maxsyncwait = 3.0 );


// Based on the information found in the runtime compute
// the theoretical IPD. 
// YOU MUST HAVE FILLED "rte.sizes" WITH THE RESULT OF A constrain()
// FUNCTION CALL BEFORE ACTUALLY CALLING THIS ONE!
void compute_theoretical_ipd( runtime& rte ) {
    netparms_type&     net( rte.netparms );
    const unsigned int datagramsize( rte.sizes[constraints::write_size] );

    if( datagramsize>0 ) {
        // total bits-per-second to send divided by the mtu (in bits)
        //  = number of packets per second to send. from this follows
        //  the packet spacing trivially
        // the trackbitrate already includes headerbits; both
        // for VLBA non-data-replacement bitrate and for Mark4 datareplacement.
        // the amount of headerbits for Mk5B format is marginal wrt total
        // bitrate.
        // TODO: take compression into account
        //       30 Jun 2010 HV - hopefully done.
        //
        // 20 Aug 2010: HV - ipd @ 1Gbps comes out as 124 us which is too
        //                   large; see FIFO filling up. Decided to add
        //                   a 0.9 fraction to the theoretical ipd
        const double correctionfactor( 0.9 );
        const double factor((rte.solution)?(rte.solution.compressionfactor()):1.0);
        const double n_pkt_p_s = ((rte.ntrack() * rte.trackbitrate() * factor) / (datagramsize*8)) * correctionfactor;

        // Note: remember! ipd should be in units of microseconds
        //       previous computation (before fix) yielded units 
        //       of seconds .. d'oh!
        if( n_pkt_p_s>0.0 ) {
            // floor(3) the value into integral microseconds;
            // the IPD can better be too small rather than too high.
            net.theoretical_ipd = (int) ::floor(1.0e6/n_pkt_p_s);
            DEBUG(1, "compute_theoretical_ipd: " << net.theoretical_ipd << "us" << endl);
        }
    }
    return;
}

#define BANKID(num) ((num==0)?((UINT)BANK_A):((num==1)?((UINT)BANK_B):(UINT)-1))

// Bank handling commands
// Do both "bank_set" and "bank_info" - most of the logic is identical
// Also handle the execution of disk state query (command is handled in its own function)
string bankinfoset_fn( bool qry, const vector<string>& args, runtime& rte) {
    const unsigned int  inactive = (unsigned int)-1;
    const char          bl[] = {'A', 'B'};
    S_BANKSTATUS        bs[2];
    unsigned int        bidx[] = {inactive, inactive};
    unsigned int        selected = inactive;
    transfer_type       ctm = rte.transfermode;
    ostringstream       reply;
    const S_BANKMODE    curbm = rte.xlrdev.bankMode();


    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('='));

    // bank_info is only available as query
    if( args[0]=="bank_info" && !qry ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Can't do bank_set if we're doing anything with the disks
    if( args[0]=="bank_set" && ctm!=no_transfer && !qry ) {
        reply << " 6 : cannot change banks whilst doing " << ctm << " ;";
        return reply.str();
    }

    if ( rte.transfermode == condition ) {
        reply << " 6 : not possible during " << rte.transfermode << " ;";
        return reply.str();
    }
    
    // If we're not in bankmode none of these can return anything
    // sensible, isn't it?
    if( curbm!=SS_BANKMODE_NORMAL && curbm!=SS_BANKMODE_AUTO_ON_FULL ) {
        reply << " 6 : not in bank mode ; ";
        return reply.str();
    }

    // Ok. Inspect the banksz0rz!
    XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_A, &bs[0]) );
    XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_B, &bs[1]) );

    for(unsigned int bnk=0, bidxidx=0; bnk<2; bnk++ ) {
        if( bs[bnk].State==STATE_READY )
            bidx[bidxidx++] = bnk;
        if( bs[bnk].Selected ) 
            selected = bnk;
    }
   
    // *No* active banks
    if( bidx[0]==inactive ) {
        reply << " 0 : - :   : - :   ;";
        return reply.str();
    }

    // If we're doing bank_set as a command ...
    if( args[0]=="bank_set" && !qry ) {
        int          code = 0;
        string       bank_str   = ::toupper(OPTARG(1, args));
        unsigned int banknum;

        ASSERT2_COND( bank_str.empty()==false,
                      SCINFO("You must specify which bank to set active" ));
        ASSERT2_COND( (bank_str!="INC") || (bank_str=="INC" && selected!=inactive),
                      SCINFO("No bank selected so can't toggle") );

        // we've already verified that there *is* a bank selected
        // [if "inc" is requested]
        if( bank_str=="INC" )
            bank_str = bl[ !selected ];

        ASSERT2_COND( bank_str=="A" || bank_str=="B",
                      SCINFO("invalid bank requested") );

        banknum = bank_str[0] - 'A';

        // If the indicated bank is not the selected one
        // and it's online and the media is not faulty,
        // *then* we can do this!
        if( banknum!=selected ) {
            if( bs[banknum].State==STATE_READY ) {
                code = 1;
                XLRCALL( ::XLRSelectBank(rte.xlrdev.sshandle(), BANKID(banknum)) );
                // force a check of mount status
                rte.xlrdev.update_mount_status();
            } else {
                code = 4;
            }
        }
        reply << code << " ;";
        return reply.str();
    }

    // Here we handle the query case for bank_set and bank_info
    reply << " 0 ";
    for(unsigned int i=0; i<2; i++) {
        // output the selected bank first if any
        unsigned int selected_index = i;
        if (selected == 1) {
            selected_index = 1 - i;
        }
        if( bidx[selected_index]==inactive ) {
            if( args[0]=="bank_info" )
                reply << ": - : 0 ";
            else
                reply << ": - :   ";
        } else {
            const S_BANKSTATUS&  bank( bs[ bidx[selected_index] ] );
            reply << ": " << bl[ bidx[selected_index] ] << " : ";
            if( args[0]=="bank_info" ) {
                long page_size = ::sysconf(_SC_PAGESIZE);
                reply << ((bank.TotalCapacity * (uint64_t)page_size) - bank.Length);
            } else {
                pair<string, string> vsn_state = disk_states::split_vsn_state(string(bank.Label));
                if ( args[0] == "bank_set" ) {
                    reply << vsn_state.first;
                }
                else { // disk_state
                    reply << vsn_state.second;
                }
            }
            reply << " ";
        }
    }
    reply << ";";
    return reply.str();
}

string disk_state_fn( bool qry, const vector<string>& args, runtime& rte) {
    if ( qry ) {
        // forward it to bankinfo_fn
        return bankinfoset_fn( qry, args, rte );
    }

    // handle the command
    ostringstream reply;
    reply << "!" << args[0] << ((qry)?('?'):('='));
    
    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot set state while doing " << rte.transfermode << " ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        reply << " 8 : command requires an argument ;";
        return reply.str();
    }

    if ( rte.protected_count == 0 ) {
        reply << " 6 : need an immediate preceding protect=off ;";
        return reply.str();
    }
    // ok, we're allowed to write state

    if ( args[1].empty() ) {
        reply << " 8 : argument is empty ;";
        return reply.str();
    }

    string new_state = args[1];
    new_state[0] = ::toupper(new_state[0]);

    if ( disk_states::all_set.find(new_state) == disk_states::all_set.end() ) {
        reply << " 8 : unrecognized disk state ;";
        return reply.str();
    }

    rte.xlrdev.write_state( new_state );
    
    reply << " 0 : " << new_state << " ;";
    return reply.str();
}

string disk_state_mask_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << ((qry)?('?'):('='));

    const runtime::disk_state_flags flags[] = { runtime::erase_flag, runtime::play_flag, runtime::record_flag };
    const size_t num_flags = sizeof(flags)/sizeof(flags[0]);

    if ( qry ) {
        reply << " 0";
        for ( size_t i = 0; i < num_flags; i++ ) {
            reply << " : " << ( (rte.disk_state_mask & flags[i]) ? "1" : "0");
        }
        reply << " ;";
        return reply.str();
    }

    // handle command
    if ( args.size() != (num_flags + 1) ) {
        reply << " 8 : command required " << num_flags << " arguments ;";
        return reply.str();
    }

    unsigned int new_state_mask = 0;
    for ( size_t i = 0; i < num_flags; i++ ) {
        if ( args[i+1] == "1" ) {
            new_state_mask |= flags[i];
        }
        else if ( args[i+1] != "0" ) {
            reply << " 8 : all arguments should be either 0 or 1 ;";
            return reply.str();
        }
    }
    rte.disk_state_mask = new_state_mask;

    reply << " 0";
    for ( size_t i = 0; i < num_flags; i++ ) {
        reply << " : " << args[i+1];
    }
    reply << " ;";
    
    return reply.str();
}

// turns on/off automatic switching to other bank when a disk is complete
string bank_switch_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << ((qry)?('?'):('='));

    const S_BANKMODE curbm = rte.xlrdev.bankMode();
    if ( qry ) {
        if ( curbm == SS_BANKMODE_NORMAL ) {
            reply << " 0 : off ;";
        }
        else if ( curbm == SS_BANKMODE_AUTO_ON_FULL ) {
            reply << " 0 : on ;";
        }
        else {
            reply << " 6 : not in bank mode ;";
        }
    }
    else {
        if ( args.size() < 2 ) {
            reply << " 8 : no mode parameter;";
        }
        else {
            if ( args[1] == "on" ) {
                if ( curbm != SS_BANKMODE_AUTO_ON_FULL ) {
                    rte.xlrdev.setBankMode( SS_BANKMODE_AUTO_ON_FULL );
                }
                reply << " 0 ;";
            }
            else if ( args[1] == "off" ) {
                if ( curbm != SS_BANKMODE_NORMAL ) {
                    rte.xlrdev.setBankMode( SS_BANKMODE_NORMAL );
                }
                reply << " 0 ;";
            }
            else {
                reply << " 8 : mode parameters should be 'on' or 'off' ;";
            }
        }
    }

    return reply.str();
}

string dir_info_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << ((qry)?('?'):('='));

    if( !qry ) {
        reply << " 6 : only available as query ;";
        return reply.str();
    }

    if ( rte.transfermode == condition ) {
        reply << " 6 : not possible during " << rte.transfermode << " ;";
        return reply.str();
    }
    
    const S_BANKMODE    curbm = rte.xlrdev.bankMode();

    if( curbm==SS_BANKMODE_DISABLED ) {
        reply << " 6 : not in bank mode ; ";
        return reply.str();
    }

    S_BANKSTATUS bs[2];
    XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_A, &bs[0]) );
    XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_B, &bs[1]) );

    long page_size = ::sysconf(_SC_PAGESIZE);
    unsigned int active_index;
    if( bs[0].State==STATE_READY && bs[0].Selected ) {
        active_index = 0;
    }
    else if ( bs[1].State==STATE_READY && bs[1].Selected ) {
        active_index = 1;
    }
    else {
        reply << " 6 : no active bank ;";
        return reply.str();
    }

    reply << " 0 : " << rte.xlrdev.nScans() << " : " << bs[active_index].Length << " : " << (bs[active_index].TotalCapacity * (uint64_t)page_size) << " ;";

    return reply.str();
}


//
//
//   The Mark5 commands
//
//


struct disk2netguardargs_type {
    runtime*    rteptr;

    disk2netguardargs_type( runtime& r ) : 
        rteptr(&r)
    {}
    
private:
    disk2netguardargs_type();
};

void* disk2netguard_fun(void* args) {
    // takes ownership of args
    disk2netguardargs_type* guard_args = (disk2netguardargs_type*)args;
    try {
        // wait for the chain to finish processing
        guard_args->rteptr->processingchain.wait();
        
        if ( (guard_args->rteptr->transfermode == disk2net) && (guard_args->rteptr->disk_state_mask & runtime::play_flag) ) {
            guard_args->rteptr->xlrdev.write_state( "Played" );
        }

        RTEEXEC( *guard_args->rteptr, guard_args->rteptr->transfermode = no_transfer; guard_args->rteptr->transfersubmode.clr( run_flag ) );

    }
    catch ( const std::exception& e) {
        DEBUG(-1, "disk2net execution threw an exception: " << e.what() << std::endl );
        guard_args->rteptr->transfermode = no_transfer;
    }
    catch ( ... ) {
        DEBUG(-1, "disk2net execution threw an unknown exception" << std::endl );        
        guard_args->rteptr->transfermode = no_transfer;
    }

    delete guard_args;
    return NULL;
}



// Support disk2net, file2net and fill2net
string disk2net_fn( bool qry, const vector<string>& args, runtime& rte) {
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer ||
           (args[0] == "disk2net" && ctm==disk2net) ||
           (args[0] == "fill2net" && ctm==fill2net) ||
           (args[0] == "file2net" && ctm==file2net)
           );

    // If we aren't doing anything nor doing disk/fill 2 net - we shouldn't be here!
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( ctm==no_transfer ) {
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
            if( args[0] == "disk2net" ) {
                // prepare disken/streamstor
                XLRCALL( ::XLRSetMode(GETSSHANDLE(rte), SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRBindOutputChannel(GETSSHANDLE(rte), CHANNEL_PCI) );
                c.add(&diskreader, 10, diskreaderargs(&rte));
            } 
            else if ( args[0] == "file2net" ) {
                const string filename( OPTARG(3, args) );
                if ( filename.empty() ) {
                    reply <<  " 8 : need a source file ;";
                    return reply.str();
                }
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
                    fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'start' value") );
                }
                if( inc_s.empty()==false ) {
                    fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.inc==0 && eocptr==inc_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'inc' value") );
                }
                if( realtime_s.empty()==false ) {
                    long           num;
                    num = ::strtol(realtime_s.c_str(), &eocptr, 10);
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

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();
                
            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode = (args[0] == "disk2net" ? disk2net:
                                (args[0] == "fill2net" ? fill2net : file2net));

            // create a thread to automatically stop the transfer when done
            pthread_t thread_id;
            pthread_attr_t tattr;
            PTHREAD_CALL( ::pthread_attr_init(&tattr) );
            PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) );
            disk2netguardargs_type* guard_args = new disk2netguardargs_type( rte );
            PTHREAD2_CALL( ::pthread_create( &thread_id, &tattr, disk2netguard_fun, guard_args ),
                           delete guard_args );
        
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }

    // <on> : turn on dataflow
    //   disk2net=on[:[<start_byte>][:<end_byte>|+<amount>][:<repeat:0|1>]]
    //   file2net=on
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
                    end = 0;
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
                reply << ( rte.transfermode == fill2net ? " 0 ;" : " 1 ;" );
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

// disk2out (alias for 'play') 
// should work on both Mark5a and Mark5B/DOM
typedef std::map<runtime*, pthread_t> threadmap_type;

string disk2out_fn(bool qry, const vector<string>& args, runtime& rte) {
    // keep a mapping of runtime -> delayed_play thread such that we
    // can cancel it if necessary
    static threadmap_type delay_play_map;

    // automatic variables
    ostringstream       reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing disk2out - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=disk2out ) {
        reply << " 6 : _something_ is happening and its NOT disk2out(play)!!! ;";
        return reply.str();
    }

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

// set/query the taskid
string task_id_fn(bool qry, const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        const unsigned int tid = rte.current_taskid;

        reply << " 0 : ";
        if( tid==runtime::invalid_taskid )
            reply << "none";
        else 
            reply << tid;
        reply << " ;";
        return reply.str();
    }

    // check if argument given and if we're not doing anything
    if( args.size()<2 ) {
        reply << " 8 : no taskid given ;";
        return reply.str();
    }

    if( rte.transfermode!=no_transfer ) {
        reply << " 6 : cannot set/change taskid during " << rte.transfermode << " ;";
        return reply.str();
    }

    // Gr8! now we can set the actual taskid
    if( args[1]=="none" )
        rte.current_taskid = runtime::invalid_taskid;
    else
        rte.current_taskid = (unsigned int)::strtol(args[1].c_str(), 0, 0);
    reply << " 0 ;";

    return reply.str();
}

// query the current constraints - only available as query
string constraints_fn(bool , const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    reply << "!" << args[0] << "= 0 : "
          << rte.ntrack() << "tr : " << rte.trackformat() << " : " << rte.trackbitrate() << "bps/tr : "
          << rte.sizes << " ;";
    return reply.str();
}


unsigned int bufarg_getbufsize(chain* c, chain::stepid s) {
    return c->communicate(s, &buffererargs::get_bufsize);
}

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
                fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                // !(A || B) => !A && !B
                EZASSERT2( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                           cmdexception, EZINFO("Failed to parse 'start' value") );
            }
            if( inc_s.empty()==false ) {
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

// set up net2out, net2disk 
string net2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // This points to the scan being recorded, if any
    static ScanPointer                scanptr;    
    static per_runtime<string>        hosts;
    static per_runtime<curry_type>    oldthunk;
    static per_runtime<chain::stepid> servo_stepid;

    // automatic variables
    bool                atm; // acceptable transfer mode
    const bool          is_mk5a( rte.ioboard.hardware() & ioboard_type::mk5a_flag );
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode
    const transfer_type rtm = string2transfermode(args[0]); // requested transfer mode
    const bool          disk = todisk(rtm);
    const bool          out = toout(rtm);


    EZASSERT2(rtm==net2out || rtm==net2disk || rtm==net2fork, cmdexception,
              EZINFO("Requested transfermode " << args[0] << " not serviced by this function"));


    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==rtm);

    // If we aren't doing anything nor the requested transfer is not the one
    // running - we shouldn't be here!
    if( !atm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
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
            reply << " : " << 0 /*rte.nbyte_from_mem*/;
        }
        // this displays the flags that are set, in HRF
        //reply << " : " << rte.transfersubmode;
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
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // If we're doing net2out on a Mark5B(+) we
            // cannot accept Mark4/VLBA data.
            // A Mark5A+ can accept Mark5B data ("mark5a+ mode")
            if( rtm==net2out && !is_mk5a )  {
                ASSERT2_COND(rte.trackformat()==fmt_mark5b,
                             SCINFO("net2out on Mark5B can only accept Mark5B data"));
            }

            // Constrain the transfer sizes based on the three basic
            // parameters
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // depending on disk or out, the 2nd arg is optional or not
            if( disk && (args.size()<3 || args[2].empty()) )
                THROW_EZEXCEPT(cmdexception, " no scanname given");

            // save the current host and clear the value.
            // we may write our own value in there (optional 2nd parameter)
            // but most of the times it must be empty. 
            // getsok() uses that value to ::bind() to if it's
            // non-empty. For us that's only important if it's a
            // multicast we want to receive.
            // we'll put the original value back later.
            hosts[&rte] = rte.netparms.host;
            rte.netparms.host.clear();

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
                ASSERT_COND( devInfo.NumDrives>0 );

                // and they're not full or writeprotected
                XLRCALL( ::XLRGetDirectory(ss, &disk_dir) );
                ASSERT_COND( !(disk_dir.Full || disk_dir.WriteProtected) );
            }

            // Start building the chain

            // Read from network
            c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                               &close_filedescriptor);

            // if necessary, decompress
            if( rte.solution )
                c.add(&blockdecompressor, 10, &rte);

            // optionally buffer
            // for net2out we may optionally have to buffer 
            // an amount of bytes. Check if <nbytes> is
            // set and >0
            //  note: (!a && !b) <=> !(a || b)
            if( !(disk || nbyte_str.empty()) || (rtm == net2fork) ) {
                unsigned long b;
                chain::stepid stepid;
                if ( nbyte_str.empty() ) {
                    b = 0;
                }
                else {
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
                    ASSERT2_COND( (b!=ULONG_MAX || errno!=ERANGE) &&
                                  (b!=0         || eocptr!=nbyte_str.c_str()) &&
                                  b>0 && b<UINT_MAX,
                                  SCINFO("Invalid amount of bytes " << nbyte_str << " (1 .. " << UINT_MAX << ")") );
                }

                // We now know that 'b' has a sane value 
                stepid = c.add(&bufferer, 10, buffererargs(&rte, (unsigned int)b));
                servo_stepid[&rte] = stepid;
                    
                // Now install a 'get_buffer_size()' thunk in the rte
                // We store the previous one so's we can put it back
                // when we're done.
                oldthunk[&rte] = rte.set_bufsizegetter(
                                                       makethunk(&bufarg_getbufsize, stepid)
                                                       );
            }

            // and write the result
            c.add(fifowriter, &rte);
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
                scanptr = rte.xlrdev.startScan( arg2 );
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
                    rte.xlrdev.finishScan( scanptr );
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

            // put back original host and bufsizegetter
            rte.netparms.host = hosts[&rte];

            //if( oldthunk.hasData(&rte) ) {
            if( oldthunk.find(&rte)!=oldthunk.end() ) {
                rte.set_bufsizegetter( oldthunk[&rte] );
                oldthunk.erase( &rte );
            }

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
    if ( (args[1] == "skip") && (rtm == net2fork) ) {
        recognized = true;

        string bytes_string ( OPTARG(2, args) );

        if ( bytes_string.empty() ) {
            reply << " 8 : skip requires an extra argument (number of bytes) ;";
            return reply.str();
        }

        char* endptr;
        int64_t n_bytes = strtoll(args[2].c_str(), &endptr, 0);
            
        if ( (*endptr == '\0') && (((n_bytes != std::numeric_limits<int64_t>::max()) && (n_bytes != std::numeric_limits<int64_t>::min())) || (errno!=ERANGE)) ) {

            if ( n_bytes < 0 ) {
                rte.processingchain.communicate( servo_stepid[&rte], &buffererargs::add_bufsize, (unsigned int)-n_bytes );
            }
            else {
                rte.processingchain.communicate( servo_stepid[&rte], &buffererargs::dec_bufsize, (unsigned int)n_bytes );
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

string net2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember previous host setting
    static per_runtime<string> hosts;
    // automatic variables
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==net2file);

    // If we aren't doing anything nor doing net2file - we shouldn't be here!
    if( !atm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            reply << "active : " << 0 /*rte.nbyte_from_mem*/;
        }
        // this displays the flags that are set, in HRF
        //reply << " : " << rte.transfersubmode;
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;
    // net_protocol == udp/tcp
    // open : <filename> [: <strict> ]
    // net_protocol == unix
    // open : <filename> : <unixpath> [ : <strict> ]
    //
    //   <strict>: if given, it must be "1" to be recognized
    //      "1": IF a trackformat is set (via the "mode=" command)
    //           then the (when necessary, decompressed) datastream 
    //           will be run through a filter which ONLY lets through
    //           frames of the datatype indicated by the mode.
    //       default <strict> = 0
    //           (false/not strict/no filtering/blind dump-to-disk)
    //
    if( args[1]=="open" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const string            proto( rte.netparms.get_protocol() );
            const bool              unix( proto=="unix" );
            const string            filename( OPTARG(2, args) );
            const string            strictarg( OPTARG((unsigned int)(unix?4:3), args) ); 
            const string            uxpath( (unix?OPTARG(3, args):"") );
            unsigned int            strict = 0;
                
            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );
            ASSERT2_COND( (!unix || (unix && uxpath.empty()==false)),
                          SCINFO(" no unix socket given") );

            // We could replace this with
            //  strict = (strictarg=="1")
            // but then the user would not know if his/her value of
            // strict was actually used. better to cry out loud
            // if we didn't recognize the value
            if( strictarg.size()>0 ) {
                char*         eocptr;
                unsigned long strictval = 0;
                    
                strictval = ::strtoull(strictarg.c_str(), &eocptr, 0);

                // !(A || B) => !A && !B
                ASSERT2_COND( !(strictval==0 && eocptr==strictarg.c_str()),
                              SCINFO("Failed to parse 'strict' value") );
                ASSERT2_COND(strictval>0 && strictval<3, SCINFO("<strict>, when set, MUST be 1 or 2"));
                strict = (unsigned int)strictval;
            }

            // Conflicting request: at the moment we cannot support
            // strict mode on reading compressed Mk4/VLBA data; bits of
            // the syncword will also be compressed and hence, after 
            // decompression, the syncword will contain '0's rather
            // than all '1's, making the framesearcher useless
            ASSERT2_COND( !strict || (strict && !(rte.solution && (rte.trackformat()==fmt_mark4 || rte.trackformat()==fmt_vlba))),
                          SCINFO("Currently we cannot have strict mode with compressed Mk4/VLBA data") );

            // Now that we have all commandline arguments parsed we may
            // construct our headersearcher
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain
            // clear lasthost so it won't bother the "getsok()" which
            // will, when the net_server is created, use the values in
            // netparms to decide what to do.
            // Also register cancellationfunctions that will close the
            // network and file filedescriptors and notify the threads
            // that it has done so - the threads pick up this signal and
            // terminate in a controlled fashion
            hosts[&rte] = rte.netparms.host;

            if( unix )
                rte.netparms.host = uxpath;
            else
                rte.netparms.host.clear();

            // Add a step to the chain (c.add(..)) and register a
            // cleanup function for that step, in one go
            c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                               &close_filedescriptor);

            // Insert a decompressor if needed
            if( rte.solution )
                c.add(&blockdecompressor, 10, &rte);

            // Insert a framesearcher, if strict mode is requested
            // AND there is a dataformat to look for ...
            if( strict && dataformat.valid() ) {
                c.add(&framer<frame>, 10, framerargs(dataformat, &rte, strict>1));
                // only pass on the binary form of the frame
                c.add(&frame2block, 3);
            }

            // And write into a file
            c.register_cancel( c.add(&fdwriter<block>,  &open_file, filename, &rte),
                               &close_filedescriptor);

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = net2file;
            rte.processingchain = c;
            rte.processingchain.run();

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="close" ) {
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
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

            // If we was doing unix we unlink the 
            // server path
            if( rte.netparms.get_protocol()=="unix" )
                ::unlink( rte.netparms.host.c_str() );

            // put back original host
            rte.netparms.host = hosts[&rte];

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


string file2check_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    ostringstream       reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing net2file - we shouldn't be here!
    if( !(rte.transfermode==no_transfer || rte.transfermode==file2check)  ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            reply << "active : " << 0 ;
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
    // file2check = connect : <filename> [: 0]
    //    "[: 0]" == turn strict mode off (it is on by default)
    //    if the option is given it is checked it is "0"
    if( args[1]=="connect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const string            filename( OPTARG(2, args) );
            const string            strictopt( OPTARG(3, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // If there's no frameformat given we can't do anything
            // usefull
            ASSERT2_COND( dataformat.valid(),
                          SCINFO("please set a mode to indicate which data you expect") );

            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );

            // If the strict thingy is given, ensure it is "0"
            ASSERT_COND(strictopt.empty()==true || (strictopt=="0"));

            // Conflicting request: at the moment we cannot support
            // strict mode on reading compressed Mk4/VLBA data; bits of
            // the syncword will also be compressed and hence, after 
            // decompression, the syncword will contain '0's rather
            // than all '1's, making the framesearcher useless
            ASSERT2_COND( !(rte.solution && (rte.trackformat()==fmt_mark4 || rte.trackformat()==fmt_vlba)),
                          SCINFO("Currently we cannot deal with compressed Mk4/VLBA data") );

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain

            // Add a step to the chain (c.add(..)) and register a
            // cleanup function for that step, in one go
            c.register_cancel( c.add(&fdreader, 32, &open_file, filename, &rte),
                               &close_filedescriptor);

            // Insert a decompressor if needed
            if( rte.solution )
                c.add(&blockdecompressor, 10, &rte);

            // Insert a framesearcher
            c.add(&framer<frame>, 10, framerargs(dataformat, &rte, strictopt.empty() /*true*/));

            // And send to the timedecoder
            c.add(&timeprinter, dataformat);

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = file2check;
            rte.processingchain = c;
            rte.processingchain.run();
                
            rte.processingchain.communicate(0, &fdreaderargs::set_run, true);

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="disconnect" ) {
        recognized = true;
        if( rte.transfermode!=no_transfer ) {
            try {
                // Ok. stop the threads
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

struct file2diskguardargs_type {
    ScanPointer scan;
    runtime*    rteptr;

    file2diskguardargs_type( const ScanPointer& s, runtime& r ) : 
        scan( s ), rteptr(&r)
    {}
    
private:
    file2diskguardargs_type();
};

void* file2diskguard_fun(void* args) {
    // takes ownership of args
    file2diskguardargs_type* guard_args = (file2diskguardargs_type*)args;
    try {
        // wait for the chain to finish processing
        guard_args->rteptr->processingchain.wait();
        // apparently we need to call stop twice
        XLRCALL( ::XLRStop(guard_args->rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(guard_args->rteptr->xlrdev.sshandle()) );
        
        // store the results in the user directory
        guard_args->rteptr->xlrdev.finishScan( guard_args->scan );
        
        if ( guard_args->rteptr->disk_state_mask & runtime::record_flag ) {
            guard_args->rteptr->xlrdev.write_state( "Recorded" );
        }

        RTEEXEC( *guard_args->rteptr, guard_args->rteptr->transfermode = no_transfer; guard_args->rteptr->transfersubmode.clr( run_flag ) );

    }
    catch ( const std::exception& e) {
        DEBUG(-1, "file2disk execution threw an exception: " << e.what() << std::endl );
        guard_args->rteptr->transfermode = no_transfer;
        guard_args->rteptr->xlrdev.stopRecordingFailure();
    }
    catch ( ... ) {
        DEBUG(-1, "file2disk execution threw an unknown exception" << std::endl );        
        guard_args->rteptr->transfermode = no_transfer;
        guard_args->rteptr->xlrdev.stopRecordingFailure();
    }

    
    delete guard_args;
    return NULL;
}

string file2disk_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    static per_runtime<chain::stepid> file_stepid;
    static per_runtime<fdreaderargs>  file_args;
    static per_runtime<string>        file_name;
    static per_runtime<ScanPointer>   scan_pointer;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    reply << "!" << args[0] << ((qry)?('?'):('='));

    if ( qry ) {
        if ( (ctm == file2disk) && (rte.transfersubmode & run_flag) ) {
            ASSERT_COND( file_stepid.find(&rte) != file_stepid.end() && 
                         file_args.find(&rte) != file_args.end() &&
                         file_name.find(&rte) != file_name.end() &&
                         scan_pointer.find(&rte) != scan_pointer.end()
                         );

            reply << " 0 : active : " << file_name[&rte] << " : " << file_args[&rte].start << " : " << (rte.statistics.counter(file_stepid[&rte]) + file_args[&rte].start) << " : " << file_args[&rte].end << " : " << (scan_pointer[&rte].index() + 1) << " : " << ROScanPointer::strip_asterisk( scan_pointer[&rte].name() ) << " ;";
        }
        else {
            reply << " 0 : inactive ;";
        }
    }
    else {
        if ( ctm != no_transfer ) {
            reply << " 6 : doing " << ctm << " cannot start " << args[0] << "; ";
            return reply.str();
        }
        if ( args.size() > 1 ) {
            file_name[&rte] = args[1];
        }
        else {
            if ( file_name.find(&rte) == file_name.end() ) {
                file_name[&rte] = "save.data";
            }
        }

        char* eocptr;
        off_t start = 0;
        if ( args.size() > 2 ) {
            start = ::strtoull(args[2].c_str(), &eocptr, 0);
            ASSERT2_COND( start >= 0 && !(start==0 && eocptr==args[2].c_str()) && !((uint64_t)start==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'start' value") );
        }
        off_t end = 0;
        if ( args.size() > 3 ) {
            end = ::strtoull(args[3].c_str(), &eocptr, 0);
            ASSERT2_COND( end >= 0 && !(end==0 && eocptr==args[3].c_str()) && !((uint64_t)end==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'end' value") );
        }
        
        string scan_label;
        if ( args.size() > 4 ) {
            scan_label = args[4];
        }
        else {
            // default to filename sans suffix
            scan_label = file_name[&rte].substr(file_name[&rte].rfind('.'));
        }

        const headersearch_type dataformat(fmt_none, 0 /*rte.ntrack()*/, 0 /*(unsigned int)rte.trackbitrate()*/, 0 /*rte.vdifframesize()*/);
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

        auto_ptr<fdreaderargs> fdreaderargs_pointer( open_file(file_name[&rte] + ",r", &rte) );
        file_args[&rte] = fdreaderargs(*fdreaderargs_pointer);

        file_args[&rte].start = start;
        file_args[&rte].end = end;
        
        chain c;
        c.register_cancel( file_stepid[&rte] = c.add(&fdreader, 32, file_args[&rte]),
                           &close_filedescriptor);
        c.add(fifowriter, &rte);

        XLRCODE(SSHANDLE ss( rte.xlrdev.sshandle() ));
        XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
        XLRCALL( ::XLRClearChannels(ss) );
        XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

        scan_pointer[&rte] = rte.xlrdev.startScan( scan_label );

        // and start the recording
        XLRCALL( ::XLRAppend(ss) );

        rte.transfersubmode.clr_all();
        // reset statistics counters
        rte.statistics.clear();

        // install and run the chain
        rte.processingchain = c;

        rte.processingchain.run();

        rte.processingchain.communicate(0, &fdreaderargs::set_variable_block_size, true);
        rte.processingchain.communicate(0, &fdreaderargs::set_run, true);
        
        rte.transfermode = file2disk;
        rte.transfersubmode.clr_all().set( run_flag );

        pthread_t thread_id;
        file2diskguardargs_type* guard_args = new file2diskguardargs_type( scan_pointer[&rte], rte );
        pthread_attr_t tattr;
        PTHREAD_CALL( ::pthread_attr_init(&tattr) );
        PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) );
        PTHREAD2_CALL( ::pthread_create( &thread_id, &tattr, file2diskguard_fun, guard_args ),
                       delete guard_args );
        
        reply << " 1 ;";
    }

    return reply.str();
    
}

struct disk2fileguardargs_type {
    runtime*    rteptr;

    disk2fileguardargs_type( runtime& r ) : 
        rteptr(&r)
    {}
    
private:
    disk2fileguardargs_type();
};

void* disk2fileguard_fun(void* args) {
    // takes ownership of args
    disk2fileguardargs_type* guard_args = (disk2fileguardargs_type*)args;
    try {
        // wait for the chain to finish processing
        guard_args->rteptr->processingchain.wait();
        // apparently we need to call stop twice
        XLRCALL( ::XLRStop(guard_args->rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(guard_args->rteptr->xlrdev.sshandle()) );
        
        if ( guard_args->rteptr->disk_state_mask & runtime::play_flag ) {
            guard_args->rteptr->xlrdev.write_state( "Played" );
        }

        RTEEXEC( *guard_args->rteptr, guard_args->rteptr->transfermode = no_transfer; guard_args->rteptr->transfersubmode.clr( run_flag ) );

    }
    catch ( const std::exception& e) {
        DEBUG(-1, "disk2file execution threw an exception: " << e.what() << std::endl );
        guard_args->rteptr->transfermode = no_transfer;
    }
    catch ( ... ) {
        DEBUG(-1, "disk2file execution threw an unknown exception" << std::endl );        
        guard_args->rteptr->transfermode = no_transfer;
    }

    delete guard_args;
    return NULL;
}

string disk2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    static per_runtime<chain::stepid>  disk_stepid;
    static per_runtime<chain::stepid>  file_stepid;
    static per_runtime<diskreaderargs> disk_args;
    static per_runtime<string>         file_name;
    static per_runtime<string>         open_mode;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    reply << "!" << args[0] << ((qry)?('?'):('='));

    if ( qry ) {
        if ( (ctm == disk2file) && (rte.transfersubmode & run_flag) ) {
            ASSERT_COND( disk_stepid.find(&rte) != disk_stepid.end() && 
                         disk_args.find(&rte) != disk_args.end() &&
                         file_name.find(&rte) != file_name.end() &&
                         open_mode.find(&rte) != open_mode.end() &&
                         file_stepid.find(&rte) != file_stepid.end() );

            uint64_t start = disk_args[&rte].pp_start.Addr;
            uint64_t current = rte.statistics.counter(disk_stepid[&rte]) + start;
            uint64_t end = disk_args[&rte].pp_end.Addr;
            reply << " 0 : active : " << file_name[&rte] << " : " << start << " : " << current << " : " << end << " : " << open_mode[&rte];

            // print the bytes to cache if it isn't the default 
            // (to stay consistent with documentation)
            uint64_t bytes_to_cache = rte.processingchain.communicate(file_stepid[&rte], &fdreaderargs::get_bytes_to_cache);
            if ( bytes_to_cache != numeric_limits<uint64_t>::max() ) {
                reply << " : " << bytes_to_cache;
            }

            reply << " ;";
        }
        else {
            reply << " 0 : inactive";
            if ( file_name.find(&rte) != file_name.end() ) {
                reply << " : " << file_name[&rte];
            }
            reply << " ;";
        }
    }
    else {
        if ( ctm != no_transfer ) {
            reply << " 6 : doing " << ctm << " cannot start " << args[0] << "; ";
            return reply.str();
        }

        // if we have fewer than 3 argument we'll need the current scan for default values
        ROScanPointer current_scan = rte.xlrdev.getScan(rte.current_scan);
        if ( args.size() > 1 ) {
            file_name[&rte] = args[1];
        }
        else {
            file_name[&rte] = current_scan.name() + ".m5a";
        }

        char* eocptr;
        off_t start;
        if ( (args.size() > 2) && !args[2].empty() ) {
            start = ::strtoull(args[2].c_str(), &eocptr, 0);
            ASSERT2_COND( start >= 0 && !(start==0 && eocptr==args[2].c_str()) && !((uint64_t)start==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'start' value") );
        }
        else {
            start = rte.pp_current.Addr;
        }
        off_t end;
        if ( (args.size() > 3) && !args[3].empty() ) {
            bool plus = (args[3][0] == '+');
            const char* c_str = args[3].c_str() + ( plus ? 1 : 0 );
            end = ::strtoull(c_str, &eocptr, 0);
            ASSERT2_COND( end >= 0 && !(end==0 && eocptr==c_str) && !((uint64_t)end==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'end' value") );
            if ( plus ) {
                end += start;
            }
        }
        else {
            end = rte.pp_end.Addr;
        }
        
        // sanity checks
        uint64_t length = ::XLRGetLength(rte.xlrdev.sshandle());
        ASSERT2_COND( (start >= 0) && (start < end) && (end <= (int64_t)length), SCINFO("start " << start << " and end " << end << " values are not valid for a recording of length " << length) );

        if ( args.size() > 4 ) {
            if ( !(args[4] == "n" || args[4] == "w" || args[4] == "a") ) {
                reply << " 8 : open mode must be n, w or a ;";
                return reply.str();
            }
            open_mode[&rte] = args[4];
        }
        else {
            open_mode[&rte] = "n";
        }

        uint64_t bytes_to_cache = numeric_limits<uint64_t>::max();
        if ( args.size() > 5 ) {
            bytes_to_cache = ::strtoull(args[5].c_str(), &eocptr, 0);
            ASSERT2_COND( !(bytes_to_cache==0 && eocptr==args[5].c_str()) && !(bytes_to_cache==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'bytes to cache' value") );
        }

        disk_args[&rte] = diskreaderargs( &rte );
        disk_args[&rte].set_start( start );
        disk_args[&rte].set_end( end );
        disk_args[&rte].set_variable_block_size( true );
        disk_args[&rte].set_run( true );

        const headersearch_type dataformat(fmt_none, 0 /*rte.ntrack()*/, 0 /*(unsigned int)rte.trackbitrate()*/, 0 /*rte.vdifframesize()*/);
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

        chain c;
        disk_stepid[&rte] = c.add( diskreader, 10, disk_args[&rte] );
        file_stepid[&rte] = c.add( &fdwriter<block>, &open_file, file_name[&rte] + "," + open_mode[&rte], &rte ); 
        c.register_cancel( file_stepid[&rte], &close_filedescriptor );

        XLRCODE(SSHANDLE ss( rte.xlrdev.sshandle() ));
        XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
        XLRCALL( ::XLRBindOutputChannel(ss, 0) );
        XLRCALL( ::XLRSelectChannel(ss, 0) );

        // reset statistics counters
        rte.statistics.clear();

        // install and run the chain
        rte.processingchain = c;

        rte.processingchain.run();

        rte.processingchain.communicate(file_stepid[&rte], &fdreaderargs::set_bytes_to_cache, bytes_to_cache);

        rte.transfersubmode.clr_all().set( run_flag );
        rte.transfermode = disk2file;

        pthread_t thread_id;
        disk2fileguardargs_type* guard_args = new disk2fileguardargs_type( rte );
        pthread_attr_t tattr;
        PTHREAD_CALL( ::pthread_attr_init(&tattr) );
        PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) );
        PTHREAD2_CALL( ::pthread_create( &thread_id, &tattr, disk2fileguard_fun, guard_args ),
                       delete guard_args );
        
        reply << " 1 ;";
    }

    return reply.str();
    
}

string file2mem_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    ostringstream       reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing net2file - we shouldn't be here!
    if( !(rte.transfermode==no_transfer || rte.transfermode==file2mem)  ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            reply << "active : " << 0 ;
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
    // file2mem = connect : <filename>
    if( args[1]=="connect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const string            filename( OPTARG(2, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());
                
            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain

            // Add a step to the chain (c.add(..)) and register a
            // cleanup function for that step, in one go
            c.register_cancel( c.add(&fdreader, 32, &open_file, filename, &rte),
                               &close_filedescriptor);

            // And send to the bitbucket
            c.add(&bitbucket<block>);

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = file2mem;
            rte.processingchain = c;
            rte.processingchain.run();

            rte.processingchain.communicate(0, &fdreaderargs::set_run, true);
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="disconnect" ) {
        recognized = true;
        if( rte.transfermode!=no_transfer ) {
            try {
                // Ok. stop the threads
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

// Handle disk2file and fill2file
string diskfill2file_fn(bool q, const vector<string>& args, runtime& rte ) {
    static per_runtime<string>  destfilename;

    // automatic variables
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((q)?('?'):('=')) << " ";

    // Accept queries always, commands only when we're 
    // doing the addressed transfer
    atm = (ctm==no_transfer || 
           ((args[0]=="disk2file" && (q || (!q && ctm==disk2file))) ||
            (args[0]=="fill2file" && (q || (!q && ctm==fill2file)))) );

    // If we shouldn't be here, let it be known
    if( !atm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( q ) {
        reply << " 0 : ";
        if( ctm==no_transfer )
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
            bool                    disk( args[0]=="disk2file" );
            chain                   c;
            const string            filename( OPTARG(2, args) );
            const string            proto( rte.netparms.get_protocol() );
                
            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );

            // Now that we have all commandline arguments parsed we may
            // construct our headersearcher
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // add the steps to the chain. depending on the 
            // protocol we add the correct networkwriter
            if( disk ) {
                // prepare disken/streamstor
                XLRCALL( ::XLRSetMode(GETSSHANDLE(rte), SS_MODE_SINGLE_CHANNEL) );
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
                    fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'start' value") );
                }
                if( inc_s.empty()==false ) {
                    fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.inc==0 && eocptr==inc_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'inc' value") );
                }
                if( realtime_s.empty()==false ) {
                    long           num;
                    num = ::strtol(realtime_s.c_str(), &eocptr, 10);
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

string net2check_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember previous host setting
    static per_runtime<string> hosts;
    // automatic variables
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==net2check);

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( !atm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            reply << "active : 0";
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
    // open [: [<start>] : [<inc>] : [<time>] ]
    //    initialize the fillpattern start value with <start>
    //    and increment <inc> [64bit numbers!]
    //
    //    Defaults are:
    //      <start> = 0x1122334411223344
    //      <int>   = 0
    //
    //    <time>   if this is a non-empty string will do/use
    //             timestampchecking, fillpattern ignored
    if( args[1]=="open" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            char*                   eocptr;
            chain                   c;
            const bool              dotime = (OPTARG(4, args).empty()==false);
            fillpatargs             fpargs(&rte);
            const string            start_s( OPTARG(2, args) );
            const string            inc_s( OPTARG(3, args) );
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            if( start_s.empty()==false ) {
                fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                // !(A || B) => !A && !B
                ASSERT2_COND( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                              SCINFO("Failed to parse 'start' value") );
            }
            if( inc_s.empty()==false ) {
                fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                // !(A || B) => !A && !B
                ASSERT2_COND( !(fpargs.inc==0 && eocptr==inc_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                              SCINFO("Failed to parse 'inc' value") );
            }
                
            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain
            // clear lasthost so it won't bother the "getsok()" which
            // will, when the net_server is created, use the values in
            // netparms to decide what to do.
            // Also register cancellationfunctions that will close the
            // network and file filedescriptors and notify the threads
            // that it has done so - the threads pick up this signal and
            // terminate in a controlled fashion
            hosts[&rte] = rte.netparms.host;
            rte.netparms.host.clear();

            // Add a step to the chain (c.add(..)) and register a
            // cleanup function for that step, in one go
            c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                               &close_filedescriptor);

            // Insert a decompressor if needed
            if( rte.solution )
                c.add(&blockdecompressor, 32, &rte);

            // And write to the checker
            if( dotime ) {
                c.add(&framer<frame>, 32, framerargs(dataformat, &rte, false));
                c.add(&timechecker,  dataformat);
            } else
                c.add(&checker, fpargs);

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = net2check;
            rte.processingchain = c;
            rte.processingchain.run();

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="close" ) {
        recognized = true;
        if( rte.transfermode!=no_transfer ) {
            try {
                // Ok. stop the threads
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

            // put back original host
            rte.netparms.host = hosts[&rte];

            reply << " 0 ;";
        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

string net2sfxc_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember previous host setting
    static per_runtime<string> hosts;
    // automatic variables
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==net2sfxc || ctm==net2sfxcfork);

    // If we aren't doing anything nor doing net2sfxc - we shouldn't be here!
    if( !atm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else if ( rte.transfermode==net2sfxc ){
            reply << "active : " << 0 /*rte.nbyte_from_mem*/;
        } else {
            reply << "forking : " << 0 /*rte.nbyte_from_mem*/;
        }
        // this displays the flags that are set, in HRF
        //reply << " : " << rte.transfersubmode;
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;
    // open : <filename> [: <strict> ]
    //   <strict>: if given, it must be "1" to be recognized
    //      "1": IF a trackformat is set (via the "mode=" command)
    //           then the (when necessary, decompressed) datastream 
    //           will be run through a filter which ONLY lets through
    //           frames of the datatype indicated by the mode.
    //   <extrastrict>: if given it must be "0"
    //           this makes the framechecking less strict by
    //           forcing only a match on the syncword. By default it is
    //           on, you can only turn it off. 
    //       
    //       default <strict> = 0
    //           (false/not strict/no filtering/blind dump-to-disk)
    //
    if( args[1]=="open" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            bool                    strict( false );
            chain                   c;
            const string            filename( OPTARG(2, args) );
            const string            strictarg( OPTARG(3, args) ); 
            const string            proto( rte.netparms.get_protocol() );

            // requested transfer mode
            const transfer_type     rtm( string2transfermode(args[0]) );
                
            // these arguments MUST be given
            ASSERT_COND( filename.empty()==false );

            // We could replace this with
            //  strict = (strictarg=="1")
            // but then the user would not know if his/her value of
            // strict was actually used. better to cry out loud
            // if we didn't recognize the value
            if( strictarg.size()>0 ) {
                ASSERT2_COND(strictarg=="1", SCINFO("<strict>, when set, MUST be 1"));
                strict = true;
            }

            // Conflicting request: at the moment we cannot support
            // strict mode on reading compressed Mk4/VLBA data; bits of
            // the syncword will also be compressed and hence, after 
            // decompression, the syncword will contain '0's rather
            // than all '1's, making the framesearcher useless
            ASSERT2_COND( !strict || (strict && !(rte.solution && (rte.trackformat()==fmt_mark4 || rte.trackformat()==fmt_vlba))),
                          SCINFO("Currently we cannot have strict mode with compressed Mk4/VLBA data") );

            // Now that we have all commandline arguments parsed we may
            // construct our headersearcher
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // set read/write and blocksizes based on parameters,
            // dataformats and compression
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

            // Start building the chain
            // clear lasthost so it won't bother the "getsok()" which
            // will, when the net_server is created, use the values in
            // netparms to decide what to do.
            // Also register cancellationfunctions that will close the
            // network and file filedescriptors and notify the threads
            // that it has done so - the threads pick up this signal and
            // terminate in a controlled fashion
            hosts[&rte] = rte.netparms.host;
            rte.netparms.host.clear();

            // Add a step to the chain (c.add(..)) and register a
            // cleanup function for that step, in one go
            c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                               &close_filedescriptor);

            // Insert a decompressor if needed
            if( rte.solution )
                c.add(&blockdecompressor, 10, &rte);

            // Insert a framesearcher, if strict mode is requested
            // AND there is a dataformat to look for ...
            if( strict && dataformat.valid() ) {
                c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                // only pass on the binary form of the frame
                c.add(&frame2block, 3);
            }

            if ( rtm == net2sfxcfork ) {
                c.add(&queue_forker, 10, queue_forker_args(&rte));
            }

            // Insert fake frame generator
            c.add(&faker, 10, fakerargs(&rte));

            // And write into a socket
            c.register_cancel( c.add(&sfxcwriter,  &open_sfxc_socket, filename, &rte),
                               &close_filedescriptor );

            // reset statistics counters
            rte.statistics.clear();
            rte.transfersubmode.clr_all().set( wait_flag );

            rte.transfermode    = rtm;
            rte.processingchain = c;
            rte.processingchain.run();

            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="close" ) {
        recognized = true;
        if( rte.transfermode!=no_transfer ) {
            try {
                // Ok. stop the threads
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfersubmode.clr_all();
            rte.transfermode = no_transfer;

            // put back original host
            rte.netparms.host = hosts[&rte];

            reply << " 0 ;";
        } else {
            reply << " 6 : Not doing " << args[0] << " yet ;";
        }
    }        
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

// obviously, these numbers are chosen completely at random ...
enum hwtype { mark5a = 666, mark5b = 42 };


// Abstract out the phases of an in2{net|fork} command into setup, start,
// pause, resume and stop. If you instantiate an actual "in2net_transfer"
// for a type which is not specialized below (as it will be for "mark5a" and
// "mark5b" (see the enum just above this text)) you will have exceptions
// thrown when trying to access any of them.
template <unsigned int _Blah>
struct in2net_transfer {
    static void setup(runtime&) {
        throw cmdexception("in2net_transfer::setup not defined for this hardware!");
    }
    static void start(runtime&) {
        throw cmdexception("in2net_transfer::start not defined for this hardware!");
    }
    static void pause(runtime&) {
        throw cmdexception("in2net_transfer::pause not defined for this hardware!");
    }
    static void resume(runtime&) {
        throw cmdexception("in2net_transfer::resume not defined for this hardware!");
    }
    static void stop(runtime&) {
        throw cmdexception("in2net_transfer::stop not defined for this hardware!");
    }
};

// Now make specializations which do the Right Thing (tm) for the indicated
// hardware. 


// For the old Mark5A and Mark5A+
template <>
struct in2net_transfer<mark5a> {
    static void setup(runtime& rte) {
        // switch off clock
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2,"setup: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
        notclock = 1;
        DEBUG(2,"setup: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
    }

    // start/resume the recordclock. for Mark5A they are the same
    static void start(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2, "in2net_transfer<mark5a>=on: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
        notclock = 0;
        suspendf  = 0;
        DEBUG(2, "in2net_transfer<mark5a>=on: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
    }
    static void resume(runtime& rte) {
        start(rte);
    }

    static void pause(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2, "in2net_transfer<mark5a>=pause: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
        notclock = 1;
        DEBUG(2, "in2net_transfer<mark5a>=pause: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
    }
    static void stop(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];

        // turn off clock
        DEBUG(2, "in2net_transfer<mark5a>=stop: notclock: " << hex_t(*notclock) << endl);
        notclock = 1;
        DEBUG(2, "in2net_transfer<mark5a>=stop: notclock: " << hex_t(*notclock) << endl);
    }
};

// For Mark5B/DIM and Mark5B+/DIM
template <>
struct in2net_transfer<mark5b> {
    static void setup(runtime&) {
        DEBUG(2, "in2net_transfer<mark5b>=setup" << endl);
    }
    static void start(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=start" << endl);
        start_mk5b_dfhg( rte );
    }
    // start/resume the recordclock
    static void resume(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=resume" << endl);
        // Good. Unpause the DIM. Will restart datatransfer on next 1PPS
        rte.ioboard[ mk5breg::DIM_PAUSE ] = 0;
    }

    static void pause(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=pause" << endl);
        // Good. Pause the DIM. Don't know wether this honours the 1PPS
        // boundary
        rte.ioboard[ mk5breg::DIM_PAUSE ] = 1;
    }
    static void stop(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=stop" << endl);
        rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;
    }
};

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
//  in2file=connect:/path/to/some/file,<openmode> [ : <strict> ]
//       with <openmode>:
//          w   truncate file, create if not exist yet
//          a   append to file, create if not exist yet
//
//       IF a dataformat is set AND compression is requested, THEN a
//       framesearcher is inserted into the streamprocessor unconditionally.
//       The framesearcher only checks for the appearance
//       of the syncword of the expected dataformat 
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
string in2net_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // needed for diskrecording - need to remember across fn calls
    static ScanPointer                scanptr;
    static per_runtime<chain::stepid> fifostep;

    // automatic variables
    bool                atm; // acceptable transfer mode
    const bool          fork( args[0]=="in2fork" || args[0]=="record" );
    const bool          tonet( args[0]=="in2net" );
    const bool          tofile( args[0]=="in2file" );
    const bool          toqueue( args[0]=="record" || args[0]=="in2mem");
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Test if the current transfermode is acceptable for this
    // function: either doing nothing, in2fork or in2net
    // (and depending on 'fork' or not we accept those)
    atm = (ctm==no_transfer ||
           (tonet && ctm==in2net) ||
           (tofile && ctm==in2file) ||
           (fork && !toqueue && ctm==in2fork) ||
           (toqueue && ((fork && ctm==in2memfork) || (!fork && ctm==in2mem))));

    // good, if we shouldn't even be here, get out
    if( !atm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {

        reply << " 0 : ";

        if ( args[0] == "record" ) { // when record has been mapped to in2memfork, we need to simulate the record reply
            if( rte.transfermode==no_transfer ) {
                reply << "off";
            } else {
                // 5 possible status messages: on, halted, throttled, overflow and waiting
                S_DEVSTATUS dev_status;
                XLRCALL( ::XLRGetDeviceStatus(rte.xlrdev.sshandle(), &dev_status) );
                if ( dev_status.Recording ) {
                    if ( Mark5 == mark5a ) {
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
                    else if ( Mark5 == mark5b ) {
                        if ( dev_status.Overflow[0] || rte.ioboard[mk5breg::DIM_OF] ) {
                            reply << "overflow";
                        }
                        else {
                            reply << "on";
                        }
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
            if( rte.transfermode==no_transfer ) {
                reply << "inactive";
            } else {
                reply << rte.netparms.host << (fork?"f":"") << " : " << rte.transfersubmode;
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
    // <connect>
    if( (!toqueue && args[1]=="connect") || 
        (toqueue && args[1] =="on") ) {
        recognized = true;
        // if transfermode is already in2{net|fork}, we ARE already connected
        // (only in2{net|fork}::disconnect clears the mode to doing nothing)
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            string                  filename;
            XLRCODE(SSHANDLE        ss      = rte.xlrdev.sshandle());
            const bool              rtcp    = (rte.netparms.get_protocol()=="rtcp");

            // good. pick up optional hostname/ip to connect to
            // unless it's rtcp
            if( (fork || tonet) && !toqueue ) {
                if( args.size()>2 && !args[2].empty() ) {
                    if( !rtcp )
                        rte.netparms.host = args[2];
                    else
                        DEBUG(0, args[0] << ": WARN! Ignoring supplied host '" << args[2] << "'!" << endl);
                }
            } else if( tofile ) {
                filename = OPTARG(2, args);
                ASSERT2_COND( filename.empty()==false, SCINFO("in2file MUST have a filename as argument"));
            }

            // in2fork requires extra arg: the scanname
            // NOTE: will throw up if none given!
            // Also perform some extra sanity checks needed
            // for disk-recording
            if( fork ) {
                S_DIR         disk;
                S_DEVINFO     devInfo;

                ::memset(&disk, 0, sizeof(S_DIR));
                ::memset(&devInfo, 0, sizeof(S_DEVINFO));

                const unsigned int arg_position = (toqueue ? 2 : 3);
                if(args.size()<=arg_position || args[arg_position].empty())
                    THROW_EZEXCEPT(cmdexception, "No scannanme given for in2fork!");

                // Verify that there are disks on which we *can*
                // record!
                XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                ASSERT_COND( devInfo.NumDrives>0 );

                // and they're not full or writeprotected
                XLRCALL( ::XLRGetDirectory(ss, &disk) );
                ASSERT_COND( !(disk.Full || disk.WriteProtected) );
            } 

            in2net_transfer<Mark5>::setup(rte);

            // now program the streamstor to record from FPDP -> PCI
            XLRCALL( ::XLRSetMode(ss, (CHANNELTYPE)(fork?SS_MODE_FORK:SS_MODE_PASSTHRU)) );
            XLRCALL( ::XLRClearChannels(ss) );
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_PCI) );
            XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );

            // Check. Now program the FPDP channel
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );

            // Code courtesy of Cindy Gold of Conduant Corp.
            //   Have to distinguish between old boards and 
            //   new ones (most notably the Amazon based boards)
            //   (which are used in Mark5B+ and Mark5C)
            XLRCODE(UINT     u32recvMode);
            XLRCODE(UINT     u32recvOpt);

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

            // Start the recording. depending or fork or !fork
            // we have to:
            // * update the scandir on the discpack (if fork'ing)
            // * call a different form of 'start recording'
            //   to make sure that disken are not overwritten
            if( fork ) {
                scanptr = rte.xlrdev.startScan( args[(toqueue ? 2 : 3)] );

                // when fork'ing we do not attempt to record for ever
                // (WRAP_ENABLE==1) otherwise the disken could be overwritten
                XLRCALL( ::XLRAppend(ss) );
            } else {
                // in2net can run indefinitely
                // 18/Mar/2011 - As per communication with Cindy Gold
                //               of Conduant Corp. (the manuf. of the
                //               Mark5-en) MODE_PASSTHRU should imply
                //               WRAP_ENABLE==false. Or rather:
                //               the wording was "wrap-enable was never
                //               meant to apply to nor tested in
                //               passthru mode"
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_DISABLE/*XLR_WRAP_ENABLE*/, 1) );
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
                
            // come up with a theoretical ipd
            compute_theoretical_ipd(rte);
                
            // The hardware has been configured, now start building
            // the processingchain.
            if (toqueue) {
                c.add(&fifo_queue_writer, 1, fifo_queue_writer_args(&rte));
                c.add(&void_step);
                rte.transfersubmode.clr_all().set(run_flag);
                in2net_transfer<Mark5>::start(rte);
            }
            else {
                fifostep[&rte] = c.add(&fiforeader, 10, fiforeaderargs(&rte));

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

                    DEBUG(0, "in2net: enabling compressor " << dataformat << endl);
                    if( dataformat.valid() ) {
                        c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                        c.add(&framecompressor, 10, compressorargs(&rte));
                    } else {
                        c.add(&blockcompressor, 10, &rte);
                    }
                }

                // Write to file or to network
                if( tofile ) {
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
            rte.transfermode    = (fork?(toqueue?in2memfork:in2fork):(tofile?in2file:(toqueue?in2mem:in2net)));

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
    // <on> : turn on dataflow
    if( args[1]=="on" && !toqueue) {
        recognized = true;
        // only allow if transfermode==in2{net|fork} && has the connected flag +
        //   either not started yet (!runflag && !pauseflag) OR
        //   started but paused (runflag && pause)
        if( rte.transfermode!=no_transfer &&
            rte.transfersubmode&connected_flag &&
            ((rte.transfersubmode&run_flag && rte.transfersubmode&pause_flag) ||
             (!(rte.transfersubmode&run_flag) && !(rte.transfersubmode&pause_flag))) ) {

            // If not running yet, start the transfer.
            // Otherwise we were already running and all we
            // need to do is re-enable the inputclock.
            if( !(rte.transfersubmode&run_flag) ) {
                in2net_transfer<Mark5>::start(rte);
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
            // transfermode is either no_transfer, in2net, or in2fork, nothing else
            if( rte.transfermode!=no_transfer )
                if( rte.transfersubmode&run_flag )
                    reply << " 6 : already running ;";
                else
                    reply << " 6 : not yet connected ;";
            else 
                reply << " 6 : not doing anything ;";
        }
    }
    if( args[1]=="off" && !toqueue) {
        recognized = true;
        // only allow if transfermode=={in2net|in2fork} && submode has the run flag
        if( rte.transfermode!=no_transfer &&
            (rte.transfersubmode&run_flag)==true &&
            (rte.transfersubmode&pause_flag)==false ) {

            // Pause the recording
            in2net_transfer<Mark5>::pause(rte);

            // indicate paused state
            rte.transfersubmode.set( pause_flag );
            reply << " 0 ;";
        } else {
            // transfermode is either no_transfer or {in2net|in2fork}, nothing else
            if( rte.transfermode!=no_transfer )
                reply << " 6 : already running ;";
            else 
                reply << " 6 : not doing anything ;";
        }
    }
    // <disconnect>
    if( (!toqueue && args[1]=="disconnect" ) ||
        (toqueue && args[1]=="off") ) {
        recognized = true;
        // Only allow if we're doing in2net.
        // Don't care if we were running or not
        if( rte.transfermode!=no_transfer ) {
            string error_message;
            try {
                if ( (rte.transfermode == in2memfork) && (Mark5 == mark5b)) {
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
                error_message += string(" : Failed to stop I/O board: ") + e.what();
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop I/O board, unknown exception");
            }

            try {
                // do a blunt stop. at the sending end we do not care that
                // much processing every last bit still in our buffers
                rte.processingchain.stop();
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop processing chain: ") + e.what();
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop processing chain, unknown exception");
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
                if( fork ) {
                    rte.xlrdev.finishScan( scanptr );
                    rte.pp_current = scanptr.start();
                    rte.pp_end = scanptr.start() + scanptr.length();
                    rte.current_scan = rte.xlrdev.nScans() - 1;
                }

                if ( fork && (rte.disk_state_mask & runtime::record_flag) ) {
                    rte.xlrdev.write_state( "Recorded" );
                }
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop streamstor: ") + e.what();
                if ( fork ) {
                    rte.xlrdev.stopRecordingFailure();
                }
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop streamstor, unknown exception");
                if ( fork ) {
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

// spill = split-fill [generate fillpattern and split/dechannelize it]
// spid  = split-disk [read data from StreamStor and split/dechannelize it]
// spif  = split-file [read data from file and split/dechannelize it]
struct splitsettings_type {
    bool             strict;
    uint16_t         station;
    unsigned int     vdifsize;
    unsigned int     bitsperchannel;
    unsigned int     bitspersample;
    unsigned int     qdepth;
    netparms_type    netparms;
    chain::stepid    framerstep;
    tagremapper_type tagremapper;

    splitsettings_type():
        strict( false ), station( 0 ),
        vdifsize( (unsigned int)-1 ),
        bitsperchannel(0), bitspersample(0), qdepth( 32 )
    {}
};

// Replaces the sequence "{tag}" with the representation of 
// the number. If no "{tag}" found returns the string unmodified
template <typename T>
string replace_tag(const string& in, const T& tag) {
    string::size_type  tagptr = in.find( "{tag}" );

    // no tag? 
    if( tagptr==string::npos )
        return in;

    // make a modifieable copy
    string        lcl(in);
    ostringstream repr;
    repr << tag;
    return lcl.replace(tagptr, 5, repr.str());
}

template <unsigned int Mark5>
string spill2net_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // Keep some static info and the transfers that this function services
    static const transfer_type             transfers[] = {spill2net, spid2net, spin2net, spin2file, spif2net,
                                                          spill2file, spid2file, spif2file, splet2net, splet2file};
    static per_runtime<chain::stepid>      fifostep;
    static per_runtime<splitsettings_type> settings;
    // for split-fill pattern we can (attempt to) do realtime or
    // 'as-fast-as-you-can'. Default = as fast as the system will go
    static per_runtime<bool>               realtime;

    // atm == acceptable transfer mode
    // rtm == requested transfer mode
    // ctm == current transfer mode
    ostringstream       reply;
    const transfer_type rtm( string2transfermode(args[0]) );
    const transfer_type ctm( rte.transfermode );
    const bool          atm = find_xfer(rtm, transfers);

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Check if we should be here at all
    if( ctm!=no_transfer && rtm!=ctm ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }
    if( !atm ) {
        reply << " 2 : " << args[0] << " is not supported by this implementation ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        const string& what( OPTARG(1, args) );
        reply << " 0 : ";
        if( what=="station" ) {
            uint16_t  sid = settings[&rte].station;
            reply << sid;
            if( sid && (sid&0x8000)==0 )
               reply << " : " << (char)(sid&0xff) << (char)((sid>>8));
        } else if( what=="strict" ) {
            reply << settings[&rte].strict;
        } else if( what=="net_protocol" ) {
            const netparms_type& np = settings[&rte].netparms;

            reply << np.get_protocol() << " : " ;
            if( np.rcvbufsize==np.sndbufsize )
                reply << np.rcvbufsize;
            else
                reply << "Rx " << np.rcvbufsize << ", Tx " << np.sndbufsize;
            reply << " : " << np.get_blocksize()
                  << " : " << np.nblock;
        } else if( what=="mtu" ) {
            reply << settings[&rte].netparms.get_mtu();
        } else if( what=="ipd" ) {
            reply << settings[&rte].netparms.interpacketdelay;
        } else if( what=="vdifsize" ) {
            reply << settings[&rte].vdifsize;
        } else if( what=="bitsperchannel" ) {
            reply << settings[&rte].bitsperchannel;
        } else if( what=="bitspersample" ) {
            reply << settings[&rte].bitspersample;
        } else if( what=="qdepth" ) {
            reply << settings[&rte].qdepth;
        } else if( what=="tagmap" ) {
            tagremapper_type::const_iterator p; 
            tagremapper_type::const_iterator start = settings[&rte].tagremapper.begin();
            tagremapper_type::const_iterator end   = settings[&rte].tagremapper.end();

            for(p=start; p!=end; p++) {
                if( p!=start )
                    reply << " : ";
                reply << p->first << "=" << p->second;
            }
        } else if( what=="realtime" && args[0].find("spill2")!=string::npos ) {
            bool rt = false;
            if( realtime.find(&rte)!=realtime.end() )
                rt = realtime[&rte];
            reply << (rt?"1":"0");
        } else if( what.empty()==false ) {
            reply << " : unknown query parameter '" << what << "'";
        } else {
            if( ctm==no_transfer ) {
                reply << "inactive";
            } else {
                reply << "active";
            }
        }
        reply << " ;";
        return reply.str();
    }

    if( args.size()<2 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;
    // <connect>
    //
    // [spill|spid|spin|splet]2[net|file] = connect : <splitmethod> : <tagN>=<destN> : <tagM>=<destM> ....
    // spif2[net|file]         = connect : <file> : <splitmethod> : <tagN> = <destN> : ...
    //    splitmethod = which splitter to use. check splitstuff.cc for
    //                  defined splitters [may be left blank - no
    //                  splitting is done but reframing to VDIF IS done
    //                  ;-)]
    //    file  = [spif2* only] - file to read data-to-split from
    //    destN = <host|ip>[@<port>]    (for *2net)
    //             default port is 2630
    //            <filename>,[wa]  (for *2file)
    //              w = (over)write; empty file before writing
    //              a = append-to-file 
    //              no default mode
    //    tagN  = <tag> | <tag>-<tag> [, tagN ]
    //             note: tag range "<tag>-<tag>" endpoint is *inclusive*
    //    tag   = unsigned int
    if( args[1]=="connect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                    c;
            string                   curcdm;
            const string             splitmethod( OPTARG((fromfile(rtm)?3:2), args) );
            const string             filename( OPTARG(2, args) );
            const unsigned int       qdepth = settings[&rte].qdepth;
            chunkdestmap_type        cdm;

#if 0
            ASSERT2_COND(splitmethod.empty()==false, SCINFO("You must specify how to split the data"));
#endif


            // If we need to send over UDP we reframe to MTU size,
            // otherwise we can send out the split frames basically
            // unmodified.
            // The output chunk size is determined by the MTU of
            // the output networksettings *if* the transfer is TO the network 
            // (*2net) and the transfer is over UDP. Otherwise the VDIF 
            // framesize is unconstrained (*2file and *2net over TCP).
            // Take netparms from global parameters if we're NOT doing
            // splet2net - in that case take the network parameters
            // from the settings[&rte].netparms
            const netparms_type&        dstnet( rtm==splet2net ? settings[&rte].netparms : rte.netparms );
            const headersearch_type     dataformat(rte.trackformat(), rte.ntrack(),
                                                   (unsigned int)rte.trackbitrate(),
                                                   rte.vdifframesize());
            const unsigned int ochunksz = ( (tonet(rtm) && dstnet.get_protocol().find("udp")!=string::npos) ?
                                            dstnet.get_max_payload() :
                                            settings[&rte].vdifsize /*-1*/ );

            DEBUG(3, args[0] << ": current data format = " << endl << "  " << dataformat << endl);

            // The chunk-dest-mapping entries only start at positional
            // argument 3:
            // 0 = 'spill2net', 1='connect', 2=<splitmethod>, 3+ = <tag>=<dest>
            // 0 = 'spif2net', 1='connect', 2=<file>, 3=<splitmethod>, 4+ = <tag>=<dest>
            for(size_t i=(fromfile(rtm)?4:3); (curcdm=OPTARG(i, args)).empty()==false; i++) {
                vector<string>       parts = ::split(curcdm, '=');
                vector<unsigned int> tags;

                EZASSERT2( parts.size()==2 && parts[0].empty()==false && parts[1].empty()==false,
                           cmdexception,
                           EZINFO(" chunk-dest-mapping #" << (i-3) << " invalid \"" << curcdm << "\"") );

                // Parse intrange
                tags = ::parseUIntRange(parts[0]);
                for(vector<unsigned int>::const_iterator curtag=tags.begin();
                    curtag!=tags.end(); curtag++) {
                    EZASSERT2( cdm.insert(make_pair(*curtag, replace_tag(parts[1], *curtag))).second,
                               cmdexception,
                               EZINFO(" possible double tag " << *curtag
                                      << " - failed to insert into map destination " << parts[1]) );
                }
            }

            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
            DEBUG(2, args[0] << ": constrained sizes = " << rte.sizes << endl);

            // Look at requested transfermode
            // to see where the heck we should get the
            // data from.
            // Don't have to have a final 'else' clause since
            // IF we do not handle the requested transfer mode
            // the chain has no 'producer' and hence the
            // addition of the next step will trigger an exception ...
            if( fromfill(rtm) ) {
                fillpatargs  fpargs(&rte);
                if( realtime.find(&rte)!=realtime.end() )
                    fpargs.realtime = realtime[&rte];
                c.add( &framepatterngenerator, qdepth, fpargs );
            } else if( fromdisk(rtm) )
                c.add( &diskreader, qdepth, diskreaderargs(&rte) );
            else if( fromio(rtm) ) {
                // set up the i/o board and streamstor 
                XLRCODE(SSHANDLE   ss = rte.xlrdev.sshandle());

                in2net_transfer<Mark5>::setup(rte);
                // now program the streamstor to record from FPDP -> PCI
                XLRCALL( ::XLRSetMode(ss, (CHANNELTYPE)SS_MODE_PASSTHRU) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_PCI) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );
                // Check. Now program the FPDP channel
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );

                // Code courtesy of Cindy Gold of Conduant Corp.
                //   Have to distinguish between old boards and 
                //   new ones (most notably the Amazon based boards)
                //   (which are used in Mark5B+ and Mark5C)
                XLRCODE(UINT     u32recvMode);
                XLRCODE(UINT     u32recvOpt);

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
                // and start the recording
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_DISABLE, 1) );
                fifostep[&rte] = c.add( &fiforeader, qdepth, fiforeaderargs(&rte) );
            } else if( fromnet(rtm) ) 
                // net2* transfers always use the global network params
                // as input configuration. For net2net style use
                // splet2net = net_protocol : <proto> : <bufsize> &cet
                // to configure output network settings
                c.register_cancel( c.add( &netreader, qdepth, &net_server, networkargs(&rte) ),
                                   &close_filedescriptor);
            else if( fromfile(rtm) ) {
                EZASSERT( filename.empty() == false, cmdexception );
                c.add( &fdreader, qdepth, &open_file, filename, &rte );
            }

            // The rest of the processing chain is media independent
            settings[&rte].framerstep = c.add( &framer<tagged<frame> >, qdepth,
                                               framerargs(dataformat, &rte, settings[&rte].strict) );

            headersearch_type*             curhdr = new headersearch_type( rte.trackformat(),
                                                                           rte.ntrack(),
                                                                           (unsigned int)rte.trackbitrate(),
                                                                           rte.vdifframesize() );

            if( splitmethod.empty()==false ) {
                // Figure out which splitters we need to do
                vector<string>                 splitters = split(splitmethod,'+');

                // the rest accept tagged frames as input and produce
                // tagged frames as output
                for(vector<string>::const_iterator cursplit=splitters.begin();
                    cursplit!=splitters.end(); cursplit++) {
                    unsigned int         n2c = -1;
                    vector<string>       splittersetup = split(*cursplit,'*');
                    headersearch_type*   newhdr = 0;
                    splitproperties_type splitprops;

                    EZASSERT2( cursplit->empty()==false, cmdexception, EZINFO("empty splitter not allowed!") );
                    EZASSERT2( splittersetup.size()==1 || (splittersetup.size()==2 && splittersetup[1].empty()==false),
                               cmdexception,
                               EZINFO("Invalid splitter '" << *cursplit << "' - use <splitter>[*<int>]") );

                    // If the splittersetup looks like a dynamic channel extractor
                    // (ie "[..] [...]") and the user did not provide an input step size
                    // we'll insert it
                    if( splittersetup[0].find('[')!=string::npos && splittersetup[0].find('>')==string::npos ) {
                        ostringstream  pfx;
                        pfx << curhdr->ntrack << " > ";
                        splittersetup[0] = pfx.str() + splittersetup[0];
                    }

                    // Look up the splitter
                    EZASSERT2( (splitprops = find_splitfunction(splittersetup[0])).fnptr(),
                               cmdexception,
                               EZINFO("the splitfunction '" << splittersetup[0] << "' cannot be found") );

                    if( splittersetup.size()==2 ) {
                        char*         eocptr;
                        unsigned long ul;
                        ul = ::strtoul(splittersetup[1].c_str(), &eocptr, 0);
                        EZASSERT2( eocptr!=splittersetup[1].c_str() && *eocptr=='\0' && ul>0 && ul<=UINT_MAX,
                                   cmdexception,
                                   EZINFO("'" << splittersetup[1] << "' is not a numbah or it's too frikkin' large (or zero)!") );
                        n2c = (unsigned int)ul;
                    }
                    splitterargs  splitargs(&rte, splitprops, *curhdr, n2c);
                    newhdr = new headersearch_type( splitargs.outputhdr );
                    delete curhdr;
                    curhdr = newhdr;
                    c.add( &coalescing_splitter, qdepth, splitargs );
                }
            } else {
                // no splitter given, then we must strip the header
                c.add( &header_stripper, qdepth, *((const headersearch_type*)curhdr) );
            }

            // Whatever came out of the splitter we reframe it to VDIF
            // By now we know what kind of output the splitterchain is
            // producing so we can tell the reframer that
            reframe_args       ra(settings[&rte].station, curhdr->trackbitrate,
                                  curhdr->payloadsize, ochunksz, settings[&rte].bitsperchannel,
                                  settings[&rte].bitspersample);

            delete curhdr;

            // install the current tagremapper
            ra.tagremapper = settings[&rte].tagremapper;

            c.add( &reframe_to_vdif, qdepth, ra);

            // Based on where the output should go, add a final stage to
            // the processing
            if( tofile(rtm) )
                c.register_cancel( c.add( &multiwriter<miniblocklist_type, fdwriterfunctor>,
                                          &multifileopener,
                                          multidestparms(&rte, cdm)), 
                                   &multicloser );
            else if( tonet(rtm) ) {
                // if we need to write to upds we silently call upon the vtpwriter in
                // stead of the networkwriter
                if( dstnet.get_protocol().find("udps")!=string::npos ) {
                    c.register_cancel( c.add( &multiwriter<miniblocklist_type, vtpwriterfunctor>,
                                              &multiopener,
                                              multidestparms(&rte, cdm, dstnet) ),
                                       &multicloser );
                } else {
                    c.register_cancel( c.add( &multiwriter<miniblocklist_type, netwriterfunctor>,
                                              &multiopener,
                                              multidestparms(&rte, cdm, dstnet) ),
                                       &multicloser );
                }
            }

            // reset statistics counters
            rte.statistics.clear();

            // Now we can start the chain
            rte.processingchain = c;
            DEBUG(2, args[0] << ": starting to run" << endl);
            rte.processingchain.run();

            if ( fromfile(rtm) ) {
                rte.processingchain.communicate(0, &fdreaderargs::set_run, true);
            }

            DEBUG(2, args[0] << ": running" << endl);
            rte.transfermode    = rtm;
            rte.transfersubmode.clr_all().set(connected_flag).set(wait_flag);
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="station" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            uint16_t     sid = 0;
            const string station_id( OPTARG(2, args) );
            for(unsigned int i=0; i<2 && i<station_id.size(); i++)
                sid = (uint16_t)(sid | (((uint16_t)station_id[i])<<(i*8)));
            settings[&rte].station = sid;
            reply << " 0 ;";
        } else {
            reply << " 6 : cannot change during transfer ;";
        }
    } else if( args[1]=="strict" ) {
        const string strictarg( OPTARG(2, args) );

        recognized = true;
        EZASSERT2( strictarg=="1" || strictarg=="0",
                   cmdexception,
                   EZINFO("use '1' to turn strict mode on, '0' for off") );
        // Save the new value in the per runtime settings
        settings[&rte].strict = (strictarg=="1");

        // IF there is a transfer running, we communicate it also
        // to the framer
        if( rte.transfermode!=no_transfer )
            rte.processingchain.communicate( settings[&rte].framerstep,
                                             &framerargs::set_strict,
                                             settings[&rte].strict );

        reply << " 0 ;";
    } else if( args[1]=="net_protocol" ) {
        string         proto( OPTARG(2, args) );
        const string   sokbufsz( OPTARG(3, args) );
        netparms_type& np = settings[&rte].netparms;

        recognized = true;

        if( proto.empty()==false )
            np.set_protocol(proto);

        if( sokbufsz.empty()==false ) {
            char*          eptr;
            long int       bufsz = ::strtol(sokbufsz.c_str(), &eptr, 0);

            // was a unit given? [note: all whitespace has already been stripped
            // by the main commandloop]
            EZASSERT2( eptr!=sokbufsz.c_str() && ::strchr("kM\0", *eptr),
                       cmdexception,
                       EZINFO("invalid socketbuffer size '" << sokbufsz << "'") );

            // Now we can do this
            bufsz *= ((*eptr=='k')?KB:(*eptr=='M'?MB:1));

            // Check if it's a sensible "int" value for size, ie >=0 and <=INT_MAX
            EZASSERT2( bufsz>=0 && bufsz<=INT_MAX,
                       cmdexception,
                       EZINFO("<socbuf size> '" << sokbufsz << "' out of range") );
            np.rcvbufsize = np.sndbufsize = (int)bufsz;
        }
        reply << " 0 ;";
    } else if( args[1]=="mtu" ) {
        char*             eocptr;
        const string      mtustr( OPTARG(2, args) );
        netparms_type&    np = settings[&rte].netparms;
        unsigned long int mtu;

        recognized = true;
        EZASSERT2(mtustr.empty()==false, cmdexception, EZINFO("mtu needs a parameter"));

        mtu = ::strtoul(mtustr.c_str(), &eocptr, 0);

        // Check if it's a sensible "int" value for size, ie >0 and <=INT_MAX
        EZASSERT2(eocptr!=mtustr.c_str() && *eocptr=='\0' && errno!=ERANGE &&  mtu>0 && mtu<=UINT_MAX,
                  cmdexception,
                  EZINFO("mtu '" << mtustr << "' out of range") );
        np.set_mtu( (unsigned int)mtu );
        reply << " 0 ;";
    } else if( args[1]=="ipd" ) {
        char*          eocptr;
        long int       ipd;
        const string   ipdstr( OPTARG(2, args) );
        netparms_type& np = settings[&rte].netparms;

        recognized = true;
        EZASSERT2(ipdstr.empty()==false, cmdexception, EZINFO("ipd needs a parameter"));

        ipd = ::strtol(ipdstr.c_str(), &eocptr, 0);

        // Check if it's an acceptable "ipd" value 
        EZASSERT2(eocptr!=ipdstr.c_str() && *eocptr=='\0' && errno!=ERANGE && ipd>=-1 && ipd<=INT_MAX,
                cmdexception,
                EZINFO("ipd '" << ipdstr << "' NaN/out of range (range: [-1," << INT_MAX << "])") );
        np.interpacketdelay = (int)ipd;
        reply << " 0 ;";
    } else if( args[1]=="vdifsize" ) {
        char*             eocptr;
        const string      vdifsizestr( OPTARG(2, args) );
        unsigned long int vdifsize;

        recognized = true;
        EZASSERT2(vdifsizestr.empty()==false, cmdexception, EZINFO("vdifsize needs a parameter"));

        vdifsize = ::strtoul(vdifsizestr.c_str(), &eocptr, 0);
        EZASSERT2(eocptr!=vdifsizestr.c_str() && *eocptr=='\0' && errno!=ERANGE && vdifsize<=UINT_MAX,
                cmdexception,
                EZINFO("vdifsize '" << vdifsizestr << "' NaN/out of range (range: [1," << UINT_MAX << "])") );
        settings[&rte].vdifsize = (unsigned int)vdifsize;
        reply << " 0 ;";
    } else if( args[1]=="bitsperchannel" ) {
        char*             eocptr;
        const string      bpcstr( OPTARG(2, args) );
        unsigned long int bpc;

        recognized = true;
        EZASSERT2(bpcstr.empty()==false, cmdexception, EZINFO("bitsperchannel needs a parameter"));

        bpc = ::strtoul(bpcstr.c_str(), &eocptr, 0);
        EZASSERT2(eocptr!=bpcstr.c_str() && *eocptr=='\0' && bpc>0 && bpc<=64, cmdexception,
                EZINFO("bits per channel must be >0 and less than 65"));
        settings[&rte].bitsperchannel = (unsigned int)bpc;
        reply << " 0 ;";
    } else if( args[1]=="bitspersample" ) {
        char*             eocptr;
        const string      bpsstr( OPTARG(2, args) );
        unsigned long int bps;

        recognized = true;
        EZASSERT2(bpsstr.empty()==false, cmdexception, EZINFO("bitspersample needs a parameter"));

        bps = ::strtoul(bpsstr.c_str(), &eocptr, 0);

        EZASSERT2(eocptr!=bpsstr.c_str() && *eocptr=='\0' && bps>0 && bps<=32, cmdexception,
                EZINFO("bits per sample must be >0 and less than 33"));
        settings[&rte].bitspersample = (unsigned int)bps;
        reply << " 0 ;";
    } else if( args[1]=="qdepth" ) {
        char*             eocptr;
        const string      qdstr( OPTARG(2, args) );
        unsigned long int qd;

        recognized = true;
        EZASSERT2(qdstr.empty()==false, cmdexception, EZINFO("qdepth needs a parameter"));

        qd = ::strtoul(qdstr.c_str(), &eocptr, 0);

        // Check if it's an acceptable qdepth
        EZASSERT2( eocptr!=qdstr.c_str() && *eocptr=='\0' && errno!=ERANGE && qd>0 && qd<=UINT_MAX,
                cmdexception,
                EZINFO("qdepth '" << qdstr << "' NaN/out of range (range: [1," << UINT_MAX << "])") );
        settings[&rte].qdepth = qd;
        reply << " 0 ;";
    } else if( args[1]=="realtime" && args[0].find("spill2")!=string::npos ) {
        char*        eocptr;
        long int     rt;
        const string rtstr( OPTARG(2, args) );

        recognized = true;
        EZASSERT2(rtstr.empty()==false, cmdexception, EZINFO("realtime needs a parameter"));

        rt = ::strtol(rtstr.c_str(), &eocptr, 10);

        // Check if it's an acceptable number
        EZASSERT2( eocptr!=rtstr.c_str() && *eocptr=='\0' && errno!=ERANGE,
                cmdexception,
                EZINFO("realtime parameter must be a decimal number") );
        realtime[&rte] = (rt!=0);
        reply << " 0 ;";
    } else if( args[1]=="tagmap" ) {

        recognized = true;
        if( rte.transfermode==no_transfer ) {
            tagremapper_type  newmap;
            string            curentry;

            // parse the tag->datathread mappings
            for(size_t i=2; (curentry=OPTARG(i, args)).empty()==false; i++) {
                unsigned int         tag, datathread;
                vector<string>       parts = ::split(curentry, '=');

                EZASSERT2( parts.size()==2 && parts[0].empty()==false && parts[1].empty()==false,
                           cmdexception,
                           EZINFO(" tag-to-threadid #" << (i-2) << " invalid \"" << curentry << "\"") );

                // Parse numbers
                tag        = (unsigned int)::strtoul(parts[0].c_str(), 0, 0);
                datathread = (unsigned int)::strtoul(parts[1].c_str(), 0, 0);

                EZASSERT2( newmap.insert(make_pair(tag, datathread)).second,
                           cmdexception,
                           EZINFO(" possible double tag " << tag
                                  << " - failed to insert into map datathread " << parts[1]) );
            }
            settings[&rte].tagremapper = newmap;
            reply << " 0 ;";
        } else {
            reply << " 6 : Cannot change during transfers ;";
        }
    } else if( args[1]=="on" ) {
        recognized = true;
        // First: check if we're doing spill2[net|file]
        if( rte.transfermode==spill2net || rte.transfermode==spill2file ) {
            if( ((rte.transfersubmode&run_flag)==false) ) {
                uint64_t      nword = 100000;
                const string  nwstr( OPTARG(2, args) );
                const string  start_s( OPTARG(3, args) );
                const string  inc_s( OPTARG(4, args) );

                if( !nwstr.empty() ) {
                    ASSERT2_COND( ::sscanf(nwstr.c_str(), "%" SCNu64, &nword)==1,
                                  SCINFO("value for nwords is out of range") );
                }

                if( start_s.empty()==false ) {
                    char*     eocptr;
                    uint64_t  fill;

                    fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fill==0 && eocptr==start_s.c_str()) && !(fill==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'start' value") );

                    rte.processingchain.communicate(0, &fillpatargs::set_fill, fill);
                }
                if( inc_s.empty()==false ) {
                    char*     eocptr;
                    uint64_t  inc;

                    inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(inc==0 && eocptr==inc_s.c_str()) && !(inc==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'inc' value") );
                    rte.processingchain.communicate(0, &fillpatargs::set_inc, inc);
                }

                // turn on the dataflow
                rte.processingchain.communicate(0, &fillpatargs::set_nword, nword);
                rte.processingchain.communicate(0, &fillpatargs::set_run,   true);
                recognized = true;
                rte.transfersubmode.clr(wait_flag).set(run_flag);
                reply << " 0 ;";
            } else {
                reply << " 6 : already running ;";
            }
            // Maybe we're doing spid (disk) to [net|file]?
        } else if( rte.transfermode==spid2net || rte.transfermode==spid2net ) {
            if( ((rte.transfersubmode&run_flag)==false) ) {
                bool               repeat = false;
                uint64_t           nbyte;
                playpointer        pp_s;
                playpointer        pp_e;
                const string       startbyte_s( OPTARG(2, args) );
                const string       endbyte_s( OPTARG(3, args) );
                const string       repeat_s( OPTARG(4, args) );

                // Pick up optional extra arguments:
                // note: we do not support "scan_set" yet so
                //       the part in the doc where it sais
                //       that, when omitted, they refer to
                //       current scan start/end.. that no werk

                // start-byte #
                if( !startbyte_s.empty() ) {
                    uint64_t v;

                    ASSERT2_COND( ::sscanf(startbyte_s.c_str(), "%" SCNu64, &v)==1,
                                  SCINFO("start-byte# is out-of-range") );
                    pp_s.Addr = v;
                }
                // end-byte #
                // if prefixed by "+" this means: "end = start + <this value>"
                // rather than "end = <this value>"
                if( !endbyte_s.empty() ) {
                    uint64_t v;

                    ASSERT2_COND( ::sscanf(endbyte_s.c_str(), "%" SCNu64, &v)==1,
                                  SCINFO("end-byte# is out-of-range") );
                    if( endbyte_s[0]=='+' )
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
            } else {
                reply << " 6 : already running ;";
            }
        } else if( rte.transfermode==spin2net || rte.transfermode==spin2file ) {
            // only allow if we're in a connected state
            if( rte.transfermode&connected_flag ) {
                // initial state = connected, wait
                // other acceptable states are: running, pause
                // or: running
                if( rte.transfersubmode&wait_flag ) {
                    // first time here - kick the fiforeader into action
                    in2net_transfer<Mark5>::start(rte);
                    rte.processingchain.communicate(fifostep[&rte], &fiforeaderargs::set_run, true);
                    // change from WAIT->RUN,PAUSE (so below we can go
                    // from "RUN, PAUSE" -> "RUN"
                    rte.transfersubmode.clr( wait_flag ).set( run_flag ).set( pause_flag );
                }

                // ok, deal with pause / unpause
                if( rte.transfersubmode&run_flag && rte.transfersubmode&pause_flag ) {
                    // resume the hardware
                    in2net_transfer<Mark5>::resume(rte);
                    rte.transfersubmode.clr( pause_flag );
                    reply << " 0 ;";
                } else {
                    reply << " 6 : already on or not running;";
                }
            } else {
                reply << " 6 : not connected anymore ;";
            }
        } else {
            // "=on" doesn't apply yet!
            reply << " 6 : not doing " << args[0] << " ;";
        }
    } else if( args[1]=="off" ) {
        // Only valid for spin2[net|file]; pause the hardware
        if( rte.transfermode==spin2net || rte.transfermode==spin2file ) {
            recognized = true;
            // only acceptable if we're actually running and not yet
            // paused
            if( rte.transfersubmode&run_flag && !(rte.transfersubmode&pause_flag)) {
                in2net_transfer<Mark5>::pause(rte);
                rte.transfersubmode.set( pause_flag );
            } else {
                reply << " 6 : not running or already paused ;";
            }
        } else if( rte.transfermode==no_transfer ) {
            recognized = true;
            reply << " 6 : not doing " << args[0] << " ;";
        }
    } else if( args[1]=="disconnect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            reply << " 6 : not doing " << args[0] << " ;";
        } else {
            string error_message;
            DEBUG(2, "Stopping " << rte.transfermode << "..." << endl);

            if( rte.transfermode==spin2net || rte.transfermode==spin2file ) {
                try {
                    // tell hardware to stop sending
                    in2net_transfer<Mark5>::stop(rte);
                }
                catch ( std::exception& e ) {
                    error_message += string(" : Failed to stop I/O board: ") + e.what();
                }
                catch ( ... ) {
                    error_message += string(" : Failed to stop I/O board, unknown exception");
                }
                
                try {
                    // And stop the recording on the Streamstor. Must be
                    // done twice if we are running, according to the
                    // manual. I think.
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
            }

            try {
                rte.processingchain.stop();
                DEBUG(2, rte.transfermode << " disconnected" << endl);
                rte.processingchain = chain();
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();

            if ( error_message.empty() ) {
                reply << " 0 ;";
            }
            else {
                reply << " 4" << error_message << " ;";
            }
            
            recognized = true;
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

struct dupvars {
    bool           expand;
    unsigned int   factor;
    unsigned int   sz;

    dupvars() :
        expand(false), factor(0), sz(0)
    {}
};

string net2mem_fn(bool qry, const vector<string>& args, runtime& rte ) {
    static per_runtime<string>   host;
    static per_runtime<dupvars>  arecibo;
    
    const transfer_type rtm( string2transfermode(args[0]) );
    const transfer_type ctm( rte.transfermode );

    ostringstream       reply;
    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if ( qry ) {
        const string areciboarg = OPTARG(1, args);
        if( ::strcasecmp(areciboarg.c_str(), "ar")==0 ) {
            const dupvars& dv = arecibo[&rte];
            reply << " 0 : " << (dv.expand?"true":"false") << " : "
                  << dv.sz << "bit" << " : x" << dv.factor << " ;";
            return reply.str();
        }
        reply << "0 : " << (ctm == rtm ? "active" : "inactive") << " ;";
        return reply.str();
    }

    // handle command
    if ( args.size() < 2 ) {
        reply << "8 : " << args[0] << " requires a command argument ;";
        return reply.str();
    }

    if ( args[1] == "open" ) {
        if ( ctm != no_transfer ) {
            reply << "6 : cannot start " << args[0] << " while doing " << ctm << " ;";
            return reply.str();
        }

        // constraint the expected sizes
        const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                           (unsigned int)rte.trackbitrate(),
                                           rte.vdifframesize());
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

        // Start building the chain
        // clear lasthost so it won't bother the "getsok()" which
        // will, when the net_server is created, use the values in
        // netparms to decide what to do.
        // Also register cancellationfunctions that will close the
        // network and file filedescriptors and notify the threads
        // that it has done so - the threads pick up this signal and
        // terminate in a controlled fashion
        host[&rte] = rte.netparms.host;

        rte.netparms.host.clear();
        
        chain c;

        c.register_cancel( c.add(&netreader, 10, &net_server, networkargs(&rte)),
                           &close_filedescriptor);

        // Insert a decompressor if needed
        if( rte.solution ) {
            c.add(&blockdecompressor, 10, &rte);
        }

        if( arecibo.find(&rte)!=arecibo.end() ) {
            const dupvars& dvref = arecibo[&rte];
            if( dvref.expand ) {
                DEBUG(0, "Adding Arecibo duplication step" << endl);
                c.add(&duplicatorstep, 10, duplicatorargs(&rte, dvref.sz, dvref.factor));
            }
        }

        // And write to mem
        c.add( queue_writer, queue_writer_args(&rte) );

        // reset statistics counters
        rte.statistics.clear();
        rte.transfersubmode.clr_all();

        rte.transfermode = rtm;
        rte.processingchain = c;
        rte.processingchain.run();

        reply << "0 ;";

    }
    else if ( args[1] == "close" ) {
        if ( ctm != rtm ) {
            reply << "6 : not doing " << args[0] << " ;";
            return reply.str();
        }

        try {
            rte.processingchain.stop();
            reply << "0 ;";
        }
        catch ( std::exception& e ) {
            reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
        }
        catch ( ... ) {
            reply << " 4 : Failed to stop processing chain, unknown exception ;";
        }
        rte.transfersubmode.clr_all();
        rte.transfermode = no_transfer;

        // put back original host
        rte.netparms.host = host[&rte];
    }
    else if( ::strcasecmp(args[1].c_str(), "ar")==0 ) {
        // Command is:
        //   net2mem = ar : <sz> : <factor>
        // Will duplicate <sz> bits times <factor>
        char*         eocptr;
        dupvars&      dv = arecibo[&rte];
        const string  szstr( OPTARG(2, args) );
        const string  factorstr( OPTARG(3, args) );

        if( ctm!=no_transfer ) {
            reply << "6 : cannot change " << args[0] << " while doing " << ctm << " ;";
            return reply.str();
        }

        // Both arguments must be given
        ASSERT2_COND(szstr.empty()==false, SCINFO("Specify the input itemsize (in units of bits)"));
        ASSERT2_COND(factorstr.empty()==false, SCINFO("Specify a duplication factor"));

        dv.sz = (unsigned int)::strtoul(szstr.c_str(), &eocptr, 0);
        ASSERT2_COND(eocptr!=szstr.c_str() && *eocptr=='\0', 
                     SCINFO("Failed to parse the itemsize as a number"));
        dv.factor = (unsigned int)::strtoul(factorstr.c_str(), &eocptr, 0);
        ASSERT2_COND(eocptr!=factorstr.c_str() && *eocptr=='\0', 
                     SCINFO("Failed to parse the factor as a number"));
        dv.expand = true;
        reply << " 0 ;";
    }
    else {
        reply << "2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }

    return reply.str();
}

string mem2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream       reply;
    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if ( qry ) {
        reply << "0 : ";
        if ( rte.transfermode != mem2file ) {
            reply << "inactive ;";
            return reply.str();
        }

        if ( rte.processingchain.communicate(0, &queue_reader_args::is_finished)) {
            if ( rte.processingchain.communicate(1, &fdreaderargs::is_finished) ) {
                reply << "done";
            }
            else {
                reply << "flushing";
            }
        }
        else {
            reply << "active";
        }
        reply << " : " << rte.statistics.counter(1) << " ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        reply << "8 : command requires an argument ;";
        return reply.str();
    }

    if ( args[1] == "on" ) {
        if ( rte.transfermode != no_transfer ) {
            reply << "6 : cannot start " << args[0] << " while doing " << rte.transfermode << " ;";
            return reply.str();
        }

        // mem2file = on : <file> : <max bytes in buffer> [: <file option>]
        const string filename    ( OPTARG(2, args) );
        const string bytes_string( OPTARG(3, args) );
              string option      ( OPTARG(4, args) );
        
        if ( filename.empty() ) {
            reply << "8 : command requires a destination file ;";
            return reply.str();
        }

        uint64_t bytes;
        char*    eocptr;
        bytes = ::strtoull(bytes_string.c_str(), &eocptr, 0);
        ASSERT2_COND( (bytes!=0 || eocptr!=bytes_string.c_str()) && !(bytes==~((uint64_t)0) && errno==ERANGE),
                      SCINFO("Failed to parse bytes to buffer") );

        if ( option.empty() ) {
            option = "n";
        }

        // now start building the processingchain
        const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                           (unsigned int)rte.trackbitrate(),
                                           rte.vdifframesize());
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
     
        unsigned int blocks = (bytes + rte.netparms.get_blocksize() - 1) / rte.netparms.get_blocksize(); // round up

        
        chain c;
        c.register_cancel( c.add(&queue_reader, blocks, queue_reader_args(&rte)),
                           &cancel_queue_reader);
   
        c.add(&fdwriter<block>,  &open_file, filename + "," + option, &rte );
       
        // reset statistics counters
        rte.statistics.clear();

        // clear the interchain queue, such that we do not have ancient data
        ASSERT_COND( rte.interchain_source_queue );
        rte.interchain_source_queue->clear();
        
        rte.transfermode    = mem2file;
        rte.processingchain = c;
        rte.processingchain.run();

        rte.processingchain.communicate(0, &queue_reader_args::set_run, true);
    }
    else if ( args[1] == "stop" ) {
        if ( rte.transfermode != mem2file ) {
            reply << "6 : not doing " << args[0] << " ;";
            return reply.str();
        }

        rte.processingchain.delayed_disable();
    }
    else if ( args[1] == "off" ) {
        if ( rte.transfermode != mem2file ) {
            reply << "6 : not doing " << args[0] << " ;";
            return reply.str();
        }

        rte.transfermode = no_transfer;

        rte.processingchain.stop();
    }
    else {
        reply << "2 : " << args[1] << " is not a valid command argument ;";
        return reply.str();
    }

    reply << "0 ;";
    return reply.str();
}

string mem2net_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // good, if we shouldn't even be here, get out
    if( ctm != no_transfer && ctm != mem2net ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.netparms.host << " : " << rte.transfersubmode;
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
    if( args[1]=="connect" ) {
        recognized = true;
        // if transfermode is already mem2net, we ARE already connected
        // (only mem2net::disconnect clears the mode to doing nothing)
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const bool              rtcp    = (rte.netparms.get_protocol()=="rtcp");

            // good. pick up optional hostname/ip to connect to
            // unless it's rtcp
            if( args.size()>2 && !args[2].empty() ) {
                if( !rtcp )
                    rte.netparms.host = args[2];
                else
                    DEBUG(0, args[0] << ": WARN! Ignoring supplied host '" << args[2] << "'!" << endl);
            }

            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // constrain sizes based on network parameters and optional
            // compression. If this is the Mark5A version of 
            // mem2net it can only yield mark4/vlba data and for
            // these formats the framesize/offset is irrelevant for
            // compression since each individual bitstream has full
            // headerinformation.
            // If, otoh, we're running on a mark5b we must look for
            // frames first and compress those.
            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
                
            // come up with a theoretical ipd
            compute_theoretical_ipd(rte);
                
            // now start building the processingchain
            queue_reader_args qargs(&rte);
            qargs.reuse_blocks = true;
            c.register_cancel(c.add(&queue_reader, 10, qargs),
                              &cancel_queue_reader);

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

                DEBUG(0, "mem2net: enabling compressor " << dataformat << endl);
                if( dataformat.valid() ) {
                    c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                    c.add(&framecompressor, 10, compressorargs(&rte));
                } else {
                    c.add(&blockcompressor, 10, &rte);
                }
            }
                
            // Write to network
            c.register_cancel(c.add(&netwriter<block>, &net_client, networkargs(&rte)),
                              &close_filedescriptor);
            rte.transfersubmode.clr_all().set(wait_flag);

            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing
            rte.transfermode = mem2net;

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
    if ( args[1]=="on" ) {
        recognized = true;
        if ( rte.transfermode == mem2net && (rte.transfermode & wait_flag) ) {
            // clear the interchain queue, such that we do not have ancient data
            ASSERT_COND( rte.interchain_source_queue );
            rte.interchain_source_queue->clear();
                
            rte.processingchain.communicate(0, &queue_reader_args::set_run, true);
            reply << " 0 ;";
        }
        else {
            reply << " 6 : " << args[0] << " not connected ;";
        }
    }
    // <disconnect>
    if( ( args[1]=="disconnect" ) ) {
        recognized = true;
        // Only allow if we're doing mem2net.
        // Don't care if we were running or not
        if( rte.transfermode!=no_transfer ) {
            try {
                // do a blunt stop. at the sending end we do not care that
                // much processing every last bit still in our buffers
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }

            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();
        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}


string mem2sfxc_fn(bool qry, const vector<string>& args, runtime& rte ) {
    const transfer_type rtm( string2transfermode(args[0]) );
    const transfer_type ctm( rte.transfermode );

    ostringstream       reply;
    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if ( qry ) {
        reply << "0 : " << (ctm == rtm ? "active" : "inactive") << " ;";
        return reply.str();
    }

    // handle command
    if ( args.size() < 2 ) {
        reply << "8 : " << args[0] << " requires a command argument ;";
        return reply.str();
    }
    
    if ( args[1] == "open" ) {
        if ( ctm != no_transfer ) {
            reply << "6 : cannot start " << args[0] << " while doing " << ctm << " ;";
            return reply.str();
        }

        if ( args.size() < 3 ) {
            reply << "8 : open command requires a file argument ;";
            return reply.str();            
        }

        const string filename( args[2] );

        // Now that we have all commandline arguments parsed we may
        // construct our headersearcher
        const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                           (unsigned int)rte.trackbitrate(),
                                           rte.vdifframesize());
        
        // set read/write and blocksizes based on parameters,
        // dataformats and compression
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
        
        chain c;
        
        // start with a queue reader
        c.register_cancel(c.add(&stupid_queue_reader, 10, queue_reader_args(&rte)),
                          &cancel_queue_reader);
        // Insert fake frame generator
        c.add(&faker, 10, fakerargs(&rte));
        
        // And write into a socket
        c.register_cancel( c.add(&sfxcwriter, &open_sfxc_socket, filename, &rte),
                           &close_filedescriptor);

        // reset statistics counters
        rte.statistics.clear();

        rte.transfermode    = rtm;
        rte.processingchain = c;
        rte.processingchain.run();
        
        rte.processingchain.communicate(0, &queue_reader_args::set_run, true);
        
       reply << " 0 ;";

    }
    else if ( args[1] == "close" ) {
        if ( ctm != rtm ) {
            reply << "6 : not doing " << args[0] << " ;";
            return reply.str();
        }
        
        try {
            rte.processingchain.stop();
            reply << "0 ;";
        }
        catch ( std::exception& e ) {
            reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
        }
        catch ( ... ) {
            reply << " 4 : Failed to stop processing chain, unknown exception ;";
        }
        
        rte.transfersubmode.clr_all();
        rte.transfermode = no_transfer;
        
   }
    else {
        reply << "2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    return reply.str();
}

// connect to the datastream, find the frames and decode the timestamps
string mem2time_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // need to remember the stepid of the timedecoder step
    static per_runtime<chain::stepid> timepid;
    // automatic variables
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // good, if we shouldn't even be here, get out
    if( ctm!=no_transfer && ctm!=mem2time ) {
        reply << " 6 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            // get the last os + data timestamps and format them
            double                 dt;
            struct tm              gmt;
            const timegrabber_type times = rte.processingchain.communicate(timepid[&rte], &timegrabber_type::get_times);

            ::gmtime_r(&times.os_time.tv_sec, &gmt);
            reply << "O/S : " << tm2vex(gmt, times.os_time.tv_nsec) << " : ";
            ::gmtime_r(&times.data_time.tv_sec, &gmt);
            reply << "data : " << tm2vex(gmt, times.data_time.tv_nsec) ;
            dt = (double)(times.os_time.tv_sec - times.data_time.tv_sec) + 
                 (((double)times.os_time.tv_nsec)/1.0e9 - ((double)times.data_time.tv_nsec)/1.0e9);
            reply << " : " << format("%.3lf", dt) << "s";
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
        // Attempt to set up the timedecoding chain
        if( rte.transfermode==no_transfer ) {
            chain                   c;
            const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                               (unsigned int)rte.trackbitrate(),
                                               rte.vdifframesize());

            // In order to be able to decode data we certainly must have
            // a valid data format
            EZASSERT(dataformat.valid(), Error_Code_6_Exception);

            // now start building the processingchain

            // Read from the interchain queue. We use the stupid reader
            // because we send the blocks straight into a framer, which
            // doesn't care about blocksizes. And we can re-use the 
            // blocks because we don't need to go a different blocksize
            queue_reader_args qargs(&rte);
            qargs.run          = true;
            qargs.reuse_blocks = true;
            c.register_cancel(c.add(&stupid_queue_reader, 10, qargs),
                              &cancel_queue_reader);

            // Add the framer
            c.add(&framer<frame>, 10, framerargs(dataformat, &rte));
                
            // And the timegrabber
            timepid[&rte] = c.add(&timegrabber);

            rte.transfersubmode.clr_all().set(run_flag);

            // reset statistics counters
            rte.statistics.clear();

            // Update global transferstatus variables to
            // indicate what we're doing
            rte.transfermode = mem2time;

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
    // <close>
    if( ( args[1]=="close" ) ) {
        recognized = true;
        // Only allow if we're doing mem2time.
        // Don't care if we were running or not
        if( rte.transfermode!=no_transfer ) {
            try {
                // do a blunt stop. at the sending end we do not care that
                // much processing every last bit still in our buffers
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }

            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();
            timepid.erase( &rte );

        } else {
            reply << " 6 : Not doing " << args[0] << " ;";
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

#if 0
string getlength_fn( bool, const vector<string>&, runtime& rte ) {
    ostringstream  reply;
    S_DIR          curDir;

    try {
        UserDirectory  ud( rte.xlrdev );
        const ScanDir& sd( ud.scanDir() );

        for( unsigned int i=0; i<sd.nScans(); ++i ) {
            DEBUG(0, sd[i] << endl);
        }
    }
    catch( ... ) 
    {}

    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &curDir) );
    reply << "!getlength = 0 : L" << curDir.Length << " : AL" << curDir.AppendLength
          << ": XLRGL" << ::XLRGetLength(rte.xlrdev.sshandle()) << " ;";
    return reply.str();
}

string erase_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    UserDirectory ud( rte.xlrdev );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
       reply << "0 : " << ((ud==UserDirectory())?(""):("not ")) << "erased ;";
       return reply.str();
    }

    // Ok must be command.
    // Erasen met die hap!
    ud = UserDirectory();
    ud.write( rte.xlrdev );
    XLRCALL( ::XLRErase(rte.xlrdev.sshandle(), SS_OVERWRITE_NONE) );
    reply << " 0;";
    return reply.str();
}

#endif

string mk5a_clock_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream               reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing record - we shouldn't be here!
    if( qry ) {
        reply << " 0 : " << !(rte.ioboard[ mk5areg::notClock ]) << " ;";
        return reply.str();
    }

    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    //rte.ioboard[ mk5areg::notClock ] = (args[1]=="off");
    if( args[1]=="on" ) {
        in2net_transfer<mark5a>::start(rte);
        reply << " 0 ; ";
    } else if (args[1]=="off" ) {
        in2net_transfer<mark5a>::stop(rte);
        reply << " 0 ; ";
    } else  {
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }

    return reply.str();
}

// Really, in2disk is 'record'. But in lieu of naming conventions ...
// the user won't see this name anyway :)
// Note: do not stick this one in the Mark5B/DOM commandmap :)
// Oh well, you'll get exceptions when trying to execute then
// anyway
string in2disk_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // This points to the scan being recorded, if any
    static ScanPointer      curscanptr;    
    // automatic variables
    ostringstream               reply;
    ioboard_type::iobflags_type hardware( rte.ioboard.hardware() );

    // If we're not supposed to be here!
    ASSERT_COND( (hardware&ioboard_type::mk5a_flag || hardware&ioboard_type::dim_flag) );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing record - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=in2disk ) {
        reply << " 6 : _something_ is happening and its NOT in2disk!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
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
                }
                else if ( rte.ioboard.hardware()&ioboard_type::dim_flag ) {
                    if ( dev_status.Overflow[0] || rte.ioboard[mk5breg::DIM_OF] ) {
                        reply << "overflow";
                    }
                    else {
                        reply << "on";
                    }
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
            S_DIR         disk;
            string        scan( args[2] );
            string        experiment( OPTARG(3, args) );
            string        station( OPTARG(4, args) );
            string        source( OPTARG(5, args) );
            string        scanlabel;
            XLRCODE(SSHANDLE ss( rte.xlrdev.sshandle() ));
            S_DEVINFO     devInfo;

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

            // construct the scanlabel
            if( !experiment.empty() )
                scanlabel = experiment;
            if( !station.empty() ) {
                if( !scanlabel.empty() )
                    station = "_"+station;
                scanlabel += station;
            }
            if( !scan.empty() ) {
                if( !scanlabel.empty() )
                    scan = "_"+scan;
                scanlabel += scan;
            }
            // and finally, optionally, the source
            if( !source.empty() ) {
                if( !scanlabel.empty() )
                    source = "_"+source;
                scanlabel += source;
            }
            // Now then. If the scanlabel is *still* empty
            // we give it the value of '+'
            if( scanlabel.empty() )
                scanlabel = "+";

            // Depending on Mk5A or Mk5B/DIM ...
            // switch off clock (mk5a) or
            // stop the DFH-generator
            if( hardware&ioboard_type::mk5a_flag )
                rte.ioboard[ mk5areg::notClock ] = 1;
            else
                rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;

            // Already program the streamstor, do not
            // start Recording otherwise we can't read/write
            // the UserDirectory.
            // Let it record from FPDP -> Disk
            XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
            XLRCALL( ::XLRClearChannels(ss) );
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
            XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );

            // HV: Take care of Amazon - as per Conduant's
            //     suggestion
            XLRCODE( UINT     u32recvMode;)
            XLRCODE( UINT     u32recvOpt;)
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

            curscanptr = rte.xlrdev.startScan( scanlabel );

            // Great, now start recording & kick off the I/O board
            //XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 0) );
            XLRCALL( ::XLRAppend(ss) );

            if( hardware&ioboard_type::mk5a_flag )
                rte.ioboard[ mk5areg::notClock ] = 0;
            else
                start_mk5b_dfhg( rte );

            // Update global transferstatus variables to
            // indicate what we're doing
            rte.statistics.clear();
            rte.transfermode    = in2disk;
            rte.transfersubmode.clr_all();
            // in2disk is running immediately
            rte.transfersubmode |= run_flag;
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }
    if( args[1]=="off" ) {
        recognized = true;
        // only allow if transfermode==in2disk && submode has the run flag
        if( rte.transfermode==in2disk && (rte.transfersubmode&run_flag)==true ) {
            string error_message;
            
            try {
                // Depending on the actual hardware ...
                // stop transferring from I/O board => streamstor
                if( hardware&ioboard_type::mk5a_flag ) {
                    rte.ioboard[ mk5areg::notClock ] = 1;
                }
                else {
                    // we want to end at a whole second, first pause the ioboard
                    rte.ioboard[ mk5breg::DIM_PAUSE ] = 1;

                    // wait one second, to be sure we got an 1pps
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
            }
            catch ( std::exception& e ) {
                error_message += string(" : Failed to stop I/O board: ") + e.what();
            }
            catch ( ... ) {
                error_message += string(" : Failed to stop I/O board, unknown exception");
            }

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


// The 1PPS source command for Mk5B/DIM
string pps_source_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // Pulse-per-second register value in HumanReadableFormat
    static const string pps_hrf[4] = { "none", "altA", "altB", "vsi" };
    const unsigned int  npps( sizeof(pps_hrf)/sizeof(pps_hrf[0]) );
    // variables
    unsigned int                 selpps;
    ostringstream                oss;
    ioboard_type::mk5bregpointer pps = rte.ioboard[ mk5breg::DIM_SELPP ];

    oss << "!" << args[0] << (qry?('?'):('='));
    if( qry ) {
        oss << " 0 : " << pps_hrf[ *pps ] << " ;";
        return oss.str();
    }
    // It was a command. We must have (at least) one argument [the first, actually]
    // and it must be non-empty at that!
    if( args.size()<2 || args[1].empty() ) {
        oss << " 8 : Missing argument to command ;";
        return oss.str();
    }
    // See if we recognize the pps string
    for( selpps=0; selpps<npps; ++selpps )
        if( ::strcasecmp(args[1].c_str(), pps_hrf[selpps].c_str())==0 )
            break;
    if( selpps==npps ) {
        oss << " 6 : Unknown PPS source '" << args[1] << "' ;";
    } else {
        // write the new PPS source into the hardware
        pps = selpps;
        oss << " 0 ;";
    }
    return oss.str();
}

// mtu function
string mtu_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));
    if( q ) {
        oss << " 0 : " << np.get_mtu() << " ;";
        return oss.str();
    }
 
    // only allow command when no transfer is running
    if( rte.transfermode!=no_transfer ) {
        oss << " 6 : Not allowed to change during transfer ;";
        return oss.str();
    } 

    // command better have an argument otherwise 
    // it don't mean nothing
    if( args.size()>=2 && args[1].size() ) {
        unsigned int  m = (unsigned int)::strtol(args[1].c_str(), 0, 0);

        np.set_mtu( m );
        oss << " 0 ;";
    } else {
        oss << " 8 : Missing argument to command ;";
    }
    return oss.str();
}

// net_port function
string net_port_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));
    if( q ) {
        oss << " 0 : " << np.get_port() << " ;";
        return oss.str();
    }
 
    // only allow command when no transfer is running
    if( rte.transfermode!=no_transfer ) {
        oss << " 6 : Not allowed to change during transfer ;";
        return oss.str();
    } 

    // command better have an argument otherwise 
    // it don't mean nothing
    if( args.size()>=2 && args[1].size() ) {
        unsigned int  m = (unsigned int)::strtol(args[1].c_str(), 0, 0);

        np.set_port( m );
        oss << " 0 ;";
    } else {
        oss << " 8 : Missing argument to command ;";
    }
    return oss.str();
}

// tstat? 
//   "old" style/default output format:
//   !tstat? 0 : <delta-t> : <transfer> : <step1 rate> : <step2 rate> : ... ;
//      with 
//         <stepN rate> formatted as "<StepName> <float>[mckMG]bps"
//         <delta-t>    elapsed wall-clock time since last invocation of
//                      "tstat?". If >1 user is polling "tstat?" you'll
//                      get funny results
//
// tstat= <mumbojumbo>  (tstat as a command rather than a query)
//   whatever argument you specify is completely ignored.
//   the format is now:
//
//   !tstat= 0 : <timestamp> : <transfer> : <step1 name> : <step1 counter> : <step2 name> : <step2 counter>
//       <timestamp>  UNIX timestamp + added millisecond fractional seconds formatted as a float
//      
//       This allows you to poll at your own frequency and compute the rates
//       for over that period. Or graph them. Or throw them away.
string tstat_fn(bool q, const vector<string>& args, runtime& rte ) {
    double                          dt;
    uint64_t                        fifolen;
    const double                    fifosize( 512 * 1024 * 1024 );
    ostringstream                   reply;
    chainstats_type                 current;
    struct timeb                    time_cur;
    static per_runtime<timeb*>      time_last_per_runtime;
    struct timeb*&                  time_last = time_last_per_runtime[&rte];
    static per_runtime<chainstats_type> laststats_per_runtime;
    chainstats_type&                laststats = laststats_per_runtime[&rte];
    chainstats_type::const_iterator lastptr, curptr;

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    if( rte.transfermode==no_transfer ) {
        reply << "0 : 0.0 : no_transfer ;";
        return reply.str();
    }

    // must serialize access to the StreamStor
    // (hence the do_xlr_[un]lock();
    do_xlr_lock();
    ftime( &time_cur );
    fifolen = ::XLRGetFIFOLength(rte.xlrdev.sshandle());
    do_xlr_unlock();

    // make a copy of the statistics with the lock on the runtimeenvironment
    // held
    RTEEXEC(rte, current=rte.statistics);

    // Are we called as a command? Then our lives are much easier!
    if( !q ) {
        double tijd = (double)time_cur.time + ((double)time_cur.millitm)/1.0e3;

        // indicate succes and output timestamp + transfermode
        reply << " 0 : "
              << format("%.3lf", tijd) << " : "
              << rte.transfermode ;

        // output each chainstatcounter
        for(curptr=current.begin(); curptr!=current.end(); curptr++)
            reply << " : " << curptr->second.stepname << " : " << curptr->second.count;

        // finish off with the FIFOLength counter
        reply << " : FIFOLength : " << fifolen;

        // and terminate the reply
        reply << ';';
        return reply.str();
    }

    // Must check if the current transfer matches the saved one - if not we
    // must restart our timing
    for(lastptr=laststats.begin(), curptr=current.begin();
        lastptr!=laststats.end() && curptr!=current.end() &&
            lastptr->first==curptr->first && // check that .first (==stepid) matches
            lastptr->second.stepname==curptr->second.stepname; // check that stepnames match
        lastptr++, curptr++) {};
    // If not both lastptr & curptr point at the end of their respective
    // container we have a mismatch and must start over
    if( !(lastptr==laststats.end() && curptr==current.end()) ) {
        delete time_last;
        time_last = NULL;
    }

    if( !time_last ) {
        time_last = new struct timeb;
        *time_last = time_cur;
    }

    // Compute 'dt'. If it's too small, do not even try to compute rates
    dt = (time_cur.time + time_cur.millitm/1000.0) - 
         (time_last->time + time_last->millitm/1000.0);

    if( dt>0.1 ) {
        double fifolevel    = ((double)fifolen/fifosize) * 100.0;

        // Indicate success and report dt in seconds and the
        // transfer we're running
        reply << " 0 : "
              << format("%5.2lfs", dt) << " : "
              << rte.transfermode ;
        // now, for each step compute the rate. we've already established
        // equivalence making the stop condition simpler
        for(curptr=current.begin(), lastptr=laststats.begin();
            curptr!=current.end(); curptr++, lastptr++) {
            double rate = (((double)(curptr->second.count-lastptr->second.count))/dt)*8.0;
            reply << " : " << curptr->second.stepname << " " << sciprintd(rate,"bps");
        }
        // Finish off with the FIFO percentage
        reply << " : F" << format("%4.1lf%%", fifolevel) << " ;";
    } else {
        // dt is too small; request to try again
        reply << " 1 : Retry - we're initialized now : " << rte.transfermode << " ;";
    }

    // Update statics
    *time_last = time_cur;
    laststats  = current;
    return reply.str();
}

string memstat_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream                   reply;

    // This part of the reply we can already form
    reply << "!" << args[0] << ((q)?('?'):('=')) << " ";

    if( !q ) {
        reply << " 2 : query only ;";
        return reply.str();
    }
    reply << " 0 : " << rte.get_memory_status() << " ;";
    return reply.str();
}


string evlbi_fn(bool q, const vector<string>& args, runtime& rte ) {
    string        fmt("total : %t : loss : %l (%L) : out-of-order : %o (%O) : extent : %R");
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) << " 0 : ";
    if( !q ) {
        unsigned int                   n;
        ostringstream                  usrfmt;
        vector<string>::const_iterator vs = args.begin();
        if( vs!=args.end() )
            vs++;
        for( n=0; vs!=args.end(); n++, vs++ )
            usrfmt << (n?" : ":"") << *vs;
        fmt = usrfmt.str();
    }
    reply << fmt_evlbistats(rte.evlbi_stats, fmt.c_str()) << " ;";
    return reply.str();
}


struct conditionguardargs_type {
    runtime*    rteptr;

    conditionguardargs_type( runtime& r ) : 
        rteptr(&r)
    {}
    
private:
    conditionguardargs_type();
};

void* conditionguard_fun(void* args) {
    // takes ownership of args
    conditionguardargs_type* guard_args = (conditionguardargs_type*)args;
    try {
        S_DEVSTATUS status;
        do {
            sleep( 3 ); // copying SSErase behaviour here
            XLRCALL( ::XLRGetDeviceStatus(guard_args->rteptr->xlrdev.sshandle(),
                                          &status) );
        } while ( status.Recording );
        
        if ( guard_args->rteptr->disk_state_mask & runtime::erase_flag ) {
            guard_args->rteptr->xlrdev.write_state( "Erased" );
        }

        RTEEXEC( *guard_args->rteptr, guard_args->rteptr->transfermode = no_transfer);

    }
    catch ( const std::exception& e) {
        DEBUG(-1, "conditioning execution threw an exception: " << e.what() << std::endl );
        guard_args->rteptr->transfermode = no_transfer;
    }
    catch ( ... ) {
        DEBUG(-1, "conditioning execution threw an unknown exception" << std::endl );        
        guard_args->rteptr->transfermode = no_transfer;
    }

    delete guard_args;
    return NULL;
}

string reset_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream          reply;

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    if ( q ) {
        reply << "2 : only implemented as command ;";
        return reply.str();
    }

    if ( args.size() != 2 ) {
        reply << "8 : command requires excatly one argument ;";
        return reply.str();
    }

    if ( args[1] == "abort" ) {
        // in case of error, set transfer to no transfer
        // the idea is: it is better to be in an unknown state that might work
        // than in a state that is known but useless
        if ( rte.transfermode == disk2net ||
             rte.transfermode == disk2file || 
             rte.transfermode == file2disk 
             ) {
            try {
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
                rte.transfermode = no_transfer;
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
                rte.transfermode = no_transfer;
            }
        }
        else if ( rte.transfermode == condition ) {
            try {
                // has to be called twice (according to docs)
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop streamstor: " << e.what() << " ;";
                rte.transfermode = no_transfer;
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop streamstor, unknown exception ;";
                rte.transfermode = no_transfer;
            }
            reply << "0 ;";
        }
        else {
            reply << "6 : nothing running to abort ;";
        }
        return reply.str();
    }

    if ( rte.protected_count == 0 ) {
        reply << "6 : need an immediate preceding protect=off ;";
        return reply.str();
    }

    if ( rte.transfermode != no_transfer ) {
        reply << "6 : cannot erase while " << rte.transfermode << " is in progress ;";
        return reply.str();
    }

    if ( args[1] == "erase" ) {
        rte.xlrdev.erase();
        rte.pp_current = 0;
        rte.xlrdev.write_state( "Erased" );
    }
    else if ( args[1] == "erase_last_scan" ) {
        rte.xlrdev.erase_last_scan();
        rte.xlrdev.write_state( "Erased" );
    }
    else if ( args[1] == "condition" ) {
        rte.xlrdev.start_condition();
        rte.transfermode = condition;
        // create a thread to automatically stop the transfer when done
        pthread_t thread_id;
        pthread_attr_t tattr;
        PTHREAD_CALL( ::pthread_attr_init(&tattr) );
        PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) );
        conditionguardargs_type* guard_args = new conditionguardargs_type( rte );
        PTHREAD2_CALL( ::pthread_create( &thread_id, &tattr, conditionguard_fun, guard_args ),
                       delete guard_args );
    }
    else {
        reply << "2 : unrecognized control argument '" << args[1] << "' ;";
        return reply.str();
    }
    reply << "0 ;";
    return reply.str();
}

// specialization for Mark5B/DIM
// We do *not* test for DIM; others should've
// checked for us
// mode={ext|tvg|ramp}:<bitstreammask>[:<decimation ratio>[:<fpdpmode>]]
// fpdpmode not supported by this code.
// We allow 'tvg+<num>' to set a specific tvg mode. See runtime.h for 
// details. Default will map to 'tvg+1' [internal tvg]
string mk5bdim_mode_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    mk5b_inputmode_type curipm;

    // Wether this is command || query, we need the current inputmode
    rte.get_input( curipm );

    // This part of the reply we can already form
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        format_type fmt = rte.trackformat();

        if( is_vdif(fmt) )
            reply << "0 : " << fmt << " : " << rte.ntrack() << " : " << rte.vdifframesize() << " ;";
        else {
            // Decimation = 2^j
            const int decimation = (int)::round( ::exp(curipm.j * M_LN2) );
            reply << "0 : " << curipm.datasource << " : " << hex_t(curipm.bitstreammask)
                << " : " << decimation << " : "
                << (*rte.ioboard[mk5breg::DIM_II]) + 1
                << " ;";
        }
        return reply.str();
    }

    // Must be the mode command. Only allow if we're not doing a transfer
    if( rte.transfermode!=no_transfer ) {
        reply << "6 : cannot change during " << rte.transfermode << " ;";
        return reply.str();
    }
    // We require at least two non-empty arguments
    // ('data source' and 'bitstreammask')
    // (unless the mode == "none", in which case no ekztra arguments
    //  are req'd)
    if( (args.size()<=1) || /* only the command or nothing at all? that is never any good */
        (args.size()==2 && args[1]!="none") || /* only "mode = none" is acceptable in this case */
        (args.size()==3 && (args[1].empty() || args[2].empty())) /* not two non-empty arguments */
      ) {
        reply << "8 : must have at least two non-empty arguments ;";
        return reply.str(); 
    }

    // Are we setting VDIF?
    if( args[1].find("vdif")!=string::npos ) {
        rte.set_vdif(args);
        reply << " 0 ;";
        return reply.str();
    }
    // Start off with an empty inputmode.
    int                     tvgmode;
    mk5b_inputmode_type     ipm( mk5b_inputmode_type::empty );

    // Get the current inputmode. _some_ parameters must be left the same.
    // For (most, but not all) non-boolean parameters we have 'majik' values
    // indicating 'do not change this setting' but for booleans (and some other
    // 'verbatim' values that impossible).
    // So we just copy the current value(s) of those we want to keep unmodified.

    // use 'clock_set' to modify these!
    ipm.selcgclk  = curipm.selcgclk; 
    ipm.seldim    = curipm.seldim;
    ipm.seldot    = curipm.seldot;

    ipm.userword  = curipm.userword;
    ipm.startstop = curipm.startstop;
    ipm.tvrmask   = curipm.tvrmask;
    ipm.gocom     = curipm.gocom;

    // Other booleans (fpdp2/tvgsel a.o. are explicitly set below)
    // or are fine with their current default

    // Argument 1: the datasource
    // If the 'datasource' is "just" tvg, this is taken to mean "tvg+1"
    ipm.datasource     = ((args[1]=="tvg")?(string("tvg+1")):(args[1]));

    DEBUG(2, "Got datasource " << ipm.datasource << endl);

    // Now check what the usr wants
    if( ipm.datasource=="ext" ) {
        // aaaaah! Usr want REAL data!
        ipm.tvg        = 0;
        ipm.tvgsel     = false;
    } else if( ipm.datasource=="ramp" ) {
        // Usr want incrementing test pattern. Well, let's not deny it then!
        ipm.tvg        = 7;
        ipm.tvgsel     = true;
    } else if( ::sscanf(ipm.datasource.c_str(), "tvg+%d", &tvgmode)==1 ) {
        // Usr requested a specific tvgmode.
        ipm.tvg        = tvgmode;
        // Verify that we can do it

        // tvgmode==0 implies external data which contradicts 'tvg' mode.
        // Also, a negative number is out-of-the-question
        ASSERT2_COND( ipm.tvg>=1 && ipm.tvg<=8, SCINFO(" Invalid TVG mode number requested") );

        ipm.tvgsel     = true;

        // these modes request FPDP2, verify the H/W can do it
        if( ipm.tvg==3 || ipm.tvg==4 || ipm.tvg==5 || ipm.tvg==8 ) {
           ASSERT2_COND( rte.ioboard.hardware()&ioboard_type::fpdp_II_flag,
                         SCINFO(" requested TVG mode needs FPDP2 but h/w does not support it") );
           // do request FPDP2
           ipm.fpdp2   = true;
        }
    } else if( ipm.datasource=="none" ) {
        // Set mode directly - do not try to parse bitstreammask etc
        rte.set_input( ipm );
        reply << "0 ; ";
        return reply.str();
    } else {
        reply << "8 : Unknown datasource " << args[1] << " ;";
        return reply.str();
    }

    // Argument 2: the bitstreammask in hex.
    // Be not _very_ restrictive here. "The user will know
    // what he/she is doing" ... HAHAHAAA (Famous Last Words ..)
    // The 'set_input()' will do the parameter verification so
    // that's why we don't bother here
    ipm.bitstreammask  = ::strtoul( args[2].c_str(), 0, 16 );

    // Optional argument 3: the decimation.
    // Again, the actual value will be verified before it is sent to the H/W
    // The decimation is 'j', not 'k'! Bah!
    // Also: the argument is/should be given as one of: 1,2,4,8,16
    // the 'j' value is the exponent we must write into the H/W.
    if( args.size()>=4 && !args[3].empty() ) {
        int     i_decm;
        double  decm_req( ::strtod(args[3].c_str(), 0) ), decm_closest;

        // from the double value, find the closest exponent
        // of 2 that yields the requested decimation.
        i_decm       = (int)::round( ::log(decm_req)/M_LN2 );
        decm_closest = ::exp(i_decm * M_LN2);

        // We only allow decimation up to 16 [0 < i_decm <= 4]
        ASSERT2_COND( (i_decm>=0 && i_decm<=4),
                      SCINFO(" Requested decimation is not >=1 and <=16") );
        // And it must be a power of two!
        ASSERT2_COND( ::fabs(decm_req - decm_closest)<=0.01,
                      SCINFO(" Requested decimation is not a power of 2") );

        // Great. Now transfer the integer value to the h/w
        ipm.j = i_decm;
    }

    // Optional argument 4: fpdp2 mode, "1" or "0"
    const string fpdp2( OPTARG(4, args) );
    EZASSERT(fpdp2.empty()==true || fpdp2=="1" || fpdp2=="2", Error_Code_6_Exception);

    if( fpdp2=="2" ) {
        ipm.fpdp2 = true;
    } else {
        // default is false so if it was true
        // one of the modes requested it ("tvg+<n>", see above)
        // so we're now resetting it to false ... which might not be a good
        // idea
        EZASSERT2(ipm.fpdp2==false, Error_Code_6_Exception, EZINFO("FPDPII mode implied by tvg+<n> but 'fpdp2' argument would force to I"));
    }

    // Make sure other stuff is in correct setting
    ipm.gocom         = false;

    rte.set_input( ipm );

    reply << "0 ; ";
    // Return answer to caller
    return reply.str();
}

// specialization for Mark5A(+)
string mk5a_mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;

    // query can always be done
    if( qry ) {
        format_type  fmt = rte.trackformat();

        if( is_vdif(fmt) )
            reply << "!" << args[0] << "? 0 : " << fmt << " : " << rte.ntrack() << " : " << rte.vdifframesize() << " ;";
        else {
            inputmode_type  ipm;
            outputmode_type opm;

            rte.get_input( ipm );
            rte.get_output( opm );

            reply << "!" << args[0] << "? 0 : "
                << ipm.mode << " : " << ipm.submode << " : "
                << opm.mode << " : " << opm.submode << " : "
                << (opm.synced?('s'):('-')) << " : " << opm.numresyncs
                << " ;";
        }
        return reply.str();
    }

    // Command only allowed if doing nothing
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "!" << args[0] << "= 8 : Empty command (no arguments given, really) ;";
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

    // when setting vdif do not try to send it to the hardware
    if( ipm.mode.find("vdif")!=string::npos ) {
        rte.set_vdif(args);
        reply << "!" << args[0] << "= 0 ;";
        return reply.str();
    } 
    
    if( ipm.mode!="none" && !ipm.mode.empty() ) {
        // Looks like we're not setting the bypassmode for transfers

        // 2nd arg: submode
        if( args.size()>=3 ) {
            ipm.submode = opm.submode = args[2];
        }
    }

    // if output mode is set explicitly, override them
    if ( args.size() >= 5 ) {
        opm.mode = args[3];
        opm.submode = args[4];
    }

    // set mode to h/w
    if ( !ipm.mode.empty() ) {
        rte.set_input( ipm );
    }
    if ( !opm.mode.empty() ) {
        rte.set_output( opm );
    }

    // no reply yet indicates "ok"
    if( reply.str().empty() )
        reply << "!" << args[0] << "= 0 ;";
    return reply.str();
}

// Specialization for Mark5B/DOM. Currently it can only
// globally set the mode properties; no hardware
// settings are done. 
// This is here such that a Mark5B/DOM can do net2file
// correctly. (the sender and receiver of data have to
// have their modes set equally, for the constraint-solving
// to come up with the same values at either end of the
// transmission).
string mk5bdom_mode_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream          reply;
    mk5bdom_inputmode_type ipm( mk5bdom_inputmode_type::empty );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // query can always be done
    if( qry ) {
        const format_type  fmt = rte.trackformat();
        if( is_vdif(fmt) )
            reply << "0 : " << fmt << " : " << rte.ntrack() << " : " << rte.vdifframesize() << " ;";
        else {
            rte.get_input( ipm );
            reply << "0 : " << ipm.mode << " : " << rte.ntrack() << " : " << rte.trackformat() << " ;";
        }
        return reply.str();
    }

    // Command only allowed if doing nothing
    if( rte.transfermode!=no_transfer ) {
        reply << "6 : Cannot change during transfers ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "8 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    ipm.mode   = OPTARG(1, args);
    ipm.ntrack = OPTARG(2, args);

    // set mode to h/w
    if( ipm.mode.find("vdif")!=string::npos )
        rte.set_vdif(args);
    else
        rte.set_input( ipm );
    reply << "0 ;";

    return reply.str();
}

// Mark5A(+) playrate function
string playrate_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    outputmode_type opm;
    rte.get_output( opm );

    if( qry ) {
        double          clkfreq, clkgen;
        clkfreq  = opm.freq;
        clkfreq *= 9.0/8.0;

        clkgen = clkfreq;
        if ( opm.submode == "64" ) {
            clkgen *= 2;
        }

        // need implementation of table
        // listed in Mark5ACommands.pdf under "playrate" command
        reply << "0 : " << opm.freq << " : " << clkfreq << " : " << clkgen << " ;";
        return reply.str();
    }

    // if command, we require 'n argument
    if( (args.size()<2) || ((args[1] != "ext") && (args.size() < 3)) ) {
        reply << "8 : not enough arguments to command ;";
        return reply.str();
    }

    if ( args[1] == "ext" ) {
        // external, just program 0
        opm.freq = 0;
    }
    else {
        opm.freq = ::strtod(args[2].c_str(), 0);
        if ( (args[1] == "clock") || (args[1] == "clockgen") ) {
            // need to strip the 9/8 parity bit multiplier
            opm.freq /= 9.0/8.0;
            if ( (opm.mode == "vlba") || 
                 ((opm.mode == "st") && (opm.submode == "vlba")) ) {
                // and strip the the vlba header
                opm.freq /= 1.008;
            }
            if ( (args[1] == "clockgen") && (opm.submode == "64") ) {
                // and strip the frequency doubling
                opm.freq /= 2;
            }
        }
        else if ( args[1] != "data" ) {
            reply << " 8 : reference must be data, clock, clockgen or ext ;";
            return reply.str();
        }
    }
                  
    
    DEBUG(2, "Setting clockfreq to " << opm.freq << endl);
    rte.set_output( opm );
        
    // indicate success
    reply << " 0 ;";
    return reply.str();
}

// Mark5BDIM clock_set (replaces 'play_rate')
string clock_set_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream       reply;
    mk5b_inputmode_type curipm;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Get current inputmode
    rte.get_input( curipm );

    if( qry ) {
        double              clkfreq;
        
        // Get the 'K' registervalue: f = 2^(k+1)
        // Go from e^(k+1) => 2^(k+1)
        clkfreq = ::exp( ((double)(curipm.k+1))*M_LN2 );

        reply << "0 : " << clkfreq 
              << " : " << ((curipm.selcgclk)?("int"):("ext"))
              << " : " << curipm.clockfreq << " ;";
        return reply.str();
    }

    // if command, we require two non-empty arguments.
    // clock_set = <clock freq> : <clock source> [: <clock-generator-frequency>]
    if( args.size()<3 ||
        args[1].empty() || args[2].empty() ) {
        reply << "8 : must have at least two non-empty arguments ; ";
        return reply.str();
    }

    // Verify we recognize the clock-source
    ASSERT2_COND( args[2]=="int"||args[2]=="ext",
                  SCINFO(" clock-source " << args[2] << " unknown, use int or ext") );

    // We already got the current input mode.
    // Modify it such that it reflects the new clock settings.

    // If there is a frequency given, inspect it and transform it
    // to a 'k' value [and see if that _can_ be done!]
    int      k;
    string   warning;
    double   f_req, f_closest;

    f_req     = ::strtod(args[1].c_str(), 0);
    ASSERT_COND( (f_req>=0.0) );

    // can only do 2,4,8,16,32,64 MHz
    // cf IOBoard.c:
    // (0.5 - 1.0 = -0.5; the 0.5 gives roundoff)
    //k         = (int)(::log(f_req)/M_LN2 - 0.5);
    // HV's own rendition:
    k         = (int)::round( ::log(f_req)/M_LN2 ) - 1;
    f_closest = ::exp((k + 1) * M_LN2);
    // Check if in range [0<= k <= 5] AND
    // requested f close to what we can support
    ASSERT2_COND( (k>=0 && k<=5),
            SCINFO(" Requested frequency " << f_req << " <2 or >64 is not allowed") );
    ASSERT2_COND( (::fabs(f_closest - f_req)<0.01),
            SCINFO(" Requested frequency " << f_req << " is not a power of 2") );

    curipm.k         = k;

    // We do not alter the programmed clockfrequency, unless the
    // usr requests we do (if there is a 3rd argument,
    // it's the clock-generator's clockfrequency)
    curipm.clockfreq = 0;
    if( args.size()>=4 && !args[3].empty() )
        curipm.clockfreq = ::strtod( args[3].c_str(), 0 );

    // We already verified that the clocksource is 'int' or 'ext'
    // 64MHz *implies* using the external VSI clock; the on-board
    // clockgenerator can only do 40MHz
    // If the user says '64MHz' with 'internal' clock we just warn
    // him/her ...
    curipm.selcgclk = (args[2]=="int");
    if( k==5 && curipm.selcgclk )
        warning = "64MHz with internal clock will not fail but timecodes will be bogus";

    // Depending on internal or external clock, select the PCI interrupt source
    // (maybe it's valid to set both but I don't know)
    curipm.seldim = !curipm.selcgclk;
    curipm.seldot = curipm.selcgclk;

    // Send to hardware
    rte.set_input( curipm );
    reply << " 0";
    if( !warning.empty() )
        reply << " : " << warning;
    reply << " ;";
    return reply.str();
}

// Equivalents of playrate / clock_set but for generic PCs /Mark5C
// (which don't have an actual ioboard installed).
// But sometimes you must be able to specify the trackbitrate.
string mk5c_playrate_clockset_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream              reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";
    if( qry ) {
        const double rate = rte.trackbitrate()/1.0e6;
        reply << "0 : " << rate << " : " << rate << " : " << rate << " ;";
        return reply.str();
    }

    // if command, we require 'n argument
    // for now, we discard the first argument but just look at the frequency
    if( args.size()<3 ) {
        reply << "8 : not enough arguments to command ;";
        return reply.str();
    }

    // Depending on wether it was "play_rate = "
    // or "clock_set = " we do Stuff (tm)

    // play_rate = <ignored_for_now> : <freq>
    if( args[0]=="play_rate" ) {
        double       requested_frequency;
        const string frequency_arg( OPTARG(2, args) );

        ASSERT_COND(frequency_arg.empty()==false);
            
        requested_frequency = ::strtod(frequency_arg.c_str(), 0);
        ASSERT_COND(requested_frequency>0.0 && requested_frequency<=64.0);
        rte.set_trackbitrate( requested_frequency*1.0e6 );
        DEBUG(2, "play_rate[mk5c]: Setting clockfreq to " << rte.trackbitrate() << endl);
        reply << " 0 ;";
    } else if( args[0]=="clock_set" ) {
        const string clocksource( OPTARG(2, args) );
        const string frequency_arg( OPTARG(1, args) );

        // Verify we recognize the clock-source
        ASSERT2_COND( clocksource=="int"||clocksource=="ext",
                      SCINFO(" clock-source '" << clocksource << "' unknown, use int or ext") );
        ASSERT_COND(frequency_arg.empty()==false);

        // If there is a frequency given, inspect it and transform it
        // to a 'k' value [and see if that _can_ be done!]
        int      k;
        string   warning;
        double   f_req, f_closest;

        f_req     = ::strtod(frequency_arg.c_str(), 0);
        ASSERT_COND( (f_req>=0.0) );

        // can only do 2,4,8,16,32,64 MHz
        // cf IOBoard.c:
        // (0.5 - 1.0 = -0.5; the 0.5 gives roundoff)
        //k         = (int)(::log(f_req)/M_LN2 - 0.5);
        // HV's own rendition:
        k         = (int)::round( ::log(f_req)/M_LN2 ) - 1;
        f_closest = ::exp((k + 1) * M_LN2);
        // Check if in range [0<= k <= 5] AND
        // requested f close to what we can support
        ASSERT2_COND( (k>=0 && k<=5),
                      SCINFO(" Requested frequency " << f_req << " <2 or >64 is not allowed") );
        ASSERT2_COND( (::fabs(f_closest - f_req)<0.01),
                      SCINFO(" Requested frequency " << f_req << " is not a power of 2") );

        rte.set_trackbitrate( f_closest*1.0e6 );
        // Now it's safe to set the actual frequency
        DEBUG(2, "clock_set[mk5c]: Setting clockfreq to " << rte.trackbitrate() << endl);
        reply << " 0 ;";
    } else {
        ASSERT2_COND(false, SCINFO("command is neither play_rate nor clock_set"));
    }
    return reply.str();
}

// Expect:
// net_protcol=<protocol>[:<socbufsize>[:<blocksize>[:<nblock>]]
// 
// Note: existing uses of eVLBI protocolvalues mean that when "they" say
//       'netprotcol=udp' they *actually* mean 'netprotocol=udps'
//       (see netparms.h for details). We will transform this silently and
//       add another value, "pudp" which will get translated into plain udp.
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
        return string("!net_protocol = 8 : Empty command (no arguments given, really) ;");

    // Make sure the reply is RLY empty [see before "return" below why]
    reply.str( string() );

    // Extract potential arguments
    const string proto( OPTARG(1, args) );
    const string sokbufsz( OPTARG(2, args) );
    const string workbufsz( OPTARG(3, args) );
    const string nbuf( OPTARG(4, args) );

    // See which arguments we got
    // #1 : <protocol>
    if( proto.empty()==false )
        np.set_protocol( proto );

    // #2 : <socbuf size> [we set both send and receivebufsizes to this value]
    if( sokbufsz.empty()==false ) {
        char*      eptr;
        long int   v = ::strtol(sokbufsz.c_str(), &eptr, 0);

        // was a unit given? [note: all whitespace has already been stripped
        // by the main commandloop]
        EZASSERT2( eptr!=sokbufsz.c_str() && ::strchr("kM\0", *eptr),
                   cmdexception,
                   EZINFO("invalid socketbuffer size '" << sokbufsz << "'") );

        // Now we can do this
        v *= ((*eptr=='k')?KB:(*eptr=='M'?MB:1));

        // Check if it's a sensible "int" value for size, ie >=0 and <=INT_MAX
        if( v<0 || v>INT_MAX ) {
            reply << "!" << args[0] << " = 8 : <socbuf size> out of range <0 or > INT_MAX ; ";
        } else {
            np.rcvbufsize = np.sndbufsize = (int)v;
        }
    }

    // #3 : <workbuf> [the size of the blocks used by the threads]
    //      Value will be adjusted to accomodate an integral number of
    //      datagrams.
    if( workbufsz.empty()==false ) {
        char*               eptr;
        unsigned long int   v = ::strtoul(workbufsz.c_str(), &eptr, 0);

        // was a unit given? [note: all whitespace has already been stripped
        // by the main commandloop]
        EZASSERT2( eptr!=workbufsz.c_str() && ::strchr("kM\0", *eptr),
                   cmdexception,
                   EZINFO("invalid workbuf size '" << workbufsz << "'") );

        // Now we can do this
        v *= ((*eptr=='k')?KB:(*eptr=='M'?MB:1));

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v<=UINT_MAX ) {
            np.set_blocksize( (unsigned int)v );
        } else {
            reply << "!" << args[0] << " = 8 : <workbufsize> out of range (too large) ;";
        }
    }

    // #4 : <nbuf>
    if( nbuf.empty()==false ) {
        unsigned long int   v = ::strtoul(nbuf.c_str(), 0, 0);

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
    if ( rte.transfermode == condition ) {
        return "!status? 6 : not possible during conditioning ;";
    }

    // flag definitions for readability and consistency
    const unsigned int record_flag   = 0x1<<6; 
    const unsigned int playback_flag = 0x1<<8; 
    // automatic variables
    unsigned int       st;
    ostringstream      reply;

    // compile the hex status word
    st = 1; // 0x1 == ready
    switch( rte.transfermode ) {
        case in2disk:
        case in2memfork:
            st |= record_flag;
            break;
        case disk2file:
            st |= playback_flag;
            st |= (0x1<<12); // bit 12: disk2file
            break;
        case file2disk:
            st |= record_flag;
            st |= (0x1<<13); // bit 13: disk2file
            break;
        case disk2net:
            st |= playback_flag;
            st |= (0x1<<14); // bit 14: disk2net active
            break;
        case net2disk:
            st |= record_flag;
            st |= (0x1<<15); // bit 15: net2disk
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
    do_xlr_lock();
    XLR_ERROR_CODE error = ::XLRGetLastError();
    do_xlr_unlock();
    if ( error != 0 && error != 3 ) {
        st |= (0x1 << 1); // bit 1, error message pending
    }
    if ( rte.transfermode != no_transfer ) {
        st |= (0x3 << 3); // bit 3 and 4, delayed completion command pending
    }

    S_BANKSTATUS bs[2];
    XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_A, &bs[0]) );
    XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_B, &bs[1]) );
    for ( unsigned int bank = 0; bank < 2; bank++ ) {
        if ( bs[bank].Selected ) {
            st |= (0x1 << (20 + bank * 4)); // bit 20/24, bank selected
        }
        if ( bs[bank].State == STATE_READY ) {
            st |= (0x1 << (21 + bank * 4)); // bit 21/25, bank ready
        }
        if ( bs[bank].MediaStatus == MEDIASTATUS_FULL || bs[bank].MediaStatus == MEDIASTATUS_FAULTED ) {
            st |= (0x1 << (22 + bank * 4)); // bit 22/26, bank full or faulty (not writable)
        }
        if ( bs[bank].WriteProtected ) {
            st |= (0x1 << (23 + bank * 4)); // bit 23/27, bank write protected
        }
    }


    reply << "!status? 0 : " << hex_t(st) << " ;";
    return reply.str();
}

string debug_fn( bool , const vector<string>& args, runtime& rte ) {
    rte.ioboard.dbg();
    return string("!")+args[0]+"= 0 ;";
}

// set/qre the debuglevel
string debuglevel_fn(bool qry, const vector<string>& args, runtime&) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";
    if( qry ) {
        reply << "0 : " << dbglev_fn() << " : " <<  fnthres_fn() << " ;";
        return reply.str();
    }
    // if command, we must have an argument
    if( args.size()<2 ) {
        reply << " 8 : Command must have argument ;";
        return reply.str();
    }

    // (attempt to) parse the new debuglevel  
    // from the argument. No checks against the value
    // are done as all values are acceptable (<0, 0, >0)
    int    lev;
    string s;

    if( (s=OPTARG(1, args)).empty()==false ) {
        ASSERT_COND( (::sscanf(s.c_str(), "%d", &lev)==1) );
        // and install the new value
        dbglev_fn( lev );
    }
    if( (s=OPTARG(2, args)).empty()==false ) {
        ASSERT_COND( (::sscanf(s.c_str(), "%d", &lev)==1) );
        // and install the new value
        fnthres_fn( lev );
    }
    reply << " 0 ;";

    return reply.str();
}

string interpacketdelay_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    if( qry ) {
        reply << " 0 : ";
        if( rte.netparms.interpacketdelay<0 )
            reply << "auto : " << rte.netparms.theoretical_ipd << " usec";
        else 
            reply << rte.netparms.interpacketdelay << " usec";
        reply << " ;";
        return reply.str();
    }

    // if command, we must have an argument
    if( args.size()<2 || args[1].empty() ) {
        reply << " 8 : Command must have argument ;";
        return reply.str();
    }

    // (attempt to) parse the interpacket-delay-value
    // from the argument. No checks against the value
    // are done as all values are acceptable (<0, 0, >0)
    int   ipd;

    ASSERT_COND( (::sscanf(args[1].c_str(), "%d", &ipd)==1) );

    // great. install new value
    // Before we do that, grab the mutex, as other threads may be
    // using this value ...
    RTEEXEC(rte, rte.netparms.interpacketdelay=ipd);

    reply << " 0 ;";

    return reply.str();
}


typedef std::map<runtime*, int64_t> per_rte_skip_type;

string skip_fn( bool q, const vector<string>& args, runtime& rte ) {
    static per_rte_skip_type skips;
    // local variables
    int64_t        nskip;
    ostringstream  reply;
    
    reply << "!" << args[0] << (q?('?'):('='));
    
    if( q ) {
        reply << " 0 : " << skips[&rte] << " ;";
        return reply.str();
    }
    
    // Not a query. Only allow skip if doing a 
    // transfer to which it sensibly applies:
    if( !toout(rte.transfermode) ) {
        reply << " 6 : it does not apply to " << rte.transfermode << " ;";
        return reply.str();
    }

    // We rilly need an argument
    if( args.size()<2 || args[1].empty() ) {
        reply << " 8 : Command needs argument! ;";
        return reply.str();
    }

    // Now see how much to skip
    nskip    = ::strtol(args[1].c_str(), 0, 0);

    // Attempt to do the skip. Return value is always
    // positive so must remember to get the sign right
    // before testing if the skip was achieved
    // Must serialize access to the StreamStor, therefore
    // use do_xlr_lock/do_xlr_unlock
    do_xlr_lock();
    skips[&rte] = ::XLRSkip( rte.xlrdev.sshandle(),
                             (CHANNELTYPE)(::llabs(nskip)), (nskip>=0) );
    do_xlr_unlock();
    if( nskip<0 )
        skips[&rte] = -skips[&rte];

    // If the achieved skip is not the expected skip ...
    reply << " 0";
    if( skips[&rte]!=nskip )
        reply << " : Requested skip was not achieved";
    reply << " ;";
    return reply.str();
}

// This one works both on Mk5B/DIM and Mk5B/DOM
// (because it looks at the h/w and does "The Right Thing (tm)"
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
        reply << " 6 : This is not a Mk5B ;";
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
        mk5breg::led_colour            l0, l1;

        l0 = (mk5breg::led_colour)*led0;
        l1 = (mk5breg::led_colour)*led1;
        reply << " 0 : " << l0 << " : " << l1 << " ;";
        return reply.str();
    }

    // for DOM we must first enable the leds?
    if( hw&ioboard_type::dom_flag )
        rte.ioboard[mk5breg::DOM_LEDENABLE] = 1;

    if( args.size()>=2 && args[1].size() ) {
        led0 = text2colour(args[1]);
    }
    if( args.size()>=3 && args[2].size() ) {
        led1 = text2colour(args[2]);
    }
    reply << " 0 ; ";
    return reply.str();
}

string dtsid_fn(bool , const vector<string>& args, runtime& rte) {
    int                         ndim = 0, ndom = 0;
    ostringstream               reply;
    const transfer_type         tm( rte.transfermode );
    ioboard_type::iobflags_type hw = rte.ioboard.hardware();

    reply << "!" << args[0] << "? 0 : ";

    // <system type>
    if( hw&ioboard_type::mk5a_flag ) {
        reply << "mark5A";
        ndim = ndom = 1;
    } else if( hw&ioboard_type::mk5b_flag ) {
        reply << "mark5b";
        if( hw&ioboard_type::dim_flag )
            ndim = 1;
        else
            ndom = 1;
    } else
        reply << "-";
    // <software revision date> (timestamp of this SW version)
    reply << " : " << version_constant("DATE");
    // <media type>
    // 0 - magnetic tape, 1 - magnetic disk, 2 - realtime/nonrecording
    //  assume that if the transfermode == '*2net' or 'net2out' that we are
    //  NOT recording
    const bool realtime = (tm==in2net || tm==disk2net || tm==net2out);
    reply << " : " << ((realtime==true)?(2):(1));
    // <serial number>
    char   name[128];
    int    fd = ::open("/etc/hardware_id", O_RDONLY);
    string serial;

    if( fd>0 ) {
        int rr;
        if( (rr=::read(fd, name, sizeof(name)))>0 ) {
            vector<string> parts;
            // Use only the first line of that file; use everything up to 
            // the first newline.
            parts  = split(string(name), '\n');
            serial = parts[0];
        } else {
            serial = ::strerror(rr);
        }
        ::close(fd);
    } else {
        vector<string> parts;
        ::gethostname(name, sizeof(name));
        // split at "."'s and keep only first part
        parts = split(string(name), '.');
        serial = parts[0];
        DEBUG(3, "[gethostname]serial = '" << serial << "'" << endl);
    }
    reply << " : " << serial;
    // <#DIM ports>, <#DOM ports>
    reply << " : " << ndim << " : " << ndom;
    // <command set revision>
    reply << " : 2.7x";
    if( hw.empty() ) 
        // No Input/Output designrevisions 'cuz there ain't any
        reply << " : - : - ";
    else
        // <Input design revision> & <Output design revision> (in hex)
        reply << " : " << hex_t(rte.ioboard.idr())
              << " : " << hex_t(rte.ioboard.odr());

    reply << " ;";
    return reply.str();
}

// Display all version info we know about "SS_rev?"
// Only do it as query
string ssrev_fn(bool, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const S_DEVINFO&    devInfo( rte.xlrdev.devInfo() );
    const S_XLRSWREV&   swRev( rte.xlrdev.swRev() );

    reply << "!" << args[0] << "? ";

    // Active transfer? Don't allow it then! (technically, I think
    // it *could* be done - just to be compatible with Mark5A/John Ball)
    if( rte.transfermode!=no_transfer ) {
        reply << "6 : Not whilst doing " << rte.transfermode << ";";
        return reply.str();
    }

    // Get all the versions!
    reply << " 0 : "
          << "BoardType " << devInfo.BoardType << " : "
          << "SerialNum " << devInfo.SerialNum << " : "
          << "ApiVersion " << swRev.ApiVersion << " : "
          << "ApiDateCode " << swRev.ApiDateCode << " : "
          << "FirmwareVersion " << swRev.FirmwareVersion << " : "
          << "FirmDateCode " << swRev.FirmDateCode << " : "
          << "MonitorVersion " << swRev.MonitorVersion << " : "
          << "XbarVersion " << swRev.XbarVersion << " : " 
          << "AtaVersion " << swRev.AtaVersion << " : "
          << "UAtaVersion " << swRev.UAtaVersion << " : "
          << "DriverVersion " << swRev.DriverVersion;
    if( rte.xlrdev.isAmazon() ) {
        const S_DBINFO& dbInfo( rte.xlrdev.dbInfo() );

        reply << " : "
              << "AMAZON : "
              << "SerialNum " << dbInfo.SerialNum << " : "
              << "PCBVersion " << dbInfo.PCBVersion << " : "
              << "PCBType " << dbInfo.PCBType << " : "
              << "PCBSubType " << dbInfo.PCBSubType << " : "
              << "FPGAConfig " << dbInfo.FPGAConfig << " : "
              << "FPGAConfigVersion " << dbInfo.FPGAConfigVersion << " : "
              << "NumChannels " << dbInfo.NumChannels;
    }
    reply << " ;";
    return reply.str();
}

string scandir_fn(bool, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;

    UserDirectory::Layout layout = rte.xlrdev.userdirLayout();

    reply << "!" << args[0] << " = 0 : " << layout;
    if( layout!=UserDirectory::UnknownLayout ) {
        unsigned int   scannum( 0 );
        const string   scan( OPTARG(1, args) );

        reply << " : " << rte.xlrdev.nScans();
        if( !scan.empty() ) {
            unsigned long int    v = ::strtoul(scan.c_str(), 0, 0);

            if( ((v==ULONG_MAX) && errno==ERANGE) || v>=UINT_MAX )
                throw cmdexception("value for scannum is out-of-range");
            scannum = (unsigned int)v; 
        }
        if( scannum<rte.xlrdev.nScans() ) {
            ROScanPointer  rosp( rte.xlrdev.getScan(scannum) );

            reply << " : " << rosp.name() << " : " << rosp.start() << " : " << rosp.length();
        } else {
            reply << " : <scan # " << scannum << "> out of range";
        }
    } else {
        reply << " : 0";
    }
    reply << " ;";
    return reply.str();
}

// wait for 1PPS-sync to appear. This is highly Mk5B
// spezifik so if you call this onna Mk5A itz gonna FAIL!
// Muhahahahaa!
//  pps=* [force resync]  (actual argument ignored)
//  pps?  [report 1PPS status]
string pps_fn(bool q, const vector<string>& args, runtime& rte) {
    double              dt;
    ostringstream       reply;
    const double        syncwait( 3.0 ); // max time to wait for PPS, in seconds
    struct ::timeval    start, end;
    const unsigned int  selpp( *rte.ioboard[mk5breg::DIM_SELPP] );

    reply << "!" << args[0] << (q?('?'):('='));

    // if there's no 1PPS signal set, we do nothing
    if( selpp==0 ) {
        reply << " 6 : No 1PPS signal set (use 1pps_source command) ;";
        return reply.str();
    }

    // good, check if query
    if( q ) {
        const bool  sunk( *rte.ioboard[mk5breg::DIM_SUNKPPS] );
        const bool  e_sync( *rte.ioboard[mk5breg::DIM_EXACT_SYNC] );
        const bool  a_sync( *rte.ioboard[mk5breg::DIM_APERTURE_SYNC] );

        // check consistency: if not sunk, then neither of exact/aperture
        // should be set (i guess), nor, if sunk, may both be set
        // (the pps is either exact or outside the window but not both)
        reply << " 0 : " << (!sunk?"NOT ":"") << " synced ";
        if( e_sync )
            reply << " [not incident with DOT1PPS]";
        if( a_sync )
            reply << " [> 3 clocks off]";
        reply << " ;";
        return reply.str();
    }

    // ok, it was command.
    // trigger a sync attempt, wait for some time [3 seconds?]
    // at maximum for the PPS to occur, clear the PPSFLAGS and
    // then display the systemtime at which the sync occurred

    // Note: the poll-loop below might be implementen rather 
    // awkward but I've tried to determine the time-of-sync
    // as accurate as I could; therefore I really tried to 
    // remove as much "unknown time consuming" systemcalls
    // as possible.
    register bool      sunk = false;
    const unsigned int wait_per_iter = 2; // 2 microseconds/iteration
    unsigned long int  max_loops = ((unsigned long int)(syncwait*1.0e6)/wait_per_iter);

    // Pulse SYNCPPS to trigger zynchronization attempt!
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 1;
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 0;

    // now wait [for some maximum amount of time]
    // for SUNKPPS to transition to '1'
    ::gettimeofday(&start, 0);
    while( max_loops-- ) {
        if( (sunk=*rte.ioboard[mk5breg::DIM_SUNKPPS])==true )
            break;
        // Ok, SUNKPPS not 1 yet.
        // sleep a bit and retry
        busywait( wait_per_iter );
    };
    ::gettimeofday(&end, 0);
    dt = ((double)end.tv_sec + (double)end.tv_usec/1.0e6) -
        ((double)start.tv_sec + (double)start.tv_usec/1.0e6);

    if( !sunk ) {
        reply << " 4 : Failed to sync to 1PPS within " << dt << "seconds ;";
    } else {
        char      tbuf[128];
        double    frac_sec;
        struct tm gmt;

        // As per Mark5B-DIM-Registers.pdf Sec. "Typical sequence of operations":
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 1;
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 0;

        // convert 'timeofday' at sync to gmt
        ASSERT_NZERO( ::gmtime_r(&end.tv_sec, &gmt) );
        frac_sec = end.tv_usec/1.0e6;
        ::strftime(tbuf, sizeof(tbuf), "%a %b %d %H:%M:", &gmt);
        reply << " 0 : sync @ " << tbuf
              << format("%0.8lfs", ((double)gmt.tm_sec+frac_sec)) << " [GMT]" << " ;";
    }
    return reply.str();
}


// report time of last generated disk-frame
// DOES NO CHECK AT ALL if a recording is running!
string dot_fn(bool q, const vector<string>& args, runtime& rte) {
    ioboard_type& iob( rte.ioboard );
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('='));
    if( !q ) {
        reply << " 2 : Only available as query ;";
        return reply.str();
    }

    const bool          fhg = *iob[mk5breg::DIM_STARTSTOP];
    pcint::timediff     delta; // 0 by default, filled in when necessary
    pcint::timeval_type os_now  = pcint::timeval_type::now();

    // Time fields that need filling in
    int      y, doy, h, m;
    double   s, frac = 0.0; // seconds + fractional seconds

    // Depending on wether FHG running or not, take time
    // from h/w or from the pseudo-dot
    if( fhg ) {
        int                 tmjd, tmjd0, tmp;
        dot_type            dot_info = get_dot();
        struct tm           tm_dot;
        struct timeval      tv;

        // Good, fetch the hdrwords from the last generated DISK-FRAME
        // and decode the hdr.
        // HDR2:   JJJSSSSS   [day-of-year + seconds within day]
        // HDR3:   SSSS****   [fractional seconds]
        //    **** = 16bit CRC
        // At the same time get the current DOT
        unsigned int hdr2 = (unsigned int)((((unsigned int)*iob[mk5breg::DIM_HDR2_H])<<16)|(*iob[mk5breg::DIM_HDR2_L]));
        unsigned int hdr3 = (unsigned int)((((unsigned int)*iob[mk5breg::DIM_HDR3_H])<<16)|(*iob[mk5breg::DIM_HDR3_L]));

        // hdr2>>(5*4) == right-shift hdr2 by 5BCD digits @ 4bits/BCD digit
        // NOTE: doy processing is a two-step process. The 3 BCD 'day' digits in
        // the Mark5B timecode == basically a VLBA timecode == Truncated MJD
        // daynumber. We'll get the tmjd first. Actual DOY will be computed
        // later on.
        unbcd((hdr2>>20), tmjd);
        // Get out all the second values
        //   5 whole seconds
        unbcd(hdr2&0x000fffff, tmp);
        s    = (double)tmp;
        // Now get the 4 fractional second digits
        unbcd(hdr3>>16, tmp);
        s   += ((double)tmp * 1.0e-4);
        // Now the decode to h/m/s can take place
        h    = (int)(s/3600.0);
        s   -= (h*3600);
        m    = (int)(s/60.0);
        s   -= (m*60);
        // break up seconds into integral seconds + fractional part
        frac = ::modf(s, &s);

        // need to get the current year from the DOT clock
        ::gmtime_r(&dot_info.dot.timeValue.tv_sec, &tm_dot);
        y    = tm_dot.tm_year + 1900;

        // as eBob pointed out: doy starts at 1 rather than 0?
        // ah crap
        // The day-of-year = the actual daynumber - MJD at begin of the
        // current year.
        // In order to compute the actual day-of-year we must subtract 
        // the 'truncated MJD' of day 0 of the current year from the
        // 'truncated MJD' found in the header.
        // So at some point we have to be prepared to TMJD wrapping (it
        // wraps, inconveniently, every 1000 days ...) between day 0 of the
        // current year and the actual tmjd we read from the h/w.
        // Jeebus!

        // Get the TMJD for day 0 of the current year
        tmjd0 = jdboy(y) % 1000;
        // Now we can compute doy, taking care of wrappage
        doy   = (tmjd0<=tmjd)?(tmjd - tmjd0):(1000 - tmjd0 + tmjd);
        doy++;

        // Overwrite values read from the FHG - 
        // eg. year is not kept in the FHG, we take it from the OS
        tm_dot.tm_yday = doy - 1;
        tm_dot.tm_hour = h;
        tm_dot.tm_min  = m;
        tm_dot.tm_sec  = (int)s;

        // Transform back into a time
        tv.tv_usec     = 0;
        tv.tv_sec      = mktime(&tm_dot);

        // Now we can finally compute delta(DOT, OS time)
        delta =  (pcint::timeval_type(tv)+frac) - dot_info.lcl;
    } else {
        dot_type         dot_info = get_dot();
        struct tm        tm_dot;

        // Go from time_t (member of timeValue) to
        // struct tm. Struct tm has fields month and monthday
        // which we use for getting DoY
        ::gmtime_r(&dot_info.dot.timeValue.tv_sec, &tm_dot);
        y     = tm_dot.tm_year + 1900;
        doy   = tm_dot.tm_yday + 1;
        h     = tm_dot.tm_hour;
        m     = tm_dot.tm_min;
        s     = tm_dot.tm_sec;
        frac  = (dot_info.dot.timeValue.tv_usec * 1.0e-6);
        delta = dot_info.dot - dot_info.lcl;
    }

    // Now form the whole reply
    const bool   pps = *iob[mk5breg::DIM_SUNKPPS];
    unsigned int syncstat;
    const string stattxt[] = {"not_synced",
                        "syncerr_eq_0",
                        "syncerr_le_3",
                        "syncerr_gt_3" };
    // start with not-synced status
    // only if sync'ed, we check status of the sync.
    // I've noticed that most of the times >1 of these bits
    // will be set. however, i think there's a "most significant"
    // bit; it is the bit with the highest "deviation" from exact sync
    // that's been set that determines the actual sync state
    syncstat = 0;
    // if we have a pps, we assume (for a start) it's exactly synced)
    if( pps )
        syncstat = 1;
    // only if we have a pps + exact_sync set we move on to
    // next syncstatus [sync <=2 clock cycles]
    if( pps && *iob[mk5breg::DIM_EXACT_SYNC] )
        syncstat = 2;
    // finally, if we have a PPS and aperture sync is set,
    // were at >3 cycles orf!
    if( pps && *iob[mk5breg::DIM_APERTURE_SYNC] )
        syncstat = 3;

    // prepare the reply:
    reply << " 0 : "
          // time
          << y << "y" << doy << "d" << h << "h" << m << "m" << format("%07.4lf", s+frac) << "s : " 
          // current sync status
          << stattxt[syncstat] << " : "
          // FHG status? taken  from the "START_STOP" bit ...
          << ((fhg)?("FHG_on"):("FHG_off")) << " : "
          << os_now << " : "
          // delta( DOT, system-time )
          <<  format("%f", (double)delta) << " "
          << ";";
    return reply.str();
}

// struct to communicate between the trackmask_fn & the trackmask computing
// thread
struct computeargs_type {
    data_type     trackmask;
    // write solution in here
    runtime*      rteptr;

    computeargs_type() :
        trackmask( trackmask_empty ), rteptr( 0 )
    {}
};
void* computefun(void* p) {
    computeargs_type*  computeargs = (computeargs_type*)p;

    DEBUG(0, "computefun: start computing solution for " << hex_t(computeargs->trackmask) << endl);
    computeargs->rteptr->solution = solve(computeargs->trackmask);
    DEBUG(0, "computefun: done computing solution for " << hex_t(computeargs->trackmask) << endl);
    DEBUG(0, computeargs->rteptr->solution << endl);
    return (void*)0;
}

string trackmask_fn(bool q, const vector<string>& args, runtime& rte) {
    // computing the trackmask may take a considerable amount of time
    // so we do it in a thread. As long as the thread is computing we
    // report our status as "1" ("action initiated or enabled but not
    // completed" as per Mark5 A/B Commandset v 1.12)
    static per_runtime<pthread_t*>       runtime_computer;
    static per_runtime<computeargs_type> runtime_computeargs;

    if ( runtime_computer.find(&rte) == runtime_computer.end() ) {
        runtime_computer[&rte] = NULL;
    }
    pthread_t*& computer = runtime_computer[&rte];
    computeargs_type& computeargs = runtime_computeargs[&rte];

    // automatic variables
    char*           eocptr;
    const bool      busy( computer!=0 && ::pthread_kill(*computer, 0)==0 );
    ostringstream   reply;

    // before we do anything, update our bookkeeping.
    // if we're not busy (anymore) we should update ourselves to accept
    // further incoming commands.
    if( !busy ) {
        delete computer;
        computer = 0;
    }

    // now start forming the reply
    reply << "!" << args[0] << (q?('?'):('='));

    if( busy ) {
        reply << " 5 : still computing compressionsteps ;";
        return reply.str();
    }

    // good, check if query
    if( q ) {
        reply << " 0 : " << hex_t(computeargs.trackmask) << " : " << rte.signmagdistance << " ;";
        return reply.str();
    }
    // must be command then. we do not allow the command when doing a
    // transfer
    if( rte.transfermode!=no_transfer ) {
        reply << " 6 : cannot set trackmask whilst transfer in progress ;";
        return reply.str();
    }
    // we require at least the trackmask
    if( args.size()<2 || args[1].empty() ) {
        reply << " 8 : Command needs argument! ;";
        return reply.str();
    }
    computeargs.trackmask = ::strtoull(args[1].c_str(), &eocptr, 0);
    // !(A || B) => !A && !B
    ASSERT2_COND( !(computeargs.trackmask==0 && eocptr==args[1].c_str()) && !(computeargs.trackmask==~((uint64_t)0) && errno==ERANGE),
                  SCINFO("Failed to parse trackmask") );
                 
    // The sign magnitude distance is optional, default value 0
    // which means no magnitude restoration effort is made
    rte.signmagdistance = 0;
    if( args.size()>2 ) {
        ASSERT2_COND( ::sscanf(args[2].c_str(), "%d", &rte.signmagdistance) == 1,
                      SCINFO("Failed to parse sign-magnitude distance") );
    }

    // no tracks are dropped
    if( computeargs.trackmask==((uint64_t)0xffffffff << 32) + 0xffffffff ) 
        computeargs.trackmask=0;

    // Right - if no trackmask, clear it also from the runtime environment.
    // If yes trackmask, start a thread to compute the solution
    if( computeargs.trackmask ) {
        computer           = new pthread_t;
        computeargs.rteptr = &rte;

        // attempt to start the thread. if #fail then clean up
        PTHREAD2_CALL( ::pthread_create(computer, 0, computefun, &computeargs),
                       delete computer; computer = 0 );
        reply << " 1 : start computing compression steps ;";
    } else {
        rte.solution = solution_type();
        reply << " 0 : " << hex_t(computeargs.trackmask) << " : " << rte.signmagdistance << " ;";
    }
    return reply.str();
}


string version_fn(bool q, const vector<string>& args, runtime& ) {
    ostringstream   reply;

    reply << "!" << args[0]  << (q?"?":"=") << " ";

    // this is query only
    if( q ) 
        reply << " 0 : " << buildinfo() << " ;";
    else
        reply << " 2 : query only ;";
    return reply.str();
}



string bufsize_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream   reply;

    reply << "!" << args[0]  << (q?"?":"=") << " ";
    // this is query only
    if( q ) 
        reply << " 0 : " << rte.get_buffersize() << " ;";
    else
        reply << " 2 : query only ;";
    return reply.str();
}

// set the DOT at the next 1PPS [if one is set, that is]
// this function also performs the dot_inc command
string dot_set_fn(bool q, const vector<string>& args, runtime& rte) {
    // default DOT to set is "now()"
    pcint::timeval_type        now = pcint::timeval_type::now();
    ostringstream              reply;
    pcint::timeval_type        dot = now;

    // We must remember these across function calls since 
    // the user may query them later
    static int                 dot_inc;
    static pcint::timeval_type dot_set;

    // Already form this part of the reply
    reply << "!" << args[0] << (q?('?'):('='));

    // Mind you - IF we're already doing a transfer then we
    // should never evar be allowed to do this!
    if( !(q || rte.transfermode==no_transfer) ) {
        reply << " 6 : not whilst doing " << rte.transfermode << " ;";
        return reply.str();
    }

    // Handle dot_inc command/query
    if( args[0]=="dot_inc" ) {
        char*     eptr;
        string    incstr( OPTARG(1, args) );

        if( q ) {
            reply << " 0 : " << dot_inc << " ;";
            return reply.str();
        }
        // it's a command so it *must* have an argument
        if( incstr.empty() ) {
            reply << " 8 : command MUST have an argument ;";
            return reply.str();
        }
        // Verify the argument is sensible
        dot_inc = (int)::strtol(incstr.c_str(), &eptr, 10);
        if( eptr==incstr.c_str() || *eptr!='\0' ) {
            reply << " 8 : not an integer value argument ;";
            return reply.str();
        }
        // FIXME: is this an error "6" or "4"?
        if( !inc_dot(dot_inc) ) {
            reply << " 4 : DOT clock not running yet! ;";
            return reply.str();
        }
        reply << " 0 ;";
        return reply.str();
    }

    if( q ) {
        reply << " 0 : " << dot_set << " : * : " << format("%7.4lf", get_set_dot_delay()) << " ;";
        return reply.str();
    }
    // Ok must have been a command, then!
    bool                         force = false;
    string                       req_dot( OPTARG(1, args) );
    string                       force_opt( OPTARG(2, args) );
    ioboard_type&                iob( rte.ioboard );
    ioboard_type::mk5bregpointer sunkpps( iob[mk5breg::DIM_SUNKPPS] );

    // if force_opt is non-empty and not equal to "force", that is an 
    // error
    if( !force_opt.empty() ) {
        if( force_opt!="force" ) {
            reply << " 8 : invalid force-value ;";
            return reply.str();
        }
        force = true;
    }

    // this whole charade only makes sense if there is a 1PPS 
    // source selected
    if( (*iob[mk5breg::DIM_SELPP])==0 ) {
        reply << " 4 : cannot set DOT if no 1PPS source selected ;";
        return reply.str();
    }

    // if usr. passed a time, pick it up.
    // Supported format: VEX-like timestring
    //       0000y000d00h00m00.0000s
    //  with basically all fields being optional
    //  but with implicit order. Omitted fields are
    //  taken from the current systemtime.
    if( req_dot.size() ) {
        time_t             tt;
        struct ::tm        tms;
        unsigned int       microseconds = 0;

        // fill in current time as default
        ::time( &tt );
        ::gmtime_r( &tt, &tms );

        parse_vex_time( req_dot, tms, microseconds );

        if ( microseconds > 0 ) {
            reply << " 8 : DOT clock can only be set to an integer second value ;";
            return reply.str();
        }

        // now create the actual timevalue
        struct ::timeval   requested;

        // we only do integral seconds
        requested.tv_sec  = ::my_timegm( &tms );
        requested.tv_usec = 0;
        
        dot = pcint::timeval_type( requested );
        DEBUG(2, "dot_set: requested DOT at next 1PPS is-at " << dot << endl);
    } else {
        // Modify the DOT such that it represents the next integer second
        // (user didn't specify his own time so we take O/S time and work 
        //  from there)
        dot.timeValue.tv_sec++;
        dot.timeValue.tv_usec = 0;
    }

    // force==false && PPS already SUNK? 
    //  Do not resync the hardware but reset the binding of
    //  current OS time <=> DOT
    // Since 'dot' contains, by now, the actual DOT that we *want* to set
    // we can immediately bind local to DOT using the current time
    // (the time of entry into this routine):
    if( !force && *sunkpps ) {
        // If we fail to set the DOT it means the dot clock isn't running!
        if( !set_dot(dot) ) {
            reply << " 4 : DOT clock not running yet, use 1pps_source= and clock_set= first ;";
            return reply.str();
        }
        dot_set       = dot;
        reply << " 1 : dot_set initiated - will be executed at next 1PPS ;";
        return reply.str();
    }
    // So, we end up here because either force==true OR the card is not
    // synced yet. For the commandsequence that does not matter.

    // Now wait for 1PPS to happen [SUNKPPS becoming 1 after being reset].
    // If "force" we tell it to sync, otherwise we just clear the SUNKPPS.
    bool                 synced( false );
    pcint::timediff      dt;
    pcint::timeval_type  start;
    pcint::timeval_type  systime_at_1pps;

    // Pulse the "Reset PPS" bit
    iob[mk5breg::DIM_RESETPPS] = 1;
    iob[mk5breg::DIM_RESETPPS] = 0;

    // Id. for the syncpps bit - make sure it goes through a zero -> one
    // transition
    iob[mk5breg::DIM_SYNCPPS] = 0;
    iob[mk5breg::DIM_SYNCPPS] = 1;

    // wait at most 3 seconds for SUNKPPS to transition to '1'
    start = pcint::timeval_type::now();
    do {
        if( *sunkpps ) {
            // depending on wether user specified a time or not
            // we bind the requested time. no time given (ie empty
            // requested dot) means "use current systemtime"
            if( req_dot.empty() ) {
                // get current system time, compute next second
                // and make it a round second
                dot = pcint::timeval_type::now();
                dot.timeValue.tv_sec++;
                dot.timeValue.tv_usec = 0;
            }
            // Must be able to tell wether or not the
            // dot was set
            if( !set_dot(dot) )
                dot = pcint::timeval_type();
            synced = true;
            break;
        }
        // sleep for 0.1 ms
        ::usleep(100);
        // not sunk yet - busywait a bit
        dt = pcint::timeval_type::now() - start;
    } while( dt<3.0 );
    // now we can resume checking the flags
    iob[mk5breg::DIM_CLRPPSFLAGS] = 1;
    iob[mk5breg::DIM_CLRPPSFLAGS] = 0;

    // well ... ehm .. that's it then? we're sunked and
    // the systemtime <-> dot mapping's been set up.
    if( !synced ) {
        reply << " 4 : Failed to sync to selected 1PPS signal ;";
    } else {
        if( dot.timeValue.tv_sec==0 ) {
            reply << " 4 : DOT clock not running yet, use 1pps_source= and clock_set= first ;";
        } else {
            // ok, dot was set succesfully. remember it for later on ...
            dot_set       = dot;
            reply << " 1 : dot_set initiated - will be executed on next 1PPS ;";
        }
    }
    return reply.str();
}

// Set up the Mark5B/DIM input section to:
//   * sync to 1PPS (if a 1PPS source is set)
//   * set the time at the next 1PPS
//   * start generating on the next 1PPS
// Note: so *if* this function executes completely,
// it will start generating diskframes.
//
// This is done to ascertain the correct relation
// between DOT & data.
//
// dfhg = disk-frame-header-generator
//
// 'maxsyncwait' is the amount of time in seconds the system
// should at maximum wait for a 1PPS to appear.
// Note: if you said "1pps_source=none" then this method
// doesn't even try to wait for a 1pps, ok?
void start_mk5b_dfhg( runtime& rte, double maxsyncwait ) {
    const double    syncwait( maxsyncwait ); // Max. time to wait for 1PPS
    const double    minttns( 0.7 ); // minimum time to next second (in seconds)
    // (best be kept >0.0 and <1.0 ... )

    // Okie. Now it's time to start prgrm'ing the darn Mk5B/DIM
    // This is a shortcut: we rely on the Mk5's clock to be _quite_
    // accurate. We have to set the DataObservingTime at the next 1PPS
    // before we kick off the data-frame-header-generator.
    // Make sure we are not too close to the next integral second:
    // we need some processing time (computing JD, transcode to BCD
    // write into registers etc).
    int                         mjd;
    int                         tmjdnum; // truncated MJD number
    int                         nsssomjd;// number of seconds since start of mjd
    time_t                      tmpt;
    double                      ttns; // time-to-next-second, delta-t
    struct tm                   gmtnow;
    pcint::timeval_type         dot;
    mk5b_inputmode_type         curipm;
    mk5breg::regtype::base_type time_h, time_l;

    // Ere we start - see if the 1PPS is actwerly zynched!
    // That is to say: we get the current inputmode and see
    // if there is a 1PPS source selected. If the PPS source is 'None',
    // obviously, there's little point in trying to zynkronize!
    rte.get_input( curipm );

    // Trigger reset of all DIM statemachines. As per
    // the docs, this 'does not influence any settable
    // DIM parameter' (we hope)
    rte.ioboard[ mk5breg::DIM_RESET ] = 1;
    rte.ioboard[ mk5breg::DIM_RESET ] = 0;
    // selpps=0 => No PPS source
    if( curipm.selpps ) {
        double         dt;
        struct timeval start;
        struct timeval end;

        // Pulse SYNCPPS to trigger zynchronization attempt!
        rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 1;
        rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 0;

        // now wait [for some maximum amount of time]
        // for SUNKPPS to transition to '1'
        dt = 0.0;
        ::gettimeofday(&start, 0);
        do {
            if( *rte.ioboard[mk5breg::DIM_SUNKPPS] )
                break;
            // Ok, SUNKPPS not 1 yet.
            // sleep a bit and retry
            ::usleep(10);
            ::gettimeofday(&end, 0);
            dt = ((double)end.tv_sec + (double)end.tv_usec/1.0e6) -
                ((double)start.tv_sec + (double)start.tv_usec/1.0e6);
        } while( dt<syncwait );

        // If dt>=syncwait, this indicates we don't have a synched 1PPS signal?!
        ASSERT2_COND( dt<syncwait, SCINFO(" - 1PPS failed to sync"));
    }

    // As per Mark5B-DIM-Registers.pdf Sec. "Typical sequence of operations":
    rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 1;
    rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 0;

    // Great. Now wait until we reach a time which is sufficiently before 
    // the next integral second of DOT!
    do {
        // wait 1 millisecond (on non-RT kernels this is probably more like
        // 10ms)
        ::usleep(100);
        dot = get_dot().dot;
        // compute time-to-next-(integral) DOT second
        ttns = 1.0 - (double)(dot.timeValue.tv_usec/1.0e6);
    } while( ttns<minttns );

    // Good. Now be quick about it.
    // We know what the DOT will be (...) at the next 1PPS.
    // From the wait loop above we have our latest estimate of the
    // actual DOT.
    // Add 1 second, transform
    // Transform localtime into GMT, get the MJD of that,
    // transform that to "VLBA-JD" (MJD % 1000) and finally
    // transform *that* into B(inary)C(oded)D(ecimal) and
    // write it into the DIM
    // Note: do NOT forget to increment the tv_sec value 
    // because we need the next second, not the one we're in ;)
    // and set the tv_usec value to '0' since ... well .. it
    // will be the time at the next 1PPS ...
    tmpt = (time_t)(dot.timeValue.tv_sec + 1);
    ::gmtime_r( &tmpt, &gmtnow );

    // Get the MJD daynumber
    mjd = ::jdboy( gmtnow.tm_year+1900 ) + gmtnow.tm_yday;
    tmjdnum  = (mjd % 1000);
    nsssomjd = gmtnow.tm_hour * 3600 + gmtnow.tm_min*60 + gmtnow.tm_sec;
    DEBUG(2, "Got mjd for next 1PPS: " << mjd << " => TMJD=" << tmjdnum << ". Number of seconds: " << nsssomjd << endl);

    // Now we must go to binary coded decimal
    unsigned int t1, t2;
    bcd(tmjdnum, t1);
    bcd(nsssomjd, t2);

    // Transfer to the correct place in the start_time
    // JJJS   SSSS
    // time_h time_l
    time_h  = (mk5breg::regtype::base_type)(((t1 & 0xfff)) << 4);

    // Get the 5th bcd digit of the 'seconds-since-start-of-mjd'
    // and move it into the lowest bcd of the high-word of START_TIME
    time_h = (mk5breg::regtype::base_type)(time_h | ((t2 >> 16)&0xf));

    // the four lesser most-significant bcd digits of the 
    // 'seconds-since-start etc' go into the lo-word of START_TIME
    time_l  = (mk5breg::regtype::base_type)(t2 & 0xffff);
    DEBUG(2, "Writing BCD StartTime H:" << hex_t(time_h) << " L:" << hex_t(time_l) << endl);

    // Fine. Bung it into the DIM
    rte.ioboard[ mk5breg::DIM_STARTTIME_H ] = time_h;
    rte.ioboard[ mk5breg::DIM_STARTTIME_L ] = time_l;

    // Now we issue a SETUP, wait for at least '135 data-clock-cycles'
    // before releasing it. We'll approximate this by just sleeping
    // 10ms.
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 1;
    ::usleep( 10000 );
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 0;

    // Weehee! Start the darn thing on the next PPS!
    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 1;

    return;
}

string disk_info_fn(bool, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    XLRCODE(SSHANDLE ss = rte.xlrdev.sshandle());
    S_DRIVEINFO   drive_info;
    static const unsigned int max_string_size = max(XLR_MAX_DRIVESERIAL, XLR_MAX_DRIVENAME) + 1;
    vector<char>  serial_mem(max_string_size);
    char*         serial = &serial_mem[0];
    
    serial[max_string_size - 1] = '\0'; // make sure all serials are null terminated

    reply << "!" << args[0] << "? 0";

    vector<unsigned int> master_slave;
    master_slave.push_back(XLR_MASTER_DRIVE);
    master_slave.push_back(XLR_SLAVE_DRIVE);
    
    for (unsigned int bus = 0; bus < rte.xlrdev.devInfo().NumBuses; bus++) {
        for (vector<unsigned int>::const_iterator ms = master_slave.begin();
             ms != master_slave.end();
             ms++) {
            try {
                XLRCALL( ::XLRGetDriveInfo( ss, bus, *ms, &drive_info ) );
                if ( args[0] == "disk_serial" )  {
                    strncpy( serial, drive_info.Serial, max_string_size );
                    reply << " : " << strip(serial);
                }
                else if ( args[0] == "disk_model" ) {
                    strncpy( serial, drive_info.Model, max_string_size );
                    reply << " : " << strip(serial);
                }
                else { // disk_size
                    reply << " : " << ((uint64_t)drive_info.Capacity * 512ull); 
                }
            }
            catch ( ... ) {
                reply << " : ";
            }
        }
    }
    reply << " ;";

    return reply.str();
}

string position_fn(bool q, const vector<string>& args, runtime& rte) {
    // will return depending on actual query:
    // pointers: <record pointer> : <scan start> : <scan end> ; scan start and end are filled with "-" for now
    // position: <record pointer> : <play pointer>
    ostringstream              reply;

    reply << "!" << args[0] << (q?("? "):("= ")) << "0 : " << ::XLRGetLength(rte.xlrdev.sshandle()) << " : ";

    if (args[0] == "position") {
        reply << rte.pp_current.Addr + (rte.transfermode == disk2out ? ::XLRGetPlayLength(rte.xlrdev.sshandle()) : 0);
    }
    else if (args[0] == "pointers") {
        reply << rte.pp_current << " : " << rte.pp_end;
    }
    else {
        THROW_EZEXCEPT(cmdexception, "query '" + args[0] + "' not recognized in position_fn");
    }
    reply << " ;";

    return reply.str();
}

string os_rev_fn(bool q, const vector<string>& args, runtime&) {
    ostringstream              reply;

    reply << "!" << args[0] << (q?('?'):('='));

    string line;
    ifstream version_file ("/proc/version");
    if (version_file.is_open()) {
        getline (version_file,line);
        reply << " 0 : " << line << " ;";
        version_file.close();
    }
    else {
        reply << " 4 : failed to open /proc/version ;";
    }

    return reply.str();
}

string start_stats_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream              reply;

    reply << "!" << args[0] << (q?('?'):('='));

    // units are in 15ns

    if ( q ) {
        reply << " 0";
        vector< ULONG > ds = rte.xlrdev.get_drive_stats( );
        for ( unsigned int i = 0; i < ds.size(); i++ ) {
            reply << " : " << (ds[i] / 1e9 * 15) << "s";
        }
        reply << " ;";
        return reply.str();
    }

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot set statistics during transfers ;";
        return reply.str();
    }

    vector<ULONG> to_use;
    if ( args.size() == xlrdevice::drive_stats_length + 1 ) {
        char* eocptr;
        double parsed;
        for ( unsigned int i = 0; i < xlrdevice::drive_stats_length; i++ ) {
            parsed = ::strtod(args[i+1].c_str(), &eocptr);
            ASSERT2_COND( !(fabs(parsed) <= 0 && eocptr==args[i+1].c_str()) && (*eocptr=='\0' || *eocptr=='s') && !((parsed>=HUGE_VAL || parsed<=-HUGE_VAL) && errno==ERANGE),
                          SCINFO("Failed to parse a time from '" << args[i+1] << "'") );
            to_use.push_back( (ULONG)round(parsed * 1e9 / 15) );
        }
    }
    else if ( !((args.size() == 1) || ((args.size() == 2) && args[1].empty())) ) { // an empty string is parsed as an argument, so check that
        reply << " 8 : " << args[0] << " requires 0 or " << xlrdevice::drive_stats_length << " arguments, " << (args.size() - 1) << " given ;";
        return reply.str();
    }
    
    rte.xlrdev.set_drive_stats( to_use );

    reply << " 0 ;";

    return reply.str();
}

string get_stats_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    XLRCODE(SSHANDLE      ss = rte.xlrdev.sshandle());
    S_DRIVESTATS  stats[XLR_MAXBINS];
    static per_runtime<unsigned int> current_drive_number;
    
    reply << "!" << args[0] << (q?('?'):('='));

    if (rte.transfermode != no_transfer) {
        reply << " 6 : cannot retrieve statistics during transfers ;";
        return reply.str();
    }

    reply << " 0";
    
    unsigned int drive_to_use = current_drive_number[&rte];
    if (drive_to_use + 1 >= 2 * rte.xlrdev.devInfo().NumBuses) {
        current_drive_number[&rte] = 0;
    }
    else {
        current_drive_number[&rte] = drive_to_use + 1;
    }
    
    reply << " : " << drive_to_use;
    XLRCODE( unsigned int bus = drive_to_use/2 );
    XLRCODE( unsigned int master_slave = (drive_to_use % 2 ? XLR_SLAVE_DRIVE : XLR_MASTER_DRIVE) );
    XLRCALL( ::XLRGetDriveStats( ss, bus, master_slave, stats ) );
    for (unsigned int i = 0; i < XLR_MAXBINS; i++) {
        reply << " : " << stats[i].count;
    }
    reply << " : " << XLRCODE( ::XLRDiskRepBlkCount( ss, bus, master_slave ) << ) " ;";

    return reply.str();
}

string replaced_blks_fn(bool q, const vector<string>& args, runtime& XLRCODE(rte) ) {
    ostringstream reply;
    XLRCODE(SSHANDLE ss = rte.xlrdev.sshandle());
    
    reply << "!" << args[0] << (q?('?'):('='));

    reply << " 0";
    for ( unsigned int disk = 0; disk < 8; disk++) {
        XLRCODE(unsigned int bus = disk/2);
        XLRCODE(unsigned int master_slave = disk % 2 ? XLR_SLAVE_DRIVE : XLR_MASTER_DRIVE);
        XLRCODE(reply << " : " << ::XLRDiskRepBlkCount( ss, bus, master_slave ) ); 
    }
    do_xlr_lock();
    XLRCODE(reply << " : " << ::XLRTotalRepBlkCount( ss ) << " ;");
    do_xlr_unlock();

    return reply.str();
}

string vsn_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    char          label[XLR_LABEL_LENGTH + 1];
    XLRCODE(SSHANDLE ss = rte.xlrdev.sshandle());

    label[XLR_LABEL_LENGTH] = '\0';

    reply << "!" << args[0] << (q?('?'):('=')) ;

    XLRCALL( ::XLRGetLabel( ss, label) );
    
    pair<string, string> vsn_state = disk_states::split_vsn_state(string(label));
    if ( q ) {
        reply << " 0 : " << vsn_state.first;

        // check for matching serial numbers
        vector<ORIGINAL_S_DRIVEINFO> cached;
        try {
            cached = rte.xlrdev.getStoredDriveInfo();
        }
        catch ( ... ) {
            reply << " : Unknown ;";
            return reply.str();
        }
        
        S_DRIVEINFO drive_info;
        for ( size_t i = 0; i < cached.size(); i++) {
            UINT bus = i / 2;
            UINT master_slave = (i % 2 ? XLR_SLAVE_DRIVE : XLR_MASTER_DRIVE);
            string serial;
            try {
                XLRCALL( ::XLRGetDriveInfo( ss, bus, master_slave, &drive_info ) );
                serial = drive_info.Serial;
            }
            catch ( ... ) {
                // use empty string
            }
            if ( serial != cached[i].Serial ) {
                reply << " : Fail : " << (bus * 2 + master_slave) << " : " << cached[i].Serial << " : " << serial << " : Disk serial number mismatch ;";
                return reply.str();
            }
        }

        reply << " : OK ;";
        return reply.str();
    }
    // must be command
    
    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot write VSN while doing " << rte.transfermode << " ;";
        return reply.str();
    }

    if ( rte.protected_count == 0 ) {
        reply << " 6 : need an immediate preceding protect=off ;";
        return reply.str();
    }
    // ok, we're allowed to write VSN

    if ( args.size() < 2 ) {
        reply << " 8 : commands requires an argument ;";
        return reply.str();
    }

    // check VSN format rules: 
    // (1) total length = 8
    // (2) [A-Z]{2,6}(+|-)[0-9]^{1,5}
    if ( args[1].size() != 8 ) {
        reply << " 8 : VSN must be 8 characters long ;";
        return reply.str();
    }
    
    string regex_format = "[A-Za-z]\\{2,6\\}\\(-\\|\\+\\)[0-9]\\{1,5\\}";
    regex_t regex;
    int regex_error;
    char regex_error_buffer[1024];
    ASSERT2_COND( (regex_error = ::regcomp(&regex, regex_format.c_str(), 0)) == 0, ::regerror(regex_error, &regex, &regex_error_buffer[0], sizeof(regex_error_buffer)); SCINFO( "regex compilation returned error: '" << regex_error_buffer << "'") );
    regex_error = ::regexec(&regex, args[1].c_str(), 0, NULL, 0);
    if ( regex_error != 0 ) {
        ::regerror(regex_error, &regex, &regex_error_buffer[0], sizeof(regex_error_buffer));
        reply << " 8 : " << args[1] << " does not match the regular expression [A-Z]{2,6}(+|-)[0-9]{1,5} ;";
        return reply.str();
    }
    
    // compute the capacity and maximum data rate, to be appended to vsn
    S_DEVINFO     dev_info;
    S_DRIVEINFO   drive_info;

    XLRCALL( ::XLRGetDeviceInfo( ss, &dev_info ) );

    vector<unsigned int> master_slave;
    master_slave.push_back(XLR_MASTER_DRIVE);
    master_slave.push_back(XLR_SLAVE_DRIVE);
    
    unsigned int number_of_disks = 0;
    UINT minimum_capacity = std::numeric_limits<UINT>::max(); // in 512 bytes
    for (unsigned int bus = 0; bus < dev_info.NumBuses; bus++) {
        for (vector<unsigned int>::const_iterator ms = master_slave.begin();
             ms != master_slave.end();
             ms++) {
            try {
                XLRCALL( ::XLRGetDriveInfo( ss, bus, *ms, &drive_info ) );
                number_of_disks++;
                if ( drive_info.Capacity < minimum_capacity ) {
                    minimum_capacity = drive_info.Capacity;
                }
            }
            catch ( ... ) {
                DEBUG( -1, "Failed to get drive info for drive " << bus << (*ms==XLR_MASTER_DRIVE?" master":" slave") << endl);
            }
        }
    }

    // construct the extended vsn, which is
    // (1) the given label
    // (2) "/" Capacity, computed as the minumum capacity over all disk rounded down to a multiple of 10GB times the number of disks
    // (3) "/" Maximum rate, computed as 128Mbps times number of disks
    // (4) '\036' (record separator) disk state
    ostringstream extended_vsn;
    const uint64_t capacity_round = 10000000000ull; // 10GB
    extended_vsn << toupper(args[1]) << "/" << (((uint64_t)minimum_capacity * 512ull)/capacity_round*capacity_round * number_of_disks / 1000000000) << "/" << (number_of_disks * 128) << '\036' << vsn_state.second;

    rte.xlrdev.write_vsn( extended_vsn.str() );

    reply << " 0 ;";
    return reply.str();
}

struct XLR_Buffer {
    READTYPE* data;
    XLR_Buffer( uint64_t len ) {
        data = new READTYPE[(len + sizeof(READTYPE) - 1) / sizeof(READTYPE)];
    }
    
    ~XLR_Buffer() {
        delete[] data;
    }
};

string data_check_5a_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot do a data check while " << rte.transfermode << " is in progress ;";
        return reply.str();
    }

    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));

    XLRCODE(
    S_READDESC readdesc;
    readdesc.XferLength = bytes_to_read;
    readdesc.AddrHi     = rte.pp_current.AddrHi;
    readdesc.AddrLo     = rte.pp_current.AddrLo;
    readdesc.BufferAddr = buffer->data;
            );

    // make sure SS is ready for reading
    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

    data_check_type found_data_type;

    // static variables to be able to compute "missing bytes"
    static data_check_type prev_data_type;
    static playpointer prev_play_pointer;

    unsigned int first_valid;
    unsigned int first_invalid;
        
    // use track 4 for now
    unsigned int track = 4;
    if ( args[0] == "track_check" ) {
        track = *rte.ioboard[ mk5areg::ChASelect ];
    }
    if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, track, found_data_type) ) {
        struct tm time_struct;
        headersearch_type header_format(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);

        ::gmtime_r( &found_data_type.time.tv_sec, &time_struct );

        // mode and submode
        if (found_data_type.format == fmt_mark4_st) {
            reply << " 0 : st : mark4 : ";
        }
        else if (found_data_type.format == fmt_vlba_st) {
            reply << " 0 : st : vlba : ";
        }
        else {
            reply << " 0 : " << found_data_type.format << " : " << found_data_type.ntrack << " : ";
        }

        double track_frame_period = (double)header_format.payloadsize * 8 / (double)(header_format.trackbitrate * header_format.ntrack);
        double time_diff = (found_data_type.time.tv_sec - prev_data_type.time.tv_sec) + 
            (found_data_type.time.tv_nsec - prev_data_type.time.tv_nsec) / 1000000000.0;
        int64_t expected_bytes_diff = (int64_t)round(time_diff * header_format.framesize / track_frame_period);
        int64_t missing_bytes = (int64_t)(rte.pp_current - prev_play_pointer) + ((int64_t)found_data_type.byte_offset - (int64_t)prev_data_type.byte_offset) - expected_bytes_diff;
 
        reply <<  tm2vex(time_struct, found_data_type.time.tv_nsec) << " : ";
        reply <<  found_data_type.byte_offset << " : " << track_frame_period << "s : ";
        if ( args[0] == "track_check" ) {
            // track data rate, in MHz (well, that's what the doc says, not sure how a data rate can be in Hz)
            reply << round(header_format.payloadsize * 8 / track_frame_period / header_format.ntrack)/1e6 << " : ";
            reply << register2track(track);
        }
        else {
            reply << header_format.framesize;
        }
        reply  << " : " << missing_bytes << " ;";

        prev_data_type = found_data_type;
        prev_play_pointer = rte.pp_current;
    }
    else if ( is_mark5a_tvg( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << " 0 : tvg : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
    }
    else if ( is_ss_test_pattern( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << " 0 : SS : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
    }
    else {
        reply << " 0 : ? ;";
    }

    return reply.str();
}

string data_check_dim_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot do a data check while " << rte.transfermode << " is in progress ;";
        return reply.str();
    }

    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));

    XLRCODE(
    S_READDESC readdesc;
    readdesc.XferLength = bytes_to_read;
    readdesc.AddrHi     = rte.pp_current.AddrHi;
    readdesc.AddrLo     = rte.pp_current.AddrLo;
    readdesc.BufferAddr = buffer->data;
            );

    // make sure SS is ready for reading
    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
    // read the piece of data
    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

    data_check_type found_data_type;

    // static variables to be able to compute "missing bytes"
    static data_check_type prev_data_type;
    static playpointer prev_play_pointer;

    // use track 4 for now
    if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, found_data_type) && (found_data_type.format == fmt_mark5b) ) {
        struct tm time_struct;
        headersearch_type header_format(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);
        const m5b_header& header_data = *(const m5b_header*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset]);
        const mk5b_ts& header_ts = *(const mk5b_ts*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset + 8]);

        reply << " 0 : ";
        if (header_data.tvg) {
            reply << "tvg : ";
        }
        else {
            reply << "ext : ";
        }

        ::gmtime_r( &found_data_type.time.tv_sec, &time_struct );

        double frame_period = (double)header_format.payloadsize * 8 / (double)(header_format.trackbitrate * header_format.ntrack);
        double data_rate_mbps = header_format.framesize * 8 / (frame_period * 1001600); // take out the header overhead
        double time_diff = (found_data_type.time.tv_sec - prev_data_type.time.tv_sec) + 
            (found_data_type.time.tv_nsec - prev_data_type.time.tv_nsec) / 1000000000.0;
        int64_t expected_bytes_diff = (int64_t)round(time_diff * header_format.framesize / frame_period);
        int64_t missing_bytes = (int64_t)(rte.pp_current - prev_play_pointer) + ((int64_t)found_data_type.byte_offset - (int64_t)prev_data_type.byte_offset) - expected_bytes_diff;
 
        reply <<  tm2vex(time_struct, found_data_type.time.tv_nsec) << " : " << (int)header_ts.J2 << (int)header_ts.J1 << (int)header_ts.J0 << " : " << header_data.frameno << " : " << frame_period << "s : " << data_rate_mbps << " : " << found_data_type.byte_offset << " : " << missing_bytes << " ;";

        prev_data_type = found_data_type;
        prev_play_pointer = rte.pp_current;
    }
    else {
        reply << " 0 : ? ;";
    }

    return reply.str();
}

string scan_check_5a_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot do a scan check while " << rte.transfermode << " is in progress ;";
        return reply.str();
    }

    if ( rte.current_scan >= rte.xlrdev.nScans() ) {
        reply << " 6 : current scan (#" << (rte.current_scan + 1) << ") not within bounds of number of recorded scans (" << rte.xlrdev.nScans() << ") ;";
        return reply.str();
    }

    int64_t length = rte.pp_end - rte.pp_current;
    if ( length < 0 ) {
        reply << " 6 : scan start pointer is set beyond scan end pointer ;";
        return reply.str();
    }

    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
    if ( length < bytes_to_read ) {
      reply << " 6 : scan too short to check ;";
      return reply.str();
    }
    
    ROScanPointer scan_pointer(rte.xlrdev.getScan(rte.current_scan));

    reply << " 0 : " << (rte.current_scan + 1) << " : " << scan_pointer.name() << " : ";

    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));
    playpointer read_pointer( rte.pp_current );

    XLRCODE(
    S_READDESC readdesc;
    readdesc.XferLength = bytes_to_read;
    readdesc.AddrHi     = read_pointer.AddrHi;
    readdesc.AddrLo     = read_pointer.AddrLo;
    readdesc.BufferAddr = buffer->data;
            );

    // make sure SS is ready for reading
    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
    // read the data
    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

    data_check_type found_data_type;

    unsigned int first_valid;
    unsigned int first_invalid;
    // use track 4 for now
    if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, found_data_type) ) {
        // found something at start of the scan, check for the same format at the end
        read_pointer += ( length - bytes_to_read );
        XLRCODE(
        readdesc.AddrHi     = read_pointer.AddrHi;
        readdesc.AddrLo     = read_pointer.AddrLo;
        readdesc.BufferAddr = buffer->data;
                );
        XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );
        
        data_check_type end_data_type;
        if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, end_data_type) ) {
            struct tm time_struct;
            headersearch_type header_format(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);

            ::gmtime_r( &found_data_type.time.tv_sec, &time_struct );

            if (found_data_type.format == fmt_mark4_st) {
                reply << "st : mark4 : ";
            }
            else if (found_data_type.format == fmt_vlba_st) {
                reply << "st : vlba : ";
            }
            else {
                reply << found_data_type.format << " : " << found_data_type.ntrack << " : ";
            }

            double track_frame_period = (double)header_format.payloadsize * 8 / (double)(header_format.trackbitrate * header_format.ntrack);
            double time_diff = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
              (end_data_type.time.tv_nsec - found_data_type.time.tv_nsec) / 1000000000.0;
            int64_t expected_bytes_diff = (int64_t)round(time_diff * header_format.framesize / track_frame_period);
            int64_t missing_bytes = (int64_t)length - bytes_to_read - (int64_t)found_data_type.byte_offset + (int64_t)end_data_type.byte_offset - expected_bytes_diff;

            // start time 
            reply <<  tm2vex(time_struct, found_data_type.time.tv_nsec) << " : ";
            // scan length (seconds)
            double scan_length = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
                ((int)end_data_type.time.tv_nsec - (int)found_data_type.time.tv_nsec) / 1e9 + 
                (bytes_to_read - end_data_type.byte_offset) / (header_format.framesize / track_frame_period);// assume the bytes to the end have valid data
            reply << scan_length << "s : ";
            reply << (header_format.trackbitrate / 1e6) << "Mbps : ";
            reply << (-missing_bytes) << " ;";
            return reply.str();
        }

    }
    else if ( is_mark5a_tvg( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << "tvg : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
        return reply.str();
    }
    else if ( is_ss_test_pattern( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << "SS : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
        return reply.str();
    }
    reply << "? ;";

    return reply.str();
}

string scan_check_dim_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot do a data check while " << rte.transfermode << " is in progress ;";
        return reply.str();
    }

    if ( rte.current_scan >= rte.xlrdev.nScans() ) {
        reply << " 6 : current scan (#" << (rte.current_scan + 1) << ") not within bounds of number of recorded scans (" << rte.xlrdev.nScans() << ") ;";
        return reply.str();
    }

    int64_t length = rte.pp_end - rte.pp_current;
    if ( length < 0 ) {
        reply << " 6 : scan start pointer is set beyond scan end pointer ;";
        return reply.str();
    }
    
    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
    if ( length < bytes_to_read ) {
      reply << " 6 : scan too short to check ;";
      return reply.str();
    }
    

    ROScanPointer scan_pointer(rte.xlrdev.getScan(rte.current_scan));

    reply << " 0 : " << (rte.current_scan + 1) << " : " << scan_pointer.name() << " : ";

    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));
    playpointer read_pointer( rte.pp_current );
    
    XLRCODE(
    S_READDESC readdesc;
    readdesc.XferLength = bytes_to_read;
    readdesc.AddrHi     = read_pointer.AddrHi;
    readdesc.AddrLo     = read_pointer.AddrLo;
    readdesc.BufferAddr = buffer->data;
            );
    // make sure SS is ready for reading
    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
    // read the data
    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

    data_check_type found_data_type;

    unsigned int first_valid;
    unsigned int first_invalid;
    // use track 4 for now
    if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, found_data_type) && (found_data_type.format == fmt_mark5b) ) {
        // get tvg and date code from data before we re-use it
        const m5b_header& header_data = *(const m5b_header*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset]);
        const mk5b_ts& header_ts = *(const mk5b_ts*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset + 8]);

        bool tvg = header_data.tvg;
        ostringstream date_code;
        date_code << (int)header_ts.J2 << (int)header_ts.J1 << (int)header_ts.J0;

        // found something at start of the scan, check for the same format at the end
        read_pointer += ( length - bytes_to_read );
        XLRCODE(
        readdesc.AddrHi     = read_pointer.AddrHi;
        readdesc.AddrLo     = read_pointer.AddrLo;
        readdesc.BufferAddr = buffer->data;
                );
        XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );
        
        data_check_type end_data_type;
        if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, end_data_type) && (found_data_type.format == fmt_mark5b) ) {
            struct tm time_struct;
            headersearch_type header_format(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);
            const m5b_header& end_header_data = *(const m5b_header*)(&((unsigned char*)buffer->data)[end_data_type.byte_offset]);

            if (tvg == (bool)end_header_data.tvg) {

                ::gmtime_r( &found_data_type.time.tv_sec, &time_struct );

                if ( tvg ) {
                    reply << "tvg : ";
                }
                else {
                    reply << "- : ";
                }
                
                double track_frame_period = (double)header_format.payloadsize * 8 / (double)(header_format.trackbitrate * header_format.ntrack);
                double time_diff = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
                    (end_data_type.time.tv_nsec - found_data_type.time.tv_nsec) / 1000000000.0;
                int64_t expected_bytes_diff = (int64_t)round(time_diff * header_format.framesize / track_frame_period);
                int64_t missing_bytes = (int64_t)length - bytes_to_read - (int64_t)found_data_type.byte_offset + (int64_t)end_data_type.byte_offset - expected_bytes_diff;
                
                reply << date_code.str() << " : ";
                // start time
                reply <<  tm2vex(time_struct, found_data_type.time.tv_nsec) << " : ";
                // scan length
                double scan_length = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
                    ((int)end_data_type.time.tv_nsec - (int)found_data_type.time.tv_nsec) / 1e9 + 
                    (bytes_to_read - end_data_type.byte_offset) / (header_format.framesize / track_frame_period);// assume the bytes to the end have valid data
                
                scan_length = round(scan_length / track_frame_period) * track_frame_period; // round it to the nearest possible value

                reply << scan_length << "s : ";
                // total recording rate
                reply << (header_format.trackbitrate * header_format.ntrack / 1e6) << "Mbps : ";
                reply << (-missing_bytes) << " ;";
                return reply.str();
            }
        }
            
    }
    else if ( is_ss_test_pattern( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << "SS : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
        return reply.str();
    }
    reply << "? ;";

    return reply.str();
}



string scan_set_fn(bool q, const vector<string>& args, runtime& rte) {
    // note that we store current_scan zero based, 
    // but user communication is one based
    ostringstream              reply;
    static per_runtime<string> previous_search_string;

    reply << "!" << args[0] << (q?('?'):('='));

    const unsigned int nScans = rte.xlrdev.nScans();

    if ( q ) {
        
        reply << " 0 : " << (rte.current_scan + 1) << " : ";
        if ( rte.current_scan < nScans ) {
            ROScanPointer scan = rte.xlrdev.getScan( rte.current_scan );
            reply << scan.name() << " : " << rte.pp_current << " : " << rte.pp_end << " ;";
            
        }
        else {
            reply << " scan is out of range of current disk (" << nScans << ") ;";
        }
        
        return reply.str();
    }

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot set scan during " << rte.transfermode << " ;";
        return reply.str();
    }

    if ( nScans == 0 ) {
        reply << " 6 : disk does not have any scans present ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        rte.setCurrentScan( nScans - (rte.xlrdev.isScanRecording() ? 2 : 1) );
        return reply.str();
    }

    // first argument is a scan number, search string, "inc", "dec" or "next"

    if ( args[1].empty() ) {
        rte.setCurrentScan( nScans - (rte.xlrdev.isScanRecording() ? 2 : 1) );
    }
    else if ( args[1] == "inc" ) {
        unsigned int scan = rte.current_scan + 1;
        if ( scan >= nScans ) {
            scan = 0;
        }
        rte.setCurrentScan( scan );
    }
    else if ( args[1] == "dec" ) {
        int scan = (int)rte.current_scan - 1;
        if ( scan < 0 ) {
            scan = nScans - 1;
        }
        rte.setCurrentScan( scan );
    }
    else {
        char* endptr;
        long int parsed = strtol(args[1].c_str(), &endptr, 10);

        if ( (*endptr != '\0') || (parsed < 1) || (parsed > (long int)nScans) ) {
            // try to interpret it as a search string
            string search_string = args[1];
            unsigned int next_scan = 0;
            if ( args[1] == "next" ) {
                if ( previous_search_string.find(&rte) == previous_search_string.end() ) {
                    reply << " 4 : no search string given yet ;";
                    return reply.str();
                }
                search_string = previous_search_string[&rte];
                next_scan = rte.current_scan + 1;
            }
            unsigned int end_scan = next_scan + nScans;
            previous_search_string[&rte] = search_string;
            // search string is case insensitive, convert both to uppercase
            search_string = toupper(search_string);
            
            while ( (next_scan != end_scan) && 
                    (toupper(rte.xlrdev.getScan(next_scan % nScans).name()).find(search_string) == string::npos)
                    ) {
                next_scan++;
            }
            if ( next_scan == end_scan ) {
                reply << " 8 : failed to find scan matching '" << search_string << "' ;";
                return reply.str();
            }
            rte.setCurrentScan( next_scan % nScans );
        }
        else {
            rte.setCurrentScan( (unsigned int)parsed - 1 );
        }
    }

    // two optional argument can shift the scan start and end pointers
    
    // as the offset might be given in time, we might need the data format 
    // to compute the data rate, do that data check only once
    data_check_type found_data_type;
    bool data_checked = false;

    for ( unsigned int argument_position = 2; argument_position < min((size_t)4, args.size()); argument_position++ ) {
        int64_t byte_offset;
        if ( args[argument_position].empty() ) {
            if ( argument_position == 2 ) {
                // start default is start of scan
                byte_offset = 0;
            }
            else {
                // stop default is end of scan
                byte_offset = rte.xlrdev.getScan( rte.current_scan ).length();
            }
        }
        // for the start byte offset, we have the option to set it to 
        // s (start), c (center), e (end) and s+ (a bit past start)
        else if ( (argument_position == 2) && (args[2] == "s") ) {
            byte_offset = 0;
        }
        else if ( (argument_position == 2) && (args[2] == "c") ) {
            byte_offset = rte.xlrdev.getScan( rte.current_scan ).length() / 2;
        }
        else if ( (argument_position == 2) && (args[2] == "e") ) {
            // as per documentation, ~1M before end
            byte_offset = rte.xlrdev.getScan( rte.current_scan ).length() - 1000000;

        }
        else if ( (argument_position == 2) && (args[2] == "s+") ) {
            byte_offset = 65536;
        }
        else {
            // first try to interpret it as a time
            bool is_time = true;
            bool relative_time;
            struct ::tm parsed_time;
            unsigned int microseconds;
            try {
                // default it to "zero"
                parsed_time.tm_year = 0;
                parsed_time.tm_mon = 0;
                parsed_time.tm_mday = 1;
                parsed_time.tm_hour = 0;
                parsed_time.tm_min = 0;
                parsed_time.tm_sec = 0;
                microseconds = 0;

                // time might be prefixed with a '+' or '-', dont try to parse that
                relative_time = ( (args[argument_position][0] == '+') ||
                                  (args[argument_position][0] == '-') );
                
                ASSERT_COND( parse_vex_time(args[argument_position].substr(relative_time ? 1 : 0), parsed_time, microseconds) > 0 );
            }
            catch ( ... ) {
                is_time = false;
            }

            if ( is_time ) {
                // we need a data format to compute a byte offset from the time offset 
                if ( !data_checked ) {
                    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
                    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));

                    XLRCODE(
                    S_READDESC readdesc;
                    readdesc.XferLength = bytes_to_read;
                            );
                    playpointer pp( rte.xlrdev.getScan( rte.current_scan ).start() );
                    
                    XLRCODE(
                    readdesc.AddrHi     = pp.AddrHi;
                    readdesc.AddrLo     = pp.AddrLo;
                    readdesc.BufferAddr = buffer->data;
                            );
                
                    // make sure SS is ready for reading
                    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
                    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
                    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
                    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

                    const unsigned int track = 4; // have to pick one
                    if ( !find_data_format( (unsigned char*)buffer->data, bytes_to_read, track, found_data_type) ) {
                        reply << " 4 : failed to find data format needed to compute byte offset in scan ;";
                        return reply.str();
                    }
                }
                data_checked = true;
                headersearch_type headersearch(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);
                // data rate in B/s, taking into account header overhead
                const uint64_t data_rate = 
                    (uint64_t)headersearch.ntrack * 
                    (uint64_t)headersearch.trackbitrate *
                    (uint64_t)headersearch.framesize /
                    (uint64_t)headersearch.payloadsize / 
                    8;
                
                if ( relative_time ) {
                    // the year (if given) is ignored
                    unsigned int seconds = seconds_in_year( parsed_time );
                    byte_offset = (int64_t)round( (seconds + microseconds / 1e6) * data_rate );
                    if ( args[argument_position][0] == '-' ) {
                        byte_offset = -byte_offset;
                    }
                }
                else {
                    // re-run the parsing, but now default it to the data time
                    ASSERT_COND( gmtime_r(&found_data_type.time.tv_sec, &parsed_time ) );
                    microseconds = (unsigned int)round(found_data_type.time.tv_nsec / 1e3);
                    
                    unsigned int fields = parse_vex_time(args[argument_position], parsed_time, microseconds);
                    ASSERT_COND( fields > 0 );

                    time_t requested_time = ::mktime( &parsed_time );
                    ASSERT_COND( requested_time != (time_t)-1 );

                    // check if the requested time if before or after the data time
                    if ( (requested_time < found_data_type.time.tv_sec) || 
                         ((requested_time == found_data_type.time.tv_sec) && (((long)microseconds * 1000) < found_data_type.time.tv_nsec)) ) {
                        // we need to be at the next "mark"
                        // this means increasing the first field 
                        // that was not defined by one

                        // second, minute, hour, day, year 
                        // (we take the same shortcut for years as in Mark5A/dimino)
                        const unsigned int field_second_values[] = { 
                            1, 
                            60, 
                            60 * 60, 
                            24 * 60 * 60, 
                            365 * 24 * 60 * 60};
                        requested_time += field_second_values[ min((size_t)fields, sizeof(field_second_values)/sizeof(field_second_values[0]) - 1) ];
                    }

                    // verify that increasing the requested time actually 
                    // puts us past the data time
                    ASSERT_COND( (requested_time > found_data_type.time.tv_sec) || ((requested_time == found_data_type.time.tv_sec) && (((long)microseconds * 1000) >= found_data_type.time.tv_nsec)) );

                    unsigned int seconds = requested_time - found_data_type.time.tv_sec;
                    byte_offset = seconds * data_rate 
                        - (uint64_t)round((found_data_type.time.tv_nsec / 1e9 - microseconds /1e6) * data_rate) 
                        + found_data_type.byte_offset;
                }
                
            }
            else {
                // failed to parse input as time, should be byte offset then
                char* endptr;
                byte_offset = strtoll(args[argument_position].c_str(), &endptr, 0);
                
                if ( ! ((*endptr == '\0') && (((byte_offset != std::numeric_limits<int64_t>::max()) && (byte_offset != std::numeric_limits<int64_t>::min())) || (errno!=ERANGE))) ) {
                    reply << " 8 : failed to parse byte offset or time code from '" << args[argument_position] << "' ;";
                    return reply.str();
                }
            }
        }

        // we should have a byte offset now
        if ( argument_position == 2 ) {
            // apply it to the start byte position
            if (byte_offset >= 0) {
                rte.pp_current += byte_offset;
            }
            else {
                rte.pp_current = rte.pp_end;
                rte.pp_current -= -byte_offset;
            }
        }
        else {
            // apply it to the end byte position
            if (byte_offset >= 0) {
                rte.pp_end = rte.xlrdev.getScan( rte.current_scan ).start();
                rte.pp_end += byte_offset;
            }
            else {
                rte.pp_end -= -byte_offset;
            }
        }
    }

    // check that the byte positions are within scan bounds
    // if not, set them to the scan bound themselves and return error 8
    ROScanPointer current_scan = rte.xlrdev.getScan( rte.current_scan );
    if ( (rte.pp_current < current_scan.start()) ||
         (rte.pp_end > current_scan.start() + current_scan.length()) ) {
        rte.setCurrentScan( rte.current_scan );
        reply << " 8 : scan pointer offset(s) are out of bound ;";
        return reply.str();
    }

    reply << " 0 ;";

    return reply.str();
}

string error_fn(bool q, const vector<string>& args, runtime& ) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    do_xlr_lock();
    XLR_ERROR_CODE error_code = ::XLRGetLastError();
    do_xlr_unlock();
    char error_string[XLR_ERROR_LENGTH + 1];
    error_string[XLR_ERROR_LENGTH] = '\0';
    XLRCALL( ::XLRGetErrorMessage(error_string, error_code) );
    reply << " 0 : " << error_code << " : " << error_string << ";";
    
    return reply.str();
}

string recover_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('='));

    if ( q ) {
        // command set doesn't say it's command only, 
        // neither what it does as a query, so just reply
        reply << " 0 ;";
        return reply.str();
    }
    
    if ( args.size() < 2 ) {
        reply << " 8 : missing recover mode parameter ;";
        return reply.str();
    }

    char* eptr;
    long int mode = ::strtol(args[1].c_str(), &eptr, 0);
    if ( (mode < 0) || (mode > 2) ) {
        reply << " 8 : mode (" << mode << ") out of range [0, 2] ;";
        return reply.str();
    }

    if ( mode == 0 ) {
        rte.xlrdev.recover( SS_RECOVER_POWERFAIL );
    }
    else if ( mode == 1 ) {
        rte.xlrdev.recover( SS_RECOVER_OVERWRITE );
    }
    else {
        rte.xlrdev.recover( SS_RECOVER_UNERASE );
    }

    reply << " 0 : " << mode << " ;";
    return reply.str();
}

string protect_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream              reply;

    reply << "!" << args[0] << (q?('?'):('='));

    if ( q ) {
        if ( rte.transfermode == condition ) {
            reply << " 6 : not possible during " << rte.transfermode << " ;";
            return reply.str();
        }
        if ( rte.xlrdev.bankMode() == SS_BANKMODE_DISABLED ) {
            reply << " 6 : cannot determine protect in non-bank mode ;";
            return reply.str();
        }
        S_BANKSTATUS bs[2];
        XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_A, &bs[0]) );
        XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_B, &bs[1]) );
        for (unsigned int bank = 0; bank < 2; bank++) {
            if (bs[bank].Selected) {
                reply << " 0 : " << (bs[bank].WriteProtected ? "on" : "off") << " ;";
                return reply.str();
            }
        }
        reply << " 6 : no bank selected ;";
    }
    else {
        if ( rte.transfermode != no_transfer ) {
            reply << " 6 : not possible during " << rte.transfermode << " ;";
            return reply.str();
        }

        if ( args.size() < 2 ) {
            reply << " 8 : must have argument ;";
            return reply.str();
        }
        
        if ( args[1] == "on" ) {
            rte.protected_count = 0;
            XLRCALL( ::XLRSetWriteProtect(rte.xlrdev.sshandle()) );
        }
        else if ( args[1] == "off" ) {
            rte.protected_count = 2;
            XLRCALL( ::XLRClearWriteProtect(rte.xlrdev.sshandle()) );
        }
        else {
            reply << " 8 : argument must be 'on' or 'off' ;";
            return reply.str();
        }

        reply << " 0 ;";
    }
    return reply.str();
}

string rtime_5a_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << (q?('?'):('='));
    uint64_t length = ::XLRGetLength(rte.xlrdev.sshandle());
    long page_size = ::sysconf(_SC_PAGESIZE);
    uint64_t capacity = (uint64_t)rte.xlrdev.devInfo().TotalCapacity * (uint64_t)page_size;
    inputmode_type inputmode;
    rte.get_input(inputmode);
    headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                 (unsigned int)rte.trackbitrate(),
                                 rte.vdifframesize());
    double track_data_rate = (double)dataformat.trackbitrate * (double)dataformat.framesize / (double)dataformat.payloadsize;
    double total_recording_rate = track_data_rate * dataformat.ntrack;

    reply << " 0 : " 
          << ((capacity - length) / total_recording_rate * 8) << "s : "
          << ((capacity - length) / 1e9) << "GB : " 
          << ((capacity - length) * 100.0 / capacity) << "% : "
          << inputmode.mode << " : "
          << inputmode.submode << " : " 
          << (track_data_rate/1e6) << "MHz : "
          << (total_recording_rate/1e6) << "Mbps ;";
        

    return reply.str();
}

string rtime_dim_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << (q?('?'):('='));
    uint64_t length = ::XLRGetLength(rte.xlrdev.sshandle());
    long page_size = ::sysconf(_SC_PAGESIZE);
    uint64_t capacity = (uint64_t)rte.xlrdev.devInfo().TotalCapacity * (uint64_t)page_size;

    mk5b_inputmode_type inputmode;
    rte.get_input(inputmode);
    headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                 (unsigned int)rte.trackbitrate(),
                                 rte.vdifframesize());
    double track_data_rate = (double)dataformat.trackbitrate * (double)dataformat.framesize / (double)dataformat.payloadsize;
    double total_recording_rate = track_data_rate * dataformat.ntrack;

    reply << " 0 : " 
          << ((capacity - length) / total_recording_rate * 8) << "s : "
          << ((capacity - length) / 1e9) << "GB : " 
          << ((capacity - length) * 100.0 / capacity) << "% : "
          << ((inputmode.tvg == 0 || inputmode.tvg == 3) ? "ext" : "tvg") << " : "
          << hex << "0x" << inputmode.bitstreammask << dec << " : " 
          << ( 1 << inputmode.j ) << " : "
          << (total_recording_rate/1e6) << "Mbps ;";
        
    return reply.str();
}

string track_set_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('='));

    if ( q ) {
        reply << " 0 : " << register2track(*rte.ioboard[ mk5areg::ChASelect ]) << " : " << register2track(*rte.ioboard[ mk5areg::ChBSelect ]) << " ;";
        return reply.str();
    }

    if ( args.size() < 3 ) {
        reply << " 8 : track_set requires 2 arguments ;";
        return reply.str();
    }
    
    mk5areg::regtype::base_type tracks[2];
    tracks[0] = rte.ioboard[ mk5areg::ChASelect ];
    tracks[1] = rte.ioboard[ mk5areg::ChBSelect ];
    unsigned long int parsed;
    char* eocptr;
    for ( unsigned int i = 0; i < 2; i++ ) {
        if ( args[i+1] == "inc" ) {
            tracks[i] = (tracks[i] + 1) % 64;
        }
        else if ( args[i+1] == "dec" ) {
            if ( tracks[i] == 0 ) {
                tracks[i] = 63;
            }
            else {
                tracks[i]--;
            }
        }
        else {
            parsed = ::strtoul(args[i+1].c_str(), &eocptr, 0);
            ASSERT2_COND( (parsed!=ULONG_MAX || errno!=ERANGE) &&
                          (parsed!=0         || eocptr!=args[i+1].c_str()) &&
                          parsed <= std::numeric_limits<unsigned int>::max(),
                          SCINFO("failed to parse track from '" << args[i+1] << "'") );
            tracks[i] = track2register( parsed );
        }
    }
    rte.ioboard[ mk5areg::ChASelect ] = tracks[0];
    rte.ioboard[ mk5areg::ChBSelect ] = tracks[1];
    
    reply << " 0 ;";
    return reply.str();
}

string tvr_fn(bool q, const vector<string>& args, runtime& rte) {
    // NOTE: this function is basically untested, as we don't have a vsi data generator
    ostringstream reply;
    per_runtime<uint64_t> bitstreammask;

    reply << "!" << args[0] << (q?('?'):('='));

    if ( q ) {
        reply << " 0 : " << (rte.ioboard[ mk5breg::DIM_GOCOM ] & rte.ioboard[ mk5breg::DIM_CHECK ]) << " : " << hex_t( (rte.ioboard[ mk5breg::DIM_TVRMASK_H ] << 16) | rte.ioboard[ mk5breg::DIM_TVRMASK_L ]) << " : " << (rte.ioboard[ mk5breg::DIM_ERF ] & rte.ioboard[ mk5breg::DIM_CHECK ]) << " ;";
        rte.ioboard[ mk5breg::DIM_ERF ] = 0;
        return reply.str();
    }

    // command

    uint64_t new_mask = 0;
    if ( (args.size() > 1) && !args[1].empty() ) {
        char* eocptr;
        new_mask = ::strtoull(args[1].c_str(), &eocptr, 0);
        ASSERT2_COND( !(new_mask==0 && eocptr==args[1].c_str()) && !(new_mask==~((uint64_t)0) && errno==ERANGE),
                  SCINFO("Failed to parse bit-stream mask") );
    }
    else {
        if ( bitstreammask.find(&rte) == bitstreammask.end() ) {
            reply << " 6 : no current bit-stream mask set yet, need an argument ;";
            return reply.str();
        }
        new_mask = bitstreammask[&rte];
    }

    if ( new_mask == 0 ) {
        // turn off TVR
        rte.ioboard[ mk5breg::DIM_GOCOM ] = 0;
    }
    else {
        rte.ioboard[ mk5breg::DIM_TVRMASK_H ] = (new_mask >> 16);
        rte.ioboard[ mk5breg::DIM_TVRMASK_L ] = (new_mask & 0xffff);
        rte.ioboard[ mk5breg::DIM_GOCOM ] = 1;
        bitstreammask[&rte] = new_mask;
    }

    reply << " 0 ;";
    return reply.str();

}

string itcp_id_fn(bool q,  const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    if ( q ) {
        reply << "0 : " << rte.itcp_id;
    }
    else {
        rte.itcp_id = OPTARG(1, args);
        reply << "0";
    }

    reply << " ;";
    return reply.str();
}

// A no-op. This will provide a success answer to any command/query mapped
// to it
string nop_fn(bool q, const vector<string>& args, runtime&) {
    ostringstream              reply;

    reply << "!" << args[0] << (q?('?'):('=')) << " 0 : actually this did not execute - mapped to a no-op function ;";
    return reply.str();
}

//
//
//    HERE we build the actual command-maps
//
//
const mk5commandmap_type& make_mk5a_commandmap( bool buffering ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev1", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev2", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev1", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev2", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("position", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("replaced_blks", replaced_blks_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("track_set", track_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("track_check", data_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("rtime", rtime_5a_fn)).second );
    


    // in2net + in2fork [same function, different behaviour]
    if ( buffering ) {
        ASSERT_COND( mk5.insert(make_pair("in2net",  mem2net_fn)).second );
    }
    else {
        ASSERT_COND( mk5.insert(make_pair("in2net", &in2net_fn<mark5a>)).second );
    }
    ASSERT_COND( mk5.insert(make_pair("in2fork", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2file", &in2net_fn<mark5a>)).second );
    if ( buffering ) {
        ASSERT_COND( mk5.insert(make_pair("record", &in2net_fn<mark5a>)).second );
    }
    else {
        ASSERT_COND( mk5.insert(make_pair("record", in2disk_fn)).second );
    }
    ASSERT_COND( mk5.insert(make_pair("in2mem", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2file",  mem2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2net",  mem2net_fn)).second );

    // net2out + net2disk [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2fork", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    // mem2*
    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    // disk2*
    ASSERT_COND( mk5.insert(make_pair("play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    // fill2*
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2out", fill2out_fn)).second );

    // file2*
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("play_rate", playrate_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5a_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("skip", skip_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<mark5a>)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );
#if 0
    // Not official mk5 commands but handy sometimes anyway :)
    insres = mk5commands.insert( make_pair("dbg", debug_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dbg into commandmap");

#endif
#if 0
    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
#endif
    ASSERT_COND( mk5.insert(make_pair("clock", mk5a_clock_fn)).second );
    return mk5;
}

// Build the Mk5B DIM commandmap
const mk5commandmap_type& make_dim_commandmap( bool buffering ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pointers", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("replaced_blks", replaced_blks_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_dim_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_dim_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("rtime", rtime_dim_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("tvr", tvr_fn)).second );

    // in2net + in2fork [same function, different behaviour]
    if ( buffering ) {
        ASSERT_COND( mk5.insert(make_pair("in2net",  mem2net_fn)).second );
    }
    else {
        ASSERT_COND( mk5.insert(make_pair("in2net",  &in2net_fn<mark5b>)).second );
    }
    ASSERT_COND( mk5.insert(make_pair("in2fork", &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2file", &in2net_fn<mark5b>)).second );
    if ( buffering ) {
        ASSERT_COND( mk5.insert(make_pair("record", &in2net_fn<mark5b>)).second );
    }
    else {
        ASSERT_COND( mk5.insert(make_pair("record", in2disk_fn)).second );
    }
    ASSERT_COND( mk5.insert(make_pair("in2mem", &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2file",  mem2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2net",  mem2net_fn)).second );

    // sekrit functions ;) Mk5B/DIM is not supposed to be able to record to
    // disk/output ... but the h/w can do it all the same :)
    // net2out + net2disk [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2fork", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    // mem2*
    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    // disk2*
    ASSERT_COND( mk5.insert(make_pair("play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );

    // file2*
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("clock_set", clock_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("1pps_source", pps_source_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pps", pps_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot", dot_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot_set", dot_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot_inc", dot_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdim_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("skip", skip_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<mark5b>)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );

#if 0
    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
#endif
    return mk5;
}

const mk5commandmap_type& make_dom_commandmap( bool ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdom_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pointers", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("replaced_blks", replaced_blks_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    // fill2*
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2out", fill2out_fn)).second );

    // net2*
    //ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    // mem2*
    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    // file2*
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );

    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<0>)).second );
    // Mk5B/DOM has an I/O board but can't read from it
    //ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<0>)).second );
    //ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<0>)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );

    return mk5;
}

const mk5commandmap_type& make_generic_commandmap( bool ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    //ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    //ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("memstat", memstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdom_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("position", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pointers", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("replaced_blks", replaced_blks_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );
    // We must be able to sort of set the trackbitrate. Support both 
    // play_rate= and clock_set (since we do "mode= mark4|vlba" and
    // "mode=ext:<bitstreammask>")
    ASSERT_COND( mk5.insert(make_pair("play_rate", mk5c_playrate_clockset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("clock_set", mk5c_playrate_clockset_fn)).second );


    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    // fill2*
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );

    // net2*
    //ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    
    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<0>)).second );
    // Generic PCs don't have Haystack I/O boards
    //ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<0>)).second );
    //ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<0>)).second );


    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("mem2file",  mem2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2net",  mem2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    return mk5;
}

