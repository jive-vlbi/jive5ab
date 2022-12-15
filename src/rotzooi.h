// utilities for dealing with ROTs
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
#ifndef JIVE5A_ROTZOOI_H
#define JIVE5A_ROTZOOI_H

#include <timewrap.h>
#include <ezexcept.h>
#include <byteorder.h>
#include <streamutil.h>
#include <evlbidebug.h>

#include <map>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <iostream>
#include <sstream>

// Plain-old C headers
#include <cstring>   // for ::memset()

// Declare a ROT-exception
DECLARE_EZEXCEPT(rotclock)

// this links the systemtime to a ROT + rate.
struct rot2systime {
    double              rot;
    double              rotrate;
    pcint::timeval_type systime;

    // defaults: systime=0.0, rot=0.0, rate=32.0e6
    rot2systime();

    // may throw. especially when rate == 0 [or close enough to it]
    rot2systime(const pcint::timeval_type& tv, double rotv, double ratev);
};

// transform ROT into human-readable string [day-of-year/HH:MM:SS.SSSSS]
std::string rot_as_string( double rot );

typedef unsigned int taskid_type;

// keep them in a mapping of jobid/taskid (whatever you
// fancy naming it)
typedef std::map<taskid_type,rot2systime> task2rotmap_type;

// This is the rot-broadcast message we're looking for.
// Taken verbatim from Haystack's Mark5A/message_structs.h
/*******************************************************************************
*                                                                              *
*                 EVN-style (EVN doc #42) message to set ROT's                 *
*                                                                              *
* Note 1: does *not* use the J-K (Ball-Dudevoir) messaging system              *
* Note 2: due to EVN msg alignment, some doubles are on 4 byte boundaries,     *
*         which we work around by using a U32 array of two elements instead    *
*******************************************************************************/

/* works on both ILP32 and LP64 systems */
typedef unsigned int U32;

/* HV: Typically, the Rot Clock messages (potentially) contain an array of 
 *     clock-message entries like these below.
 *
 *     Known action codes:
 *          0x10001 (SET_ROT)
 *          0x10002 (CHECK_ROT)
 *          0x10003 (FINISH_ROT)
 *          0x10004 (ALARM)
 */
struct Rot_Entry {
    U32 offset;                         /*   --unused--                       */
    U32 su_array;                       /* task_id => set corresponding ROT   */
                                        /*                    -1 => set COT   */
    U32 rot[2];                         /* actually a double                  */
                                        /* set ROT to this at next systick;   */
    U32 rot_year;                       /* of current observation             */
    U32 rot_rate[2];                    /* ROT inc. per systick (in sysclks)  */
                                        /* actually a double                  */
    U32 dummy;                          /*   --unused--                       */
};


struct Set_Rot {
    U32 msg_type;                       /* action request = 0x10              */
    U32 msg_id;                         /* task_id for this ROT clock         */
    U32 ref_id;                         /*   --unused--                       */
    U32 msg_src1;                       /*   --unused--                       */
    U32 msg_src2;                       /*   --unused--                       */
    U32 msg_dest1;                      /*   --unused--                       */
    U32 msg_dest2;                      /*   --unused--                       */
    U32 time_stamp_sysclks[2];          /* COT on next systick - sanity check */
    U32 time_stamp_date;                /*   --unused--                       */
    U32 full_rot_sysclks[2];            /*   --unused--                       */
    U32 full_rot_date;                  /*   --unused--                       */
    U32 msg_size;                       /* length = 0x7C                      */
    U32 action_code;                    /* SET_ROT = 0x10001 (HV: see above)  */
    U32 queueing_flags;                 /*   set to 0                         */
    U32 obey_rot[2];                    /*   --unused--                       */
    U32 end_rot[2];                     /*   --unused--                       */
    U32 repeat_interval[2];             /*   --unused--                       */
    U32 num_ent;                        /*HV: number of Rot_Entries following */
    Rot_Entry entry[1];                 /*HV: Oldest trick in the book to 
                                              allow "dynamic" array sizing -
                                              this struct [Set_Rot] is overlaid
                                              onna piece of mem'ry so accessing
                                              entry[1], entry[2] etc. addresses
                                              *outside* this struct but as long
                                              as we stay within the mem'ry 
                                              that this thing is overlaid upon
                                              (and we know that the layout beyond
                                              ourselves is what we expect ...
                                              it works great!                 */
#if 0
    U32 offset;                         /*   --unused--                       */
    U32 su_array;                       /* task_id => set corresponding ROT   */
                                        /*                    -1 => set COT   */
    U32 rot[2];                         /* actually a double                  */
                                        /* set ROT to this at next systick;   */
    U32 rot_year;                       /* of current observation             */
    U32 rot_rate[2];                    /* ROT inc. per systick (in sysclks)  */
                                        /* actually a double                  */
    U32 dummy;                          /*   --unused--                       */
#endif
};

// functions that handle the ROT messages above

// Uhmm to avoid circular dependencies i kludge this like this.
// The proper way to solve this would've been to put this
// function in a separate compilation unit with accompanying
// header file but I didn't wanna.
struct runtime;

void setrot(pcint::timeval_type& nu, Rot_Entry& re, runtime& rte);
void checkrot(pcint::timeval_type& nu, Rot_Entry& re, runtime& rte);
void finishrot(pcint::timeval_type& nu, Rot_Entry& re, runtime& rte);
void alarmrot(pcint::timeval_type& nu, Rot_Entry& re, runtime&);

// should only be called when indeed there is somethink to read
// from fd.
// Function somewhat loosely inspired by jball5a.
// NOTE NOTE NOTE NOTE
// THIS METHOD IS NOT MT-SAFE! Which is to say: you should
// NOT execute it from >1 thread at any time for reliable
// results!!!
template<typename T> void process_rot_broadcast(int fd, const T& begin, const T& end) {
    // make all variables static so fn-call is as quick as possible
    static char                buffer[ 8192 ];
    static ssize_t             nread;
    static struct Set_Rot*     msgptr = reinterpret_cast<Set_Rot*>( &buffer[0] );
    static pcint::timeval_type now;

    static endian_converter    cvt(mimicHost, bigEndian);

    // the 'Set_Rot' always applies to next 1PPS tick so we must
    // increment the time by 1 second. Already do this such that
    // if we decide to actually *use* the value of 'now' we know
    // it's good to go. 
    now  = pcint::timeval_type::now();
    now += 1.0;

    // Rite-o! Read a bunch-o-bytes from the sokkit.
    // Only <0 is treated as exceptional behaviour
    ::memset(buffer, 0x00, sizeof(buffer));
    EZASSERT_POS( (nread=::recv(fd, buffer, sizeof(buffer), 0)), rotclock);

    // cvt is set up to convert from bigEndian [JCCS's byteorder]
    // to whatever the local host's byteorder is
    cvt(msgptr->action_code);
    cvt(msgptr->msg_type);
    cvt(msgptr->msg_id);
    cvt(msgptr->num_ent);

    if( (nread<((ssize_t)sizeof(struct Set_Rot))) ||
        (msgptr->num_ent==0) || 
        (nread<(ssize_t)(sizeof(struct Set_Rot)+(msgptr->num_ent-1)*sizeof(struct Rot_Entry)))) {
        DEBUG(0, "process_rot " << now << ": Not enough bytes in networkpacket for "
              << msgptr->num_ent << " entries" << std::endl);
        return;
    }
    void   (*handler)(pcint::timeval_type&,Rot_Entry&,runtime&) = 0;

    switch( msgptr->action_code ) {
        case 0x10001: handler = setrot;    break;
        case 0x10002: handler = checkrot;  break;
        case 0x10003: handler = finishrot; break;
        case 0x10004: handler = alarmrot;  break;
        default:
            break;
    }
    if( handler==0 ) {
        DEBUG(0, "process_rot " << now << ": unknown action code "
              << format("0x%08x", msgptr->action_code) << std::endl);
        return;
    }
    // Great! It was a rot-broadcast.
    // Now decode the values we actually need
    T iter = begin;
    for (;
         iter != end;
         iter++) {
        for(unsigned int i=0; i<msgptr->num_ent; i++)
            handler(now, msgptr->entry[i], **iter);
    }
    return;
}

#endif
