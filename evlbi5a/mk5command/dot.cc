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
#include <jive5a_bcd.h>
#include <iostream>

using namespace std;


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

    // HV: 25-sep-2013 The "get_dot()" function may return an
    //                 "empty" dot_type - with two zero timestamps.
    //                 This implies that the dot clock isn't
    //                 running, or at least, that there are
    //                 not DOT 1PPS interrupts coming

    // Depending on wether FHG running or not, take time
    // from h/w or from the pseudo-dot
    if( fhg ) {
        int                 tmjd, tmjd0, tmp;
        dot_type            dot_info = get_dot();
        struct tm           tm_dot;
        struct timeval      tv;

        if( !dot_info ) {
            // see 25-sep-2013 comment above!
            reply << " 4 : DOT clock is not running - no DOT interrupts coming ;";
            return reply.str();
        }

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

        if( !dot_info ) {
            // see 25-sep-2013 comment above!
            reply << " 4 : DOT clock is not running - no DOT interrupts coming ;";
            return reply.str();
        }

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
