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

#include <map>
#include <string>
#include <timewrap.h>
#include <ezexcept.h>


// Declare a ROT-exception
DECLARE_EZEXCEPT(rotclock);

// this links the systemtime to a ROT + rate.
struct rot2systime {
    double         rot;
    double         rotrate;
    pcint::timeval systime;

    // defaults: systime=0.0, rot=0.0, rate=32.0e6
    rot2systime();

    // may throw. especially when rate == 0 [or close enough to it]
    rot2systime(const pcint::timeval& tv, double rotv, double ratev);
};

// transform ROT into human-readable string [day-of-year/HH:MM:SS.SSSSS]
std::string rot_as_string( double rot );

typedef unsigned int taskid_type;

// keep them in a mapping of jobid/taskid (whatever you
// fancy naming it)
typedef std::map<taskid_type,rot2systime> task2rotmap_type;

// Uhmm to avoid circular dependencies i kludge this like this.
// The proper way to solve this would've been to put this
// function in a separate compilation unit with accompanying
// header file but I didn't wanna.
struct runtime;
void process_rot_broadcast(int fd, runtime& rte);


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
    U32 action_code;                    /* SET_ROT = 0x10001                  */
    U32 queueing_flags;                 /*   set to 0                         */
    U32 obey_rot[2];                    /*   --unused--                       */
    U32 end_rot[2];                     /*   --unused--                       */
    U32 repeat_interval[2];             /*   --unused--                       */
    U32 num_ent;                        /*   --unused--                       */
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

#endif
