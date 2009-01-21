// very thin wrappers around ::time_t and ::timeval
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
// HV: Wrappers in namespace pcint for ::time_t and ::timeval.
//     The pcint::time_t and pcint::timeval behave like their
//     global counterparts, but they can be properly initialized;
//     the pcint:: stuff got c'tors.
//     Oh yeah, they can be used in systemcalls where a ::time_t
//     or ::timeval is used: automatic conversion is supplied...
//     Also easy 'differencing' using operator- is implemented
#ifndef JIVE5A_TIMEWRAP_H
#define JIVE5A_TIMEWRAP_H

#include <ctime>
// for ::time_t
#include <sys/time.h>
// for modf
#include <math.h>

#include <iostream>


namespace pcint {
	// *very* thin wrapper around the 
	// standard ::time_t value.
	// This default c'tor makes sure
	// the value gets properly initialized
	struct time_t {
		time_t();
		time_t( const ::time_t tm );

		::time_t  timeValue;

		const pcint::time_t& operator=( const ::time_t tm );

		// cast to ::time_t*?
		// [so this thang can be used in standard libc systemcalls :)]
		operator ::time_t*( void );
		operator const ::time_t*( void ) const;
	};

    // Holds a timediff in units of seconds (implicit unit).
    // At least that's what the operator-' return.
	struct timediff {
		timediff();
		timediff( long diff );
		timediff( double diff );

		bool operator==( const timediff& other );
		bool operator!=( const timediff& other );
		
        operator double() const {
            return difference;
        }

		double    difference;
	};
	
	// same for timeval...
	struct timeval {
		typedef enum _when {
			tv_now, tv_yesterday, tv_tomorrow
		} when; // symbolic timevals

        // static member fn which returns current time...
        static pcint::timeval now( void );
		
		// init to zero
		timeval();

		// init symbolic point in time (see above)
		timeval( pcint::timeval::when wh );

		// init from existing timeval
		timeval( const ::timeval& tv );

		// assignment from '::timeval'
		const pcint::timeval& operator=( const ::timeval& tv );

        // add a timediff to ourselves. works with anything
        // that's convertable to 'double'
        template <typename T>
        const pcint::timeval& operator+=( const T& delta ) {
            double   newtime, s;
           
            newtime =  timeValue.tv_sec +
                       timeValue.tv_usec/1.0E6 +
                       (double)delta;

            // and break down into seconds again
            newtime = ::modf( newtime, &s );
            // s holds integral part (full seconds)
            // newtime now holds fractional seconds
            timeValue.tv_sec  = (::time_t)s;
            timeValue.tv_usec = (suseconds_t)(newtime*1.0E6);

            return *this;
        }

        // return a new timeval which is the sum of *this + delta
        template <typename T>
        pcint::timeval operator+( const T& delta ) const {
            pcint::timeval   rv( *this );
            rv += delta;
            return rv;
        }

		// cast to '::timeval*' so this thang can be used in
		// libc systemcalls...
		operator ::timeval*( void );

		// our only member
		struct ::timeval   timeValue;
	};
}

// Comparison operators should not live in
// a namespace (other than the global...)
bool operator<( const pcint::time_t& l, const pcint::time_t& r );
bool operator<( const ::time_t& l, const pcint::time_t& r );
bool operator<( const pcint::time_t& l, const ::time_t& r );

bool operator<=( const pcint::time_t& l, const pcint::time_t& r );
bool operator<=( const ::time_t& l, const pcint::time_t& r );
bool operator<=( const pcint::time_t& l, const ::time_t& r );

bool operator==( const pcint::time_t& l, const pcint::time_t& r );
bool operator==( const ::time_t& l, const pcint::time_t& r );
bool operator==( const pcint::time_t& l, const ::time_t& r );

bool operator>( const pcint::time_t& l, const pcint::time_t& r );
bool operator>( const ::time_t& l, const pcint::time_t& r );
bool operator>( const pcint::time_t& l, const ::time_t& r );

bool operator>=( const pcint::time_t& l, const pcint::time_t& r );
bool operator>=( const ::time_t& l, const pcint::time_t& r );
bool operator>=( const pcint::time_t& l, const ::time_t& r );


// Other global operators
pcint::timediff operator-( const pcint::timeval& l, const pcint::timeval& r );
pcint::timediff operator-( const ::timeval& l, const pcint::timeval& r );
pcint::timediff operator-( const pcint::timeval& l, const ::timeval& r );

// comparison of timediffs
template <typename T>
bool operator<( const pcint::timediff& td, T tm ) {
	return (td.difference<((double)tm));
}

template <typename T>
bool operator<=( const pcint::timediff& td, T tm ) {
	return (td.difference<=((double)tm));
}

template <typename T>
bool operator<( T tm, const pcint::timediff& td ) {
	return ((double)tm < td.difference);
}

template <typename T>
bool operator<=( T tm, const pcint::timediff& td ) {
	return ((double)tm <= td.difference);
}

// comparison of timediffs
template <typename T>
bool operator>( const pcint::timediff& td, T tm ) {
	return (td.difference>((double)tm));
}

template <typename T>
bool operator>=( const pcint::timediff& td, T tm ) {
	return (td.difference>=((double)tm));
}

template <typename T>
bool operator>( T tm, const pcint::timediff& td ) {
	return ((double)tm > td.difference);
}

template <typename T>
bool operator>=( T tm, const pcint::timediff& td ) {
	return ((double)tm >= td.difference);
}

// Output in HRF (both for ::time_t and pcint::time_t and pcint::timediff
std::ostream& operator<<( std::ostream& os, const pcint::time_t& t );
std::ostream& operator<<( std::ostream& os, const pcint::timeval& t );
std::ostream& operator<<( std::ostream& os, const pcint::timediff& t );

#endif
