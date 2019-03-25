// For correct data frame time stamping we need high-resolution time stamp

#ifndef EVLBI5A_HIGHRESTIME_H
#define EVLBI5A_HIGHRESTIME_H

#include <iostream>
#include <time.h>
#include <sys/time.h>          // for time_t
#include <inttypes.h>          // for int64_t
#include <boost/rational.hpp>  // for rational numbers


typedef boost::rational<uint64_t> subsecond_type;
typedef boost::rational<int64_t>  highresdelta_type;


// Describes a point in time
struct highrestime_type {
    // Define 'magick' values to explicitly signal that we have no
    // subsecond time information and a double representation of that.
    // Currently: subsecond_type        = numeric_limits<uint64_t>::max()
    //            double representation = numeric_limits<double>::epsilon()
    // But see highrestime.cc for actual defn
    static const subsecond_type  UNKNOWN_SUBSECOND;
    static const double          UNKNOWN_SUBSECOND_DBL;
   
    highrestime_type();
   
    // construct from commonly used time structs

    // clocks ticking at one tick per second
    highrestime_type(time_t sec);
    highrestime_type(time_t sec, const subsecond_type& ss);

    // clock ticking at 1000000 ticks per second
    highrestime_type(const struct timeval& tv);

    // clock ticking at 1000000000 ticks per second
    highrestime_type(const struct timespec& ts);

    // 'ntick' ticks of a clock that ticks at 'tick' ticks per 'period' seconds
    highrestime_type(time_t sec, uint64_t ntick, uint64_t ticks, uint64_t period=1);

    time_t           tv_sec;
    subsecond_type   tv_subsecond;  // fractional seconds as a rational number

    const highrestime_type& operator+=( const highresdelta_type& dt );
    const highrestime_type& operator-=( const highresdelta_type& dt );

    void   normalize( void );
    double as_double( void ) const; // whole time stamp as double, in units of seconds
    double subsecond( void ) const; // only the subseconds
};


// Arithmetic on time stamps can only be done with deltas
highrestime_type  operator+(const highrestime_type& l, const highresdelta_type& dt);
highrestime_type  operator-(const highrestime_type& l, const highresdelta_type& dt);

// You can only get a delta from subtracting two time stamps (or construct one
// manually)
highresdelta_type operator-(const highrestime_type& l, const highrestime_type& r);


bool operator==(const highrestime_type& l, const highrestime_type& r);
bool operator!=(const highrestime_type& l, const highrestime_type& r);
bool operator<(const highrestime_type& l, const highrestime_type& r);

// Assume 'dt' is the result of "highrestime_type(a) - highrestime_type(b)",
// i.e. a high time resolution time difference.
// 'frameduration' is fractional seconds.
bool isModulo(const highrestime_type& dt, const subsecond_type& frameduration);

std::ostream& operator<<(std::ostream& os, const highrestime_type& hrt);

std::string tm2vex(const highrestime_type& hrt);


#endif
