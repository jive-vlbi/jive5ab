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
//
// Revision 1.1  2004/11/03 21:58:43  verkout
// HV: Wrappers in namespace pcint for ::time_t and ::timeval.
//     The pcint::time_t and pcint::timeval behave like their
//     global counterparts, but they can be properly initialized;
//     the pcint:: stuff got c'tors.
//     Oh yeah, they can be used in systemcalls where a ::time_t
//     or ::timeval is used: automatic conversion is supplied...
//
//
#include <timewrap.h>
#include <string>
#include <math.h>
#include <stdio.h>
#include <streamutil.h>

struct ::timespec ts_now( void ) {
    struct ::timeval  tv;
    struct ::timespec ts;
    ::gettimeofday(&tv, 0);
    ts.tv_sec  = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000;
    return ts;
}

bool operator==(const struct ::timespec& l, const struct ::timespec& r) {
    return l.tv_sec==r.tv_sec && l.tv_nsec==r.tv_nsec;
}
bool operator!=(const struct ::timespec& l, const struct ::timespec& r) {
    return !(l==r);
}


namespace pcint {
	time_type::time_type():
        timeValue( ::time_t(0) )
	{}

	time_type::time_type( const ::time_t tm ) :
		timeValue( tm )
	{}

	const time_type& time_type::operator=( const ::time_t tm ) {
		timeValue = tm;
		return *this;
	}

	time_type::operator ::time_t*( void ) {
		return &timeValue;
	}
	time_type::operator const ::time_t*( void ) const {
		return &timeValue;
	}

	// the timediff thingy
	timediff::timediff() :
		difference( 0.0 )
	{}

	timediff::timediff( long diff ) :
		difference( (double)diff )
	{}

	timediff::timediff( double diff ):
		difference( diff )
	{}

	bool timediff::operator==( const timediff& other ) {
        return ::fabs(difference - other.difference)<=1.0e-13;
	}

	bool timediff::operator!=( const timediff& other ) {
		return (!this->operator==(other));
	}

	
	// the timeval_type thingy
	timeval_type::timeval_type() {
		timeValue.tv_sec  = 0;
		timeValue.tv_usec = 0;
	}

    // return current o/s time
    timeval_type timeval_type::now( void ) {
        return timeval_type( timeval_type::tv_now );
    }

	timeval_type::timeval_type( timeval_type::when wh ) {
		::gettimeofday( &timeValue, 0 );
		switch( wh ) {
			case timeval_type::tv_now:
				// nothing to do
				break;

			case timeval_type::tv_yesterday:
				timeValue.tv_sec -= (24*60*60);
				break;

			case timeval_type::tv_tomorrow:
				timeValue.tv_sec += (24*60*60);
				break;

			default:
				throw ("Enumerated value out-of-range in "
                        "timeval_type::timeval_type( enum timeval_type::when )");
				break;
		}
	}

	timeval_type::timeval_type( const ::timeval& tv ):
		timeValue( tv )
	{}

	const timeval_type& timeval_type::operator=( const ::timeval& tv ) {
		timeValue = tv;
		return *this;
	}

	timeval_type::operator ::timeval*( void ) {
		return &timeValue;
	}

    // Global operators

    // all permutations of operator<  (less than)
    bool operator<( const time_type& l, const time_type& r ) {
        return (l.timeValue<r.timeValue);
    }
    bool operator<( const ::time_t& l, const time_type& r ) {
        return (l<r.timeValue);
    }
    bool operator<( const time_type& l, const ::time_t& r ) {
        return (l.timeValue<r);
    }

    // all permutations of operator<=  (less than or equal)
    bool operator<=( const time_type& l, const time_type& r ) {
        return (l.timeValue<=r.timeValue);
    }
    bool operator<=( const ::time_t& l, const time_type& r ) {
        return (l<=r.timeValue);
    }
    bool operator<=( const time_type& l, const ::time_t& r ) {
        return (l.timeValue<=r);
    }

    // all permutations of operator==  (equal)
    bool operator==( const time_type& l, const time_type& r ) {
        return (l.timeValue==r.timeValue);
    }
    bool operator==( const ::time_t& l, const time_type& r ) {
        return (l==r.timeValue);
    }
    bool operator==( const time_type& l, const ::time_t& r ) {
        return (l.timeValue==r);
    }

    // all permutations of operator>  (greater than)
    bool operator>( const time_type& l, const time_type& r ) {
        return (l.timeValue>r.timeValue);
    }
    bool operator>( const ::time_t& l, const time_type& r ) {
        return (l>r.timeValue);
    }
    bool operator>( const time_type& l, const ::time_t& r ) {
        return (l.timeValue>r);
    }

    // all permutations of operator>=  (greater than or equal)
    bool operator>=( const time_type& l, const time_type& r ) {
        return (l.timeValue>=r.timeValue);
    }
    bool operator>=( const ::time_t& l, const time_type& r ) {
        return (l>=r.timeValue);
    }
    bool operator>=( const time_type& l, const ::time_t& r ) {
        return (l.timeValue>=r);
    }


    timediff operator-( const timeval_type& l, const timeval_type& r ) {
        double  lsec, rsec;

        lsec = l.timeValue.tv_sec + l.timeValue.tv_usec/1E6;
        rsec = r.timeValue.tv_sec + r.timeValue.tv_usec/1E6;

        return timediff(lsec-rsec);
    }
    timediff operator-( const ::timeval& l, const timeval_type& r ) {
        double  lsec, rsec;

        lsec = l.tv_sec + l.tv_usec/1E6;
        rsec = r.timeValue.tv_sec + r.timeValue.tv_usec/1E6;

        return timediff(lsec-rsec);
    }
    timediff operator-( const timeval_type& l, const ::timeval& r ) {
        double  lsec, rsec;

        lsec = l.timeValue.tv_sec + l.timeValue.tv_usec/1E6;
        rsec = r.tv_sec + r.tv_usec/1E6;

        return timediff(lsec-rsec);
    }
    timediff operator-(int i, timediff const& other) {
        return timediff( (double)i - other.difference );
    }

    bool operator<(const timeval_type& l, const timeval_type& r) {
        return (l.timeValue.tv_sec<r.timeValue.tv_sec ||
                (l.timeValue.tv_sec==r.timeValue.tv_sec && 
                 l.timeValue.tv_usec<r.timeValue.tv_usec));
    }




    // Output in HRF...
    std::ostream& operator<<( std::ostream& os, const time_type& t ) {
        char      buff[32];
        struct tm tmpt;
        ::gmtime_r(&t.timeValue, &tmpt);
        ::strftime(buff, sizeof(buff), "%jd%Hh%Mm%Ss", &tmpt);
        return os << buff;
    }

    // higher precision time
    std::ostream& operator<<( std::ostream& os, const timeval_type& t ) {
        ::time_t      tmpt;
        const double  secs  = (t.timeValue.tv_sec);
        const double  usecs = (t.timeValue.tv_usec/1.0E6);
        static double dummy;

        tmpt = (::time_t)(secs + usecs);

        char      buff[32];
        char      sbuff[32];
        struct tm tmp_tm;
        ::gmtime_r(&tmpt, &tmp_tm);
        ::strftime(buff, sizeof(buff), "%Yy%jd%Hh%Mm", &tmp_tm);
        // append the seconds+fractional seconds
        ::snprintf(sbuff, sizeof(sbuff), "%07.4fs", ((double)tmp_tm.tm_sec+::modf(usecs,&dummy)));
        return os << buff << sbuff;
    }

    std::ostream& operator<<( std::ostream& os, const timediff& t ) {
        static const double    sec_per_min  = 60.0;
        static const double    sec_per_hour = 60.0*sec_per_min;
        static const double    sec_per_day  = 24.0*sec_per_hour;
        static const double    sec_per_year = 365.25*sec_per_day;

        double    nsec = t.difference;
        double    nyr, nday, nhr, nmin, ns;

        nyr    = nsec/sec_per_year;
        ::modf( nyr, &nyr );

        nday   = (nsec - (nyr*sec_per_year))/sec_per_day;
        ::modf( nday, &nday );

        nhr   = (nsec - (nyr*sec_per_year) - (nday*sec_per_day))/sec_per_hour;
        ::modf( nhr, &nhr );

        nmin  = (nsec - (nyr*sec_per_year) - (nday*sec_per_day) - (nhr*sec_per_hour))/sec_per_min;
        ::modf( nmin, &nmin );

        ns    = (nsec - (nyr*sec_per_year) - (nday*sec_per_day) - (nhr*sec_per_hour) - (nmin*sec_per_min));

        if( ::fabs(nyr)>1.0e-13 )
            os << nyr << "yr ";
        if( ::fabs(nday)>1.0e-13 )
            os << nday << "d ";
        if( ::fabs(nhr)>1.0e-13 )
            os << nhr << "h ";
        if( ::fabs(nmin)>1.0e-13 )
            os << nmin << "m ";
        os << format("%8.5lf",ns) << "s";

        return os;
    }

// end of namespace
}
