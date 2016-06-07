#include <highrestime.h>
#include <exception>
#include <ctime>             // for struct tm
#include <cstdio>            // for ::snprintf()
#include <cmath>             // for ::floor()

using namespace std;


template <typename T>
double ASDBL(T const& t) {
    return boost::rational_cast<double>(t);
}


///////////////////////////////////////////////////
//
//  the time stamp
//
///////////////////////////////////////////////////
highrestime_type::highrestime_type():
    tv_sec( 0 ), tv_subsecond(0, 1)
{}

// Clock ticking at one tick / second
highrestime_type::highrestime_type(time_t time):
    tv_sec( time ), tv_subsecond(0, 1)
{}

highrestime_type::highrestime_type(time_t time, const subsecond_type& ss):
    tv_sec( time ), tv_subsecond( ss )
{ normalize(); }

// Id. but now ticking at 10^6 ticks/second
highrestime_type::highrestime_type(const struct timeval& tv):
    tv_sec( tv.tv_sec ), tv_subsecond(tv.tv_usec, 1000000ULL)
{ normalize(); }

// Likewise, but now ticking at 10^9 ticks/second
highrestime_type::highrestime_type(const struct timespec& ts):
    tv_sec( ts.tv_sec ), tv_subsecond(ts.tv_nsec, 1000000000ULL)
{ normalize(); }


// 'ntick' ticks of a clock that ticks at 'tick' ticks per 'period' seconds
highrestime_type::highrestime_type(time_t nsec, uint64_t ntick, uint64_t ticks, uint64_t period):
    tv_sec( nsec ), tv_subsecond(ntick * period, ticks)
{
    if( !ticks )
        throw std::invalid_argument("Clock ticking at 0 ticks/second is invalid");
    normalize();
}


void highrestime_type::normalize( void ) {
    uint64_t    nsec = tv_subsecond.numerator()/tv_subsecond.denominator();
    if( nsec ) {
        tv_sec       += nsec;
        tv_subsecond -= nsec;
    }
}

double highrestime_type::as_double( void ) const {
    return (double)tv_sec + ASDBL(tv_subsecond);
}

double highrestime_type::subsecond( void ) const {
    return ASDBL(tv_subsecond);
}

// Add a delta to the current time stamp
const highrestime_type& highrestime_type::operator+=(const highresdelta_type& dt) {
    // are we adding a negative number?
    if( dt<0 ) {
        // Compute if we can safely subtract or wether we have to borrow
        // from our integer second value in order to safely subtract 
        // the delta
        const int64_t nsec = (int64_t)::floor( ASDBL(tv_subsecond) - ::fabs(ASDBL(dt)) );

        tv_sec       -= ::abs( nsec );
        tv_subsecond += ::abs( nsec );

        // Now we must do the subtraction
        tv_subsecond -= subsecond_type( (uint64_t)::abs(dt.numerator()),
                                        (uint64_t)::abs(dt.denominator()) );
    } else {
        tv_subsecond += subsecond_type( (uint64_t)dt.numerator(), 
                                        (uint64_t)dt.denominator() );
    }
    normalize();
    return *this;
}
const highrestime_type& highrestime_type::operator-=(const highresdelta_type& dt) {
    // subtraction is negative addition :-)
    this->operator+=( -dt );
    return *this;
}


///////////////////////////////////////////////////
//
//  Operators
//
///////////////////////////////////////////////////

std::ostream& operator<<(std::ostream& os, const highrestime_type& hrt) {
    char        buffer[64];
    size_t      nchar;
    struct tm   gmt;
    // break down into time fields
    ::gmtime_r(&hrt.tv_sec, &gmt);

    // start with basic format
    if( (nchar=::strftime(buffer, sizeof(buffer), "%F %H:%M:", &gmt))==0 )
        throw runtime_error("operator<<(): local buffer for ::strftime() was not large enough");

    // Depending on if we know the subsecond value or not, print it or not
    if( hrt.tv_subsecond==-1 ) {
        if( ::snprintf(buffer+nchar, sizeof(buffer)-nchar, "%02d.***", gmt.tm_sec)>=(int)(sizeof(buffer)-nchar) )
            throw runtime_error("operator<<(): local buffer for ::strftime()+::snprintf() [unknown subsecond] was not large enough");
    } else {
        if( ::snprintf(buffer+nchar, sizeof(buffer)-nchar, "%09.6f", gmt.tm_sec + hrt.subsecond())>=(int)(sizeof(buffer)-nchar) )
            throw runtime_error("operator<<(): local buffer for ::strftime()+::snprintf() was not large enough");
    }
    return os << buffer;
}

string tm2vex(const highrestime_type& hrt) {
    char        buffer[64];
    size_t      nchar;
    struct tm   gmt;

    // break down into time fields
    ::gmtime_r(&hrt.tv_sec, &gmt);
    // start with basic format
    if( hrt.tv_subsecond!=-1 )
        nchar = ::snprintf( buffer, sizeof(buffer), "%04dy%03dd%02dh%02dm%07.4fs",
                            gmt.tm_year+1900, gmt.tm_yday+1, gmt.tm_hour, gmt.tm_min, gmt.tm_sec + hrt.subsecond() );
    else
        nchar = ::snprintf( buffer, sizeof(buffer), "%04dy%03dd%02dh%02dm%02d.***s",
                            gmt.tm_year+1900, gmt.tm_yday+1, gmt.tm_hour, gmt.tm_min, gmt.tm_sec );
    if( nchar>=sizeof(buffer) )
        throw runtime_error("tm2vex: local buffer for ::snprintf() was not large enough");
    return string(buffer);
}


highrestime_type operator+(const highrestime_type& l, const highresdelta_type& dt) {
    highrestime_type    rv( l );

    rv += dt;
    return rv;
}

highrestime_type operator-(const highrestime_type& l, const highresdelta_type& dt) {
    highrestime_type  rv( l );
    
    rv -= dt;
    return rv;
}


highresdelta_type operator-(const highrestime_type& l, const highrestime_type& r) {
    // Our subsecond fields are of the unsigned persuasion.
    // We'll still just do the unsigned subtraction and then just tell the 
    // compilert that we're going to interpret the result as signed. 
    // This works because of 2's complement arithmetic.
    const subsecond_type    ssdiff( l.tv_subsecond - r.tv_subsecond );

    return highresdelta_type( l.tv_sec - r.tv_sec ) + /* integer second diff */
           highresdelta_type( (int64_t)ssdiff.numerator(), (int64_t)ssdiff.denominator() );  /* subsecond diff */
}


bool operator==(const highrestime_type& l, const highrestime_type& r) {
    return l.tv_sec == r.tv_sec && l.tv_subsecond==r.tv_subsecond;
}
bool operator!=(const highrestime_type& l, const highrestime_type& r) {
    return !(l==r);
}

bool operator<(const highrestime_type& l, const highrestime_type& r) {
    if( l.tv_sec==r.tv_sec )
        return l.tv_subsecond<r.tv_subsecond;
    return l.tv_sec<r.tv_sec;
}

// Assume 'dt' is the result of "highrestime_type(a) - highrestime_type(b)",
// i.e. a high time resolution time difference.
// 'frameduration' is fractional seconds.
bool isModulo(const highrestime_type& dt, const boost::rational<uint64_t>& frameduration) {
    return ((dt.tv_sec + dt.tv_subsecond)/frameduration).denominator()==1;
}
