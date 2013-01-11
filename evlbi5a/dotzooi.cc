// 'implementation' of the functions found in dotzooi.h
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
#include <dotzooi.h>
#include <evlbidebug.h>
#include <dayconversion.h>
#include <dosyscall.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// We only keep delta(DOT, system) as that's all we need to know.
// by default it's zero, no difference :)
static pcint::timediff  delta_dot_system;

// map system and a dot time to each other
// this amount, currently, to not much more than
// storing the offset ;)
void bind_dot_to_local( const pcint::timeval_type& dot,
                        const pcint::timeval_type& sys ) {
    delta_dot_system = dot - sys;
    DEBUG(4 , "bind_dot_to_locl:\n" <<
              "  dot=" << dot << "\n"
              "  sys=" << sys << "\n"
              " => delta=" << delta_dot_system << "\n" );
    return;
}

pcint::timeval_type local2dot( const pcint::timeval_type& lcl ) {
    DEBUG(4, "local2dot(" << lcl << "), adding " << delta_dot_system << std::endl);
    return (lcl + delta_dot_system);
}

void inc_dot(int nsec) {
    pcint::timediff   old_delta = delta_dot_system;

    delta_dot_system = pcint::timediff( (double)old_delta + (double)nsec );
    DEBUG(4, "inc_dot/" << nsec << "s = " << old_delta << " -> " << delta_dot_system << std::endl);
    return;
}

template <typename T>
const char* format_s(T*) {
    ASSERT2_COND( false,
                  SCINFO("Attempt to use an undefined formatting generator") );
    return 0;
}
template <>
const char* format_s(int*) {
    return "%d%c";
}
template <>
const char* format_s(unsigned int*) {
    return "%u%c";
}
template <>
const char* format_s(double*) {
    return "%lf%c";
}

// Function template for a function returning the location
// of a temporary variable of type T
template <typename T>
void* temporary(T*) {
    static T d;
    return &d;
}

struct fld_type {
    // fmt: pointer to format, two conversions of which last one MUST be %c
    // sep: separator character to be expected after scan into %c
    // vptr: pointer to the value where the scanned value will be stored 
    //       (if successfull)
    // tptr: pointer where the value will initially be scanned into, will
    //       only be transferred to vptr iff the scan was completely
    //       successfull
    // sz:    size of the value that is scanned
    const char*   fmt;
    const char    sep;
    void*         vptr;
    void*         tptr;
    unsigned int  sz;

    fld_type():
        fmt( 0 ), sep( '\0' ), vptr(0), tptr(0), sz(0)
    {}

    // templated constructor, at least gives _some_ degree of
    // typesafety (yeah, very shallow, I knows0rz)
    //
    // Only pass it the character you expect after the value
    // and a pointer to the location where to store the
    // (only if succesfully!) decoded value
    template <typename T>
    fld_type(char _sep, T* valueptr) :
        fmt( format_s(valueptr) ), // get the formatting string for T
        sep( _sep ),
        vptr( valueptr ),
        tptr( temporary(valueptr) ), // get a pointer-to-temp-T
        sz( sizeof(T) )
    {}
    // Returns true if the format scan returns 2 AND
    // the scanned character matches 'sep'
    // Only overwrites the value pointed at by vptr iff returns true.
    bool operator()( const char* s ) const {
        char c;
        bool rv;

        // Scan value into "tmpptr" and character into "c",
        // iff everything matches up, transfer scanned value
        // from tmpptr to valueptr
        if( (rv = (s && ::sscanf(s, fmt, tptr, &c)==2 && c==sep))==true )
            ::memcpy(vptr, tptr, sz);
        return rv;
    }
};

unsigned int parse_vex_time( std::string time_text,  struct ::tm& result, unsigned int& microseconds ) {
    // translate to pcint::timeval_type ...
    const int          not_given( -1 );
    // reserve space for the parts the user _might_ give
    int                year( not_given );
    int                doy( not_given );
    int                hh( not_given );
    int                mm( not_given );
    double             ss( not_given );

    unsigned int fields_parsed = 0;

    // the timefields we recognize.
    // Note: this order is important (it defines the
    //  order in which the field(s) may appear)
    // Note: leave the empty fld_type() as last entry -
    //  it signals the end of the list.
    // Note: a field can _only_ read a single value.
    const fld_type     fields[] = {
        fld_type('y', &year),
        fld_type('d', &doy),
        fld_type('h', &hh),
        fld_type('m', &mm),
        fld_type('s', &ss),
        fld_type()
    };

    // now go on and see what we can dig up
    {
        const char*     ptr;
        const char*     cpy = ::strdup( time_text.c_str() );
        const fld_type* cur, *nxt;

        ASSERT2_NZERO( cpy, SCINFO("Failed to duplicate string") );

        for( cur=fields, ptr=cpy, nxt=0; cur->fmt!=0; cur++ ) {
            // attempt to convert the current field at the current
            // position in the string. Also check the separating
            // character
            if( ptr && (*cur)(ptr) ) {
                // This is never an error, as long as decoding goes
                // succesfully.
                if( (ptr=::strchr(ptr, cur->sep))!=0 )
                    ptr++;
                nxt = cur+1;
            } else {
                // nope, this field did not decode
                // This is only not an error if we haven't started
                // decoding _yet_. We can detect if we have started
                // decoding by inspecting nxt. If it is nonzero,
                // decoding has started
                ASSERT2_COND( nxt==0,
                              SCINFO("Timeformat fields not in strict sequence");
                              ::free((void*)cpy) );
            }
        }
        ::free( (void*)cpy );
    }
    // done parsing user input
    // Now take over the values that were specified
    if( year!=not_given ) {
        result.tm_year = (year-1900);
        fields_parsed++;
    }
    // translate doy [day-of-year] to month/day-in-month
    if( doy!=not_given ) {
        int   month, daymonth;
        bool  doy_cvt;

        // tm_mon is zero based, tm_mday is one based, given doy is one based
        // DayConversion is all zero based
        doy_cvt = DayConversion::dayNrToMonthDay(month, daymonth,
                                                 doy - 1, result.tm_year+1900);
        ASSERT2_COND( doy_cvt==true, SCINFO("Failed to convert Day-Of-Year " << doy));
        result.tm_mon  = month;
        result.tm_mday = daymonth + 1;

        fields_parsed++;
    }

    if( hh!=not_given ) {
        ASSERT2_COND( hh<=23, SCINFO("Hourvalue " << hh << " out of range") );
        result.tm_hour = hh;
        fields_parsed++;
    }
    if( mm!=not_given ) {
        ASSERT2_COND( mm<=59, SCINFO("Minutevalue " << mm << " out of range") );
        result.tm_min = mm;
        fields_parsed++;
    }
    if( ((int)ss)!=not_given ) {
        ASSERT2_COND( ss<60, SCINFO("Secondsvalue " << ss << " out of range") );
        microseconds = (unsigned int)round( modf(ss, &ss) * 1000000 );
        result.tm_sec = (unsigned int)round(ss);
        fields_parsed++;
    }
    return fields_parsed;

}

unsigned int seconds_in_year(struct tm& tm) {
    int daynr;

    // tm_mon is zero based, tm_mday is one based, DayConversion is all zero based
    ASSERT_COND( DayConversion::dayMonthDayToNr(daynr, tm.tm_mon, tm.tm_mday - 1, tm.tm_year + 1900) );
    ASSERT_COND( daynr >= 0 );
    
    return (unsigned int)daynr * (unsigned int)DayConversion::secondsPerDay + 
        (((unsigned int)tm.tm_hour * 60) + (unsigned int)tm.tm_min) * 60 + (unsigned int)tm.tm_sec;

}
