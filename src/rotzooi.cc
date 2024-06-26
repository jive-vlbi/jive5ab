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

#include <math.h>

using std::endl;
using std::pair;
using std::string;
using std::make_pair;

// implementation of the exception
DEFINE_EZEXCEPT(rotclock)

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
    double             intpart;
    const double       sec_p_day( 24.0 * 3600 );
    const double       sec_p_hour( 3600.0 );
    const double       sec_p_min( 60.0 );
    std::ostringstream oss;

    // Number of whole days
    (void)::modf( seconds/sec_p_day, &intpart );
    oss << format("%03d", (int)intpart) << "/";

    // ok, subtract those from the ROT and get the
    // nr of hours
    seconds -= (intpart * sec_p_day);
    (void)::modf( seconds/sec_p_hour, &intpart );
    oss << format("%02d", (int)intpart) << ":";

    // good, subtract and find minutes
    seconds -= (intpart * sec_p_hour);
    (void)::modf( seconds/sec_p_min, &intpart );
    oss << format("%02d", (int)intpart) << ":";

    // finally we're only left with seconds + fractional seconds
    // (after subtraction
    seconds -= (intpart * sec_p_min);
    oss << format("%05.2lf", seconds);

    return oss.str();
}

static endian_converter    cvt(mimicHost, bigEndian);

void setrot(pcint::timeval_type& nu, Rot_Entry& re, runtime& rte) {
    static double              rot;
    static double              rate;
    static double              tmp;
    // warn if abs(ROT-systemtime) > this value [units is in seconds]
    static const double        driftlimit = 1.0e-1; 
    task2rotmap_type::iterator taskptr;

    cvt(re.su_array);
    // convert the doubles into local copies - they may be misaligned
    // (ie not on an 8-byte boundary, which could/would cause a SIGBUS)
    ::memcpy((void *)&tmp, (const void*)re.rot, sizeof(tmp));
    cvt(rot, tmp);
    ::memcpy((void *)&tmp, (const void*)re.rot_rate, sizeof(tmp));
    cvt(rate, tmp);

    if( rot<=0.0 || rate<=1.0e-4 ) {
        DEBUG(0, "SETROT  [" << format("%7u",re.su_array) << "] " << nu << ": invalid {rot,rate}={"
                 << rot << "," << rate << "}" << endl);
        return;
    }
    taskptr     = rte.task2rotmap.find( re.su_array );

    if( taskptr==rte.task2rotmap.end() ) {
        // nope. no entry. make a gnu one
        pair<task2rotmap_type::iterator,bool> insres;

        insres  = rte.task2rotmap.insert( make_pair(re.su_array, rot2systime()) );
        ASSERT2_COND( insres.second==true,
                SCINFO("Failed to insert new entry into taskid->ROT map"));
        taskptr = insres.first;
    } else {
        // yup. let's check if delta-systime is comparable to
        // delta-rot to see if we are somewhat doing Ok as for timestability
        double          drot, dsys;

        // compute delta-systime
        dsys = (double)(nu - taskptr->second.systime);

        // drot is _slightly_ more complex; it may have a non 1:1 clockrate
        // wrt to wallclocktime [speed-up/slow-down]. Note: we use the previous
        // rotrate; the current Set_Rot message's rot-rate may be different
        // but will only be valid from next ROT1PPS.
        // NOTE: rotrate is guaranteed to be != 0

        // computation = (newrot - oldrot)/rotclockrate => gives wallclock seconds
        drot = (rot - taskptr->second.rot)/taskptr->second.rotrate;

        // Warn if the system seems to drift too much
        if( ::fabs(drot-dsys)>driftlimit ) {
            DEBUG(-1, "TORTES  [" << format("%7u",taskptr->first) << "] "
                    << nu << ": sROT=" << rot_as_string(rot)
                    << " dSYS=" << format("%05.2lf",dsys) << "s dROT="
                    << format("%05.2lf",drot) << "s" << endl);
        }
    }
    // Now update the mapping to be the new one
    taskptr->second = rot2systime(nu, rot, rate);
    DEBUG(3, "SETROT  [" << format("%7u",taskptr->first) << "] " << nu << ": sROT="
             << rot_as_string(rot) << endl);
}

void checkrot(pcint::timeval_type& nu, Rot_Entry& re, runtime& rte) {
    static double              rot;
    static double              rate;
    static double              tmp;
    // warn if abs(ROT-systemtime) > this value [units is in seconds]
    static const double        driftlimit = 1.0e-1; 
    task2rotmap_type::iterator taskptr;

    cvt(re.su_array);
    // convert the doubles into local copies - they may be misaligned
    // (ie not on an 8-byte boundary, which could/would cause a SIGBUS)
    ::memcpy((void *)&tmp, (const void*)re.rot, sizeof(tmp));
    cvt(rot, tmp);
    ::memcpy((void *)&tmp, (const void*)re.rot_rate, sizeof(tmp));
    cvt(rate, tmp);

    if( rot<=0.0 || rate<=1.0e-4 ) {
        DEBUG(0, "CHECKROT[" << format("%7u", re.su_array) << "] " << nu
                 << ": invalid {rot,rate}={"
                 << rot << "," << rate << "}" << endl);
        return;
    }

    // see if we already have a mapping for this job/task/su_array
    taskptr     = rte.task2rotmap.find( re.su_array );

    if( taskptr==rte.task2rotmap.end() ) {
        // nope. no entry. make a gnu one
        pair<task2rotmap_type::iterator,bool> insres;

        insres  = rte.task2rotmap.insert( make_pair(re.su_array, rot2systime()) );
        ASSERT2_COND( insres.second==true,
                SCINFO("Failed to insert new entry into taskid->ROT map"));
        taskptr = insres.first;
    } else {
        // yup. let's check if delta-systime is comparable to
        // delta-rot to see if we are somewhat doing Ok as for timestability
        double          drot, dsys;

        // compute delta-systime
        dsys = (double)(nu - taskptr->second.systime);

        // drot is _slightly_ more complex; it may have a non 1:1 clockrate
        // wrt to wallclocktime [speed-up/slow-down]. Note: we use the previous
        // rotrate; the current Set_Rot message's rot-rate may be different
        // but will only be valid from next ROT1PPS.
        // NOTE: rotrate is guaranteed to be != 0

        // computation = (newrot - oldrot)/rotclockrate => gives wallclock seconds
        drot = (rot - taskptr->second.rot)/taskptr->second.rotrate;

        // Warn if the system seems to drift too much
        if( ::fabs(drot-dsys)>driftlimit ) {
            DEBUG(-1, "TORKCEHC[" << format("%7u",taskptr->first) << "] "
                    << nu << ": sROT=" << rot_as_string(rot)
                    << " dSYS=" << format("%05.2lf",dsys) << "s dROT="
                    << format("%05.2lf",drot) << "s" << endl);
        }
    }
    // Now update the mapping to be the new one
    taskptr->second = rot2systime(nu, rot, rate);
    DEBUG(4, "CHECKROT[" << format("%7u",taskptr->first) << "] " << nu << ": cROT="
             << rot_as_string(rot) << endl);
}

void finishrot(pcint::timeval_type& nu, Rot_Entry& re, runtime& rte) {
    std::ostringstream         s;
    task2rotmap_type::iterator taskptr;

    cvt(re.su_array);

    s << "FINISROT[" << format("%7u", re.su_array) << "] " << nu << ": ";
    // see if we _have_ a mapping for this job/task/su_array.
    // if so, do delete it; it was a finish rot after all
    if( (taskptr=rte.task2rotmap.find(re.su_array))!=rte.task2rotmap.end() ) {
        rte.task2rotmap.erase(taskptr);
        s << "erased";
    } else
        s << "NOT PREVIOUSLY IN MAP!";
    DEBUG(4, s.str() << endl);
}

void alarmrot(pcint::timeval_type& nu, Rot_Entry& re, runtime&) {
    cvt(re.su_array);
    // make sure the bits are in an order we can work with
    DEBUG(4, "ALARMROT[" << format("%7u", re.su_array) << "] " << nu << ": *pling*!" << endl);
}
