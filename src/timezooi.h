// handy date/time functions for all kinds of dataformats
// Copyright (C) 2007-2011 Des Small and a bit of Harro Verkouter
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
// Author:  Des Small - small@jive.nl
//          (Harro Verkouter - verkouter@jive.nl)
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef TIMEZOOI_H
#define TIMEZOOI_H

#include <time.h>
#include <sys/types.h> // useconds_t

// Get the current year
int get_current_year( void );

// timegm(3) is GNU extension, it is not in POSIX
time_t my_timegm(struct tm *tm);

// From a 'struct tm', compute the (Modified) Julian Day
// UNIX_MJD_EPOCH is the mjd of Jan 1st 1970, so we can easily transform
// between mjd <-> unix days
const unsigned int UNIX_MJD_EPOCH = 40587;
double jd(const struct tm& tref);
double mjd(const struct tm& tref);
double mjdnow( void );

// Get the Julian Day of the first day of the indicated year
// 'boy' = begin of year ...
int jdboy(int year);

// Fill in year/doy/h/m/s and it will normalize
// it. (Notably the change from day-of-year -> month/day)
// The normalization only seems to work reliably 
// when "tm->year" >= 2 && "tm->yday >= 1" (MacOS X 10.5)
// Also return the time_t that the normalized tm represents
time_t normalize_tm(struct tm *tm);
// Id. as normalize_tm only it will interpret (ie make sure) that the fields
// in tm are interpreted as GMT rather than local
time_t normalize_tm_gm(struct tm *tm);


// Mac OSX and OpenBSD don't have no clock_nanosleep!
// but OpenBSD does have clockid_t ...
#if defined(__APPLE__)
#ifndef CLOCK_REALTIME
typedef int clockid_t;
const clockid_t CLOCK_REALTIME = 0;
#endif
const int       TIMER_ABSTIME  = 0;
#endif // defined (__APPLE__)

#if defined(__APPLE__) || defined(__OpenBSD__)
int clock_nanosleep(clockid_t, int, const struct timespec* ts, struct timespec*);
#endif // defined (__APPLE__)

// Implement local version of usleep() using POSIX nanosleep()
// (usleep() is not POSIX and the *BSD manuals say that usleep
//  is implented through nanosleep() :D. So we can do just the same)
// Note: contrary usleep(3) we do NOT return on EINTR - we keep
//       sleeping until the entire amount is slept
namespace evlbi5a {
    int usleep(useconds_t useconds);
}

#endif // #ifdef TIMEZOOI_H
