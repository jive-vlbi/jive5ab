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
#include <timezooi.h>
#include <iostream>

using namespace std;


// set the DOT at the next 1PPS [if one is set, that is]
// this function also performs the dot_inc command
string dot_set_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream              reply;
    pcint::timeval_type        dot;
    const transfer_type        ctm = rte.transfermode;

    // We must remember these across function calls since 
    // the user may query them later
    static int                 dot_inc;

    // Already form this part of the reply
    reply << "!" << args[0] << (q?('?'):('='));

    // DOT_set, dot_inc queries are always possible
    // commands only when the i/o board is not being used
    // (i.e. we're "busy" if they ARE used)
    INPROGRESS(rte, reply, !q && (fromio(ctm) || toio(ctm)))

    // Handle dot_inc command/query
    if( args[0]=="dot_inc" ) {
        char*           eptr;
        string          incstr( OPTARG(1, args) );

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
        // DOT inc may fail for a few reasons:
        //  * dot clock not running  (no interrupts)
        //  * dot_set is in progress
        //  * dot clock not configured
        const dot_return_type dot_status = inc_dot( dot_inc );
        reply << " " << dotstatus2errcode(dot_status) << " : " << dotstatus2str(dot_status) << " ;";
        return reply.str();
    }

    if( q ) {
        pcint::timeval_type   dot_set;
        if( !get_set_dot(dot_set) )
            reply << " 1 : setting of the DOT is still in progress";
        else
            reply << " 0 : " << dot_set << " : * : " << format("%7.4lf", get_set_dot_delay());
        reply << " ;";
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
            reply << " 4 : DOT clock not running yet, use 1pps_source= and clock_set= first (not forced);";
            return reply.str();
        }
        reply << " 1 : dot_set initiated - will be executed at next 1PPS (not forced);";
        return reply.str();
    }
    // So, we end up here because either force==true OR the card is not
    // synced yet. For the commandsequence that does not matter.

    // Now wait for 1PPS to happen [SUNKPPS becoming 1 after being reset].
    // If "force" we tell it to sync, otherwise we just clear the SUNKPPS.
    bool                 synced( false );
    pcint::timediff      dt;
    pcint::timeval_type  start;

    // Stop checking the PPS flags (APERTURE_SYNC, EXACTSYNC)
    iob[mk5breg::DIM_CLRPPSFLAGS] = 1;

    // Pulse the "Reset PPS" bit
    iob[mk5breg::DIM_RESETPPS] = 1;
    iob[mk5breg::DIM_RESETPPS] = 0;

    // Id. for the syncpps bit - make sure it goes through a zero -> one
    // transition
    iob[mk5breg::DIM_SYNCPPS] = 0;
    iob[mk5breg::DIM_SYNCPPS] = 1;

    DEBUG(4, " before loop: sunkpps=" << *sunkpps << " (syncpps=" << *iob[mk5breg::DIM_SYNCPPS] << ")" << endl);

    // wait at most 3 seconds for SUNKPPS to transition to '1'
    start = pcint::timeval_type::now();
    do {
        // sleep for 0.1 ms
        ::usleep(100);
        // check!
        synced = *sunkpps;
        dt     = pcint::timeval_type::now() - start;
    } while( !synced && dt<3.0 );

    // Resume checking the flags
    iob[mk5breg::DIM_CLRPPSFLAGS] = 0;

    DEBUG(4, " 1PPS sync " << (synced?"SUCCESS":"FAIL") << " after " << dt << "s of waiting" << endl);

    // If we did succesfully synchronize to the hardware PPS,
    // put in a request for a new dot!
    // Note: if the user did not specifiy a DOT, it will 
    //       be empty and the DOT1PPS interrupt handler
    //       will take care of setting the DOT itself
    const bool   set_dot_ok = (synced && set_dot(dot));

    // well ... ehm .. that's it then? we're sunked and
    // the systemtime <-> dot mapping's been set up.
    if( !synced ) {
        reply << " 4 : Failed to sync to selected 1PPS signal ;";
    } else {
        if( !set_dot_ok ) {
            reply << " 4 : DOT clock not running yet, use 1pps_source= and clock_set= first (" << (force?"re":"") << "sync needed);";
        } else {
            // ok, dot will be set succesfully.
            reply << " 1 : dot_set initiated - will be executed on next 1PPS (" << (force?"re":"") << "sync);";
        }
    }
    return reply.str();
}
