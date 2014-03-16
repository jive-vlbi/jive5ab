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
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <timezooi.h>
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

#include <exception>
#include <stdexcept>

int get_current_year(void) {
	  time_t t;
	  struct tm now;
	  ::time(&t);
	  ::gmtime_r(&t, &now);
	  return 1900+now.tm_year;
}


// timegm(3) is GNU extension, it is not in POSIX
// so let's re-implement it here, as per 
//    http://linux.die.net/man/3/timegm
time_t my_timegm(struct tm *tm) {
    char*  tz;
    time_t ret;

    tz = ::getenv("TZ");
    ::setenv("TZ", "", 1);
    ::tzset();
    ret = ::mktime(tm);
    if (tz)
        ::setenv("TZ", tz, 1);
    else
        ::unsetenv("TZ");
    ::tzset();
    return ret;
}


// From a 'struct tm', compute the Modified Julian Day, cf.
//      http://en.wikipedia.org/wiki/Julian_day
// The Julian day number can be calculated using the following formulas:
// The months January to December are 1 to 12. Astronomical year numbering is used, thus 1 BC is
// 0, 2 BC is −1, and 4713 BC is −4712. In all divisions (except for JD) the floor function is
// applied to the quotient (for dates since 1 March −4800 all quotients are non-negative, so we
// can also apply truncation).
double mjd( const struct tm& tref ) {
    return jd(tref)-2400000.5;
}

double jd( const struct tm& tref ) {
    double    a, y, m, jd;

    // As per localtime(3)/gmtime(3), the tm_mon(th) is according to
    // 0 => Jan, 1 => Feb etc
    a   = ::floor( ((double)(14-(tref.tm_mon+1)))/12.0 );

    // tm_year is 'years since 1900'
    y   = (tref.tm_year+1900) + 4800 - a;

    m   = (tref.tm_mon+1) + 12*a - 3;

    // tm_mday is 'day of month' with '1' being the first day of the month.
    // i think we must use the convention that the first day of the month is '0'?
    // This is, obviously, assuming that the date mentioned in 'tref' is 
    // a gregorian calendar based date ...
    jd  = (double)tref.tm_mday + ::floor( (153.0*m + 2.0)/5.0 ) + 365.0*y
          + ::floor( y/4.0 ) - ::floor( y/100.0 ) + ::floor( y/400.0 ) - 32045.0;

    // that concluded the 'integral day part'.
    // Now add the time-of-day, as a fraction
    jd += ( ((double)(tref.tm_hour - 12))/24.0 +
            ((double)(tref.tm_min))/1440.0 +
            (double)tref.tm_sec/86500.0 );
    return jd;
}

int jdboy(int year) {
  int jd, y;
  
  y = year + 4799;
  jd = y * 365 + y / 4 - y / 100 + y / 400 - 31739;
  
  return jd;
}

double mjdnow( void ) {
    time_t     now = ::time(NULL);
    struct tm  tm_now;

    ::gmtime_r(&now, &tm_now);
    return ::mjd(tm_now);
}


/* From the man page:
   mktime() ignores the values supplied by the caller in the tm_wday
   and tm_yday fields. mktime() modifies the fields of the tm structure as
   follows: tm_wday and tm_yday are set to values determined from the
   contents of the other fields; if structure members are outside their
   valid interval, they will be normalized (so that, for example, 40
   October is changed into 9 November);*/

// The normalization only seems to work reliably 
// when "tm->year" >= 2 && "tm->yday >= 1" (MacOS X 10.5)
time_t normalize_tm(struct tm *tm) {
    time_t t; 

    if( tm->tm_year<2 || tm->tm_mday<1 )
        throw std::domain_error("normalize_tm: make sure tm->tm_year>=2 AND tm->tm_mday>=1");
    t = ::mktime(tm);
    if (tm->tm_isdst) {
        t  += 60*60;
    }
    ::gmtime_r(&t, tm);
    return t;
}
time_t normalize_tm_gm(struct tm *tm) {
    char*  tz;
    time_t t; 

    if( tm->tm_year<2 || tm->tm_mday<1 )
        throw std::domain_error("normalize_tm: make sure tm->tm_year>=2 AND tm->tm_mday>=1");

    // Make sure the TZ is set to empty (ie GMT/UTC)
    tz = ::getenv("TZ");
    ::setenv("TZ", "", 1);
    ::tzset();
    // Now do the normalization proper
    t = ::mktime(tm);
    ::gmtime_r(&t, tm);
    // And clean up
    if (tz)
        ::setenv("TZ", tz, 1);
    else
        ::unsetenv("TZ");
    ::tzset();
    return t;
}


#if defined( __APPLE__ )
#include <unistd.h>  // for ::sleep()

int clock_nanosleep(clockid_t, int, const struct timespec* ts, struct timespec*) {
    int             sres;
    struct timeval  now;
    struct timespec tosleep, remain;
    // clock_nanosleep gives absolute time (ie sleep until now() == ts) 
    // so to figure out how long we must sleep to arrive there
    ::gettimeofday(&now, 0);
    if( now.tv_sec>ts->tv_sec ||
        (now.tv_sec==ts->tv_sec && now.tv_usec*1000>ts->tv_nsec) )
            // now() is already past where we needed to be
            return 0;
    // Ok we now know we need to sleep - do it with whole seconds first
    while( now.tv_sec<ts->tv_sec ) {
        ::sleep( ts->tv_sec - now.tv_sec );
        ::gettimeofday(&now, 0);
    }
    // Now there's only subsecond amount of sleep to go
    tosleep.tv_sec  = 0;
    tosleep.tv_nsec = ts->tv_nsec * 1000;
    while( (sres=::nanosleep(&tosleep, &remain))==-1 && errno==EINTR ) {
        tosleep.tv_sec  = 0;
        tosleep.tv_nsec = remain.tv_nsec;
    }
    return sres;
}
#endif
