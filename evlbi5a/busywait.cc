// implementation of the busywait fn
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
#include <busywait.h>
#include <sys/time.h>
#include <time.h>


// trigger initialization of counts-per-usec
unsigned long long int counts_per_usec( void );

static unsigned long long int calib_counts = counts_per_usec();


// the exception
calibrationfail::calibrationfail( const std::string& s):
    __m( std::string("Failed to calibrate busywait - ") + s )
{}

const char* calibrationfail::what( void ) const throw() {
    return __m.c_str();
}

calibrationfail::~calibrationfail() throw()
{}


// function to calibrate the number of "volatile unsigned long long int" ++
// operations to perform in order to waste 1 microsecond of time
unsigned long long int counts_per_usec( void ) {
    // ==0 => uninitialized/uncalibrated
    static unsigned long long int  counts = 0ULL;

    if( counts )
        return counts;

    // do try to calibrate
    const unsigned int              ntries = 10;

    double                          deltat[ntries];
    unsigned long long int          total_n_count;
    unsigned long long int          total_n_microsec;
    unsigned long long int          sval = 100000;
    volatile unsigned long long int counted[ntries];

    

    for( unsigned int i=0; i<ntries; ++i ) {
        struct timeval s, e;

        ::gettimeofday(&s, 0);
        for( counted[i]=0; counted[i]<sval; ++counted[i] ) {};
        ::gettimeofday(&e, 0);

        deltat[i] = ((double)e.tv_sec + (((double)e.tv_usec)/1.0e6)) -
                    ((double)s.tv_sec + (((double)s.tv_usec)/1.0e6));

        // 1) if it was too short: do not compute average and
        //    make 'sval' larger
        // 2) if it took > 1msec, lower sval, but do compute counts/microsecond.
        // 3) otherwise: just get another measure of "number of counts/microsecond"
        if( deltat[i]<=1.0e-6 ) {
            sval      *= 100;
            deltat[i]  = 0.0;
            counted[i] = 0ULL;
        } else if( deltat[i]>=1.0e-3 ) {
            unsigned long long divider;

            // make sure we do not divide by zero OR end up
            // with an sval of 0
            divider = (unsigned long long)(deltat[i]/2000.0);
            if( divider==0 )
                divider = 2;
            if( sval==0 )
                sval = 100;
        }
    }

    // accumulate
    total_n_count    = 0ULL;
    total_n_microsec = 0ULL;
    for( unsigned int i=0; i<ntries; ++i ) {
        unsigned long long int n_microsec = (unsigned long long int)(deltat[i] * 1.0e6);

        if( n_microsec==0 )
            continue;
        total_n_microsec += n_microsec;
        total_n_count    += counted[i];
    }

    if( !total_n_microsec )
        throw calibrationfail("total_n_microsec is 0 in counts_p_usec()!");
    if( !total_n_count )
        throw calibrationfail("total_n_count is 0 in counts_p_usec()!");


    counts = (total_n_count/total_n_microsec);
    if( !counts )
        throw calibrationfail("counts_per_usec is 0 in counts_p_usec()!");
    return counts;
}



// the 'busywait' is nothing but a wrapper ...

// busywait takes a number of microseconds to busywait as argument

// this implementation is "absolute" in the sense that it
// busywaits until the amount of microseconds has indeed passed.
// Note: it is multi-thread-safe. It could be (slightly?) more 'realtime'
// [having the structs as static variables] but then the fn. wouldn't
// be MT-safe anymore.
void busywait( unsigned int n ) {
    double         scur, send;
    struct timeval tmp;

    // based on current time, compute the time at which
    // we should return from this function
    ::gettimeofday(&tmp, 0);
    send = (double)(tmp.tv_sec + ((double)(tmp.tv_usec + n))/1.0e6);

    // and wait for that time to actually arrive
    do {
        ::gettimeofday(&tmp, 0);
        scur = (double)(tmp.tv_sec + ((double)tmp.tv_usec)/1.0e6);
    } while( scur<send );
    return;
}
void busywait_old( unsigned int n ) {
    register const unsigned long long int     cnt = (n*calib_counts);
    register volatile unsigned long long int  i = 0;

    while( i<cnt )
        ++i;
    return;
}
