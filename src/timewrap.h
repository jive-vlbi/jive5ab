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
//     The pcint::time_type and pcint::timeval_type behave like their
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


struct ::timespec ts_now( void );

bool operator==(const struct ::timespec& l, const struct ::timespec& r);
bool operator!=(const struct ::timespec& l, const struct ::timespec& r);

namespace pcint {
	// *very* thin wrapper around the 
	// standard ::time_t value.
	// This default c'tor makes sure
	// the value gets properly initialized
	struct time_type {
		explicit time_type();
		explicit time_type( const ::time_t tm );

		::time_t  timeValue;

		const time_type& operator=( const ::time_t tm );

		// cast to ::time_t*?
		// [so this thang can be used in standard libc systemcalls :)]
		operator ::time_t*( void );
		operator const ::time_t*( void ) const;
	};

    // Holds a timediff in units of seconds (implicit unit).
    // At least that's what the operator- returns.
	struct timediff {
		explicit timediff();
		explicit timediff( long diff );
		explicit timediff( double diff );

		bool operator==( const timediff& other );
		bool operator!=( const timediff& other );
		
        operator double() const {
            return difference;
        }

		double    difference;
	};
	
	// same for timeval...
	struct timeval_type {
		typedef enum _when {
			tv_now, tv_yesterday, tv_tomorrow
		} when; // symbolic timevals

        // static member fn which returns current time...
        static timeval_type now( void );
		
		// init to zero
		explicit timeval_type();

		// init symbolic point in time (see above)
		timeval_type( timeval_type::when wh );

		// init from existing timeval
		explicit timeval_type( const ::timeval& tv );

		// assignment from '::timeval'
		const timeval_type& operator=( const ::timeval& tv );

        // add a timediff to ourselves. works with anything
        // that's convertable to 'double'
        template <typename T>
        const timeval_type& operator+=( const T& delta ) {
            double   delta_int, delta_frac;

            delta_frac = ::modf( (double)delta, &delta_int);

            int total_usec = (int)timeValue.tv_usec + (int)(delta_frac*1.0E6);

            if (total_usec > 1000000) {
                delta_int += 1;
                total_usec -= 1000000;
            }
            else if (total_usec < 0) {
                delta_int -= 1;
                total_usec += 1000000;
            }
            
            timeValue.tv_sec += (time_t)delta_int;
            timeValue.tv_usec = (suseconds_t)(total_usec);
            
            return *this;
        }

        // return a new timeval_type which is the sum of *this + delta
        template <typename T>
        timeval_type operator+( const T& delta ) const {
            timeval_type   rv( *this );
            rv += delta;
            return rv;
        }

		// cast to '::timeval*' so this thang can be used in
		// libc systemcalls...
		operator ::timeval*( void );

		// our only member
		struct ::timeval   timeValue;
	};
    // global comparison operators should live in
    // the namespace where (one of) their operands
    // is defined
    bool operator<( const time_type& l, const time_type& r );
    bool operator<( const ::time_t& l, const time_type& r );
    bool operator<( const time_type& l, const ::time_t& r );

    bool operator<=( const time_type& l, const time_type& r );
    bool operator<=( const ::time_t& l, const time_type& r );
    bool operator<=( const time_type& l, const ::time_t& r );

    bool operator==( const time_type& l, const time_type& r );
    bool operator==( const ::time_t& l, const time_type& r );
    bool operator==( const time_type& l, const ::time_t& r );

    bool operator>( const time_type& l, const time_type& r );
    bool operator>( const ::time_t& l, const time_type& r );
    bool operator>( const time_type& l, const ::time_t& r );

    bool operator>=( const time_type& l, const time_type& r );
    bool operator>=( const ::time_t& l, const time_type& r );
    bool operator>=( const time_type& l, const ::time_t& r );

    bool operator<(const timeval_type& l, const timeval_type& r);


    // Other global operators
    timediff operator-( const timeval_type& l, const timeval_type& r );
    timediff operator-( const ::timeval& l, const timeval_type& r );
    timediff operator-( const timeval_type& l, const ::timeval& r );
    timediff operator-( int   i, timediff const& other);


    // comparison of timediffs
    template <typename T>
        bool operator<( const timediff& td, T tm ) {
            return (td.difference<((double)tm));
        }

    template <typename T>
        bool operator<=( const timediff& td, T tm ) {
            return (td.difference<=((double)tm));
        }

    template <typename T>
        bool operator<( T tm, const timediff& td ) {
            return ((double)tm < td.difference);
        }

    template <typename T>
        bool operator<=( T tm, const timediff& td ) {
            return ((double)tm <= td.difference);
        }

    // comparison of timediffs
    template <typename T>
        bool operator>( const timediff& td, T tm ) {
            return (td.difference>((double)tm));
        }

    template <typename T>
        bool operator>=( const timediff& td, T tm ) {
            return (td.difference>=((double)tm));
        }

    template <typename T>
        bool operator>( T tm, const timediff& td ) {
            return ((double)tm > td.difference);
        }

    template <typename T>
        bool operator>=( T tm, const timediff& td ) {
            return ((double)tm >= td.difference);
        }

    // Output in HRF (both for ::time_t and time_type and timediff
    std::ostream& operator<<( std::ostream& os, const time_type& t );
    std::ostream& operator<<( std::ostream& os, const timeval_type& t );
    std::ostream& operator<<( std::ostream& os, const timediff& t );
}


#endif
