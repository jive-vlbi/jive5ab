// implementation
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
#include <rotzooi.h>
#include <runtime.h>
#include <byteorder.h>
#include <streamutil.h>

#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <iostream>
#include <sstream>

using std::endl;
using std::pair;
using std::string;
using std::make_pair;

// implementation of the exception
DEFINE_EZEXCEPT(rotclock);

rot2systime::rot2systime():
    rot( 0.0 ), rotrate( 32.0e6 ) 
{}

rot2systime::rot2systime(const pcint::timeval_type& tv, double rotv, double ratev):
    rot( rotv ), rotrate( ratev ), systime( tv )
{
    // rot-rate of < 1.0e-5 is highly unlikely ...
    EZASSERT( (::fabs(rotrate)>=1.0e-5), rotclock);
}

// ROT is time since start of year in units of 'sysclicks'
// where there are 32.0e6 'sysclicks' per second.
string rot_as_string( double rot ) {
    double             seconds( rot/32.0e6 );
    double             intpart, dummy;
    const double       sec_p_day( 24.0 * 3600 );
    const double       sec_p_hour( 3600.0 );
    const double       sec_p_min( 60.0 );
    std::ostringstream oss;

    // Number of whole days
    dummy = ::modf( seconds/sec_p_day, &intpart );
    oss << format("%03d", (int)intpart) << "/";

    // ok, subtract those from the ROT and get the
    // nr of hours
    seconds -= (intpart * sec_p_day);
    dummy    = ::modf( seconds/sec_p_hour, &intpart );
    oss << format("%02d", (int)intpart) << ":";

    // good, subtract and find minutes
    seconds -= (intpart * sec_p_hour);
    dummy    = ::modf( seconds/sec_p_min, &intpart );
    oss << format("%02d", (int)intpart) << ":";

    // finally we're only left with seconds + fractional seconds
    // (after subtraction
    seconds -= (intpart * sec_p_min);
    oss << format("%8.5lf", seconds);

    return oss.str();
}


// should only be called when indeed there is somethink to read
// from fd.
// Function somewhat loosely inspired by jball5a.
// NOTE NOTE NOTE NOTE
// THIS METHOD IS NOT MT-SAFE! Which is to say: you should
// NOT execute it from >1 thread at any time for reliable
// results!!!
void process_rot_broadcast(int fd, runtime& rte) {
    // make all variables static so fn-call is as quick as possible
    static char                buffer[ 8192 ];
    static double              rot;
    static double              rate;
    static double              tmp;
    static ssize_t             nread;
    // warn if abs(ROT-systemtime) > this value [units is in seconds]
    static const double        driftlimit = 1.0e-1; 
    static struct Set_Rot*     msgptr = reinterpret_cast<Set_Rot*>( &buffer[0] );
    static endian_converter    cvt(mimicHost, bigEndian);
    static pcint::timeval_type now;

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

    DEBUG(3, "process_rot: MSG " << hex_t(msgptr->msg_type)
             << " ID " << hex_t(msgptr->msg_id) 
             << " AC " << hex_t(msgptr->action_code) << endl);

    // Not finding the right amount of bytes or not the 
    // wrong message-type results in early returnance
    if( nread!=sizeof(struct Set_Rot) ||
        (msgptr->action_code!=0x10001 && msgptr->action_code!=0x10002) )
        return;

    // Great! It was a rot-broadcast.
    // Now decode the values we actually need
    cvt(msgptr->su_array); // taskid, jobid, su_array -> yeah whatevah!

    // convert the doubles into local copies - they may be misaligned
    // (ie not on an 8-byte boundary, which could/would cause a SIGBUS)
    ::memcpy((void *)&tmp, (const void*)msgptr->rot, sizeof(tmp));
    cvt(rot, tmp);
    ::memcpy((void *)&tmp, (const void*)msgptr->rot_rate, sizeof(tmp));
    cvt(rate, tmp);

    // only really process if rot > 0.0 and rate>1.0e-4?
    if( rot>0.0 && rate>1.0e-4) {
        task2rotmap_type::iterator            taskptr;

        // see if we already have a mapping for this job/task/su_array
        taskptr     = rte.task2rotmap.find( msgptr->su_array );

        if( taskptr==rte.task2rotmap.end() ) {
            // nope. no entry. make a gnu one
            pair<task2rotmap_type::iterator,bool> insres;

            insres  = rte.task2rotmap.insert( make_pair(msgptr->su_array, rot2systime()) );
            ASSERT2_COND( insres.second==true,
                          SCINFO("Failed to insert new entry into taskid->ROT map"));
            taskptr = insres.first;
        } else {
            // yup. let's check if delta-systime is comparable to
            // delta-rot to see if we are somewhat doing Ok as for timestability
            double          drot, dsys;

            // compute delta-systime
            dsys = (double)(now - taskptr->second.systime);

            // drot is _slightly_ more complex; it may have a non 1:1 clockrate
            // wrt to wallclocktime [speed-up/slow-down]. Note: we use the previous
            // rotrate; the current Set_Rot message's rot-rate may be different
            // but will only be valid from next ROT1PPS.
            // NOTE: rotrate is guaranteed to be != 0

            // computation = (newrot - oldrot)/rotclockrate => gives wallclock seconds
            drot = (rot - taskptr->second.rot)/taskptr->second.rotrate;

            // Warn if the system seems to drift too much
            if( ::fabs(drot-dsys)>driftlimit ) {
                DEBUG(-1, "ROTProxy: systemtime & rotclock are drifting by more than " 
                          << driftlimit << "s" << endl <<
                          "          for task " << taskptr->first << endl );
            }
        }
        // Now update the mapping to be the new one
        taskptr->second = rot2systime(now, rot, rate);
    }

    DEBUG(2,"ROTProxy: job=" << msgptr->su_array << " ROT: " << rot_as_string(rot) << endl);
    return;
}
