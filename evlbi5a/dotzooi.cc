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
#include <pthreadcall.h>
#include <threadutil.h>
#include <ioboard.h>
#include <irq5b.h>
#include <errorqueue.h>

#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <iostream>

using std::cout; 
using std::endl;

// Implementation of the dotclock exception
DEFINE_EZEXCEPT(dotclock)

// We keep the current DOT in here. At each 1PPS from
// the Mark5B/DIM we increment it by 1 second
static int                 m5b_fd  = -1;
static pthread_t*          dot_tid = 0;
static pthread_mutex_t     dot_mtx = PTHREAD_MUTEX_INITIALIZER;
static pcint::timeval_type current_dot = pcint::timeval_type();
static pcint::timeval_type last_1pps   = pcint::timeval_type();

// if non-zero it will become the new DOT at the next 1PPS
// we need to keep track of the time it took between requesting
// the DOT and the time when it was actually set
static double              request_dot_honour = 0.0;
static pcint::timeval_type request_dot        = pcint::timeval_type();
static pcint::timeval_type request_dot_issued = pcint::timeval_type();

// Using clock_set it is possible to program the DOT1PPS to be
// != 1 wall-clock second; DOT1PPS's are generated every
// 2*(mk5b_inputmode_type.k+1) mega-clock cycles. The clock generator
// runs at mk5b_inputmode_type.clockfreq mega Hz. The ratio
// between the two determines how often, in wall-clock time, the
// DOT1PPS arrives.
static double               pps_duration     = 1.0;

void pps_waker(int) {}

void* pps_handler(void*) {
    int                   r = 0, pr = 0;     // ioctl() retval and pthread_mutex_(un)lock() retval
    double                d1pps;             // time between 1PPS
    const double          maxdelta = 0.0005; // max deviation from time between PPS from 'pps_duration'
    struct mk5b_intr_info info;              // interrupt info
    struct mk5b_intr_info oldinfo;           // id. for previous one so we can compare

    // Install SIG_USR1 handler - such that we can be woken 
    // from a blocking read
    install_zig_for_this_thread(SIGUSR1);

    // Before falling into our main loop, we grab the mutex.
    // The routine that starts *us* up as a thread locks the mutex before
    // starting us up and unlocks it after the 'm5b_fd' global variable
    // has been filled in. When we get the lock we release it immediately
    // and start strutting our stuff.
    if( (pr=::pthread_mutex_lock(&dot_mtx))!=0 ) {
        DEBUG(-1, "\007\007 WARNING \007\007" << endl << " pps_handler failed to lock startup mutex?!" << endl);
        return (void*)0;
    }
    if( (pr=::pthread_mutex_unlock(&dot_mtx))!=0 ) {
        DEBUG(-1, "\007\007 WARNING \007\007" << endl << " pps_handler failed to unlock startup mutex?!" << endl);
        return (void*)0;
    }

    // Our 'main' loop
    while( true ) {
        // Wait for the interrupt
        if( (r=::ioctl(m5b_fd, MK5B_IOCWAITONIRQ, (void *)&info))!=0 )
            break;
        if( (pr=::pthread_mutex_lock(&dot_mtx))!=0 )
            break;

        // Update the current DOT if 
        //  * this was a DOT1PPS interrupt (bit 12 set in the status)
        //  * the DOT clock has been set
        if( info.soi&0x1000 ) {
            // record time of interrupt
            last_1pps = pcint::timeval_type( info.toi );

            // tick ... tock ... 
            if( current_dot.timeValue.tv_sec )
                current_dot.timeValue.tv_sec++;

            // Was another DOT value requested?
            if( request_dot.timeValue.tv_sec ) {
                current_dot = request_dot;
                // now clear requested dot since we done dat
                request_dot        = pcint::timeval_type();
                request_dot_honour = pcint::timeval_type::now() - request_dot_issued;
            }
        }

        if( (pr=::pthread_mutex_unlock(&dot_mtx))!=0 )
            break;

        // If bit 13 is set this was a DOT1PPSINT_ERR - 
        // at least one interrupt was missed. The register
        // says: 'Set to '1' if a secont DOT1PPSINT arrives
        // before the prior one is cleared'
        if( info.soi&0x2000 ) {
            DEBUG(-1, "\007\007 WARNING \007\007" << endl << " Missed DOT1PPS Interrupts!" << endl);
            if( oldinfo.toi.tv_sec ) {
                DEBUG(-1, "\007\007  Missed " << (info.coi - oldinfo.coi) << " ticks" << endl);
            } else {
                DEBUG(-1, "   Can't tell know how many though since this is the first we caught" << endl);
            }
        }

        // Some generic debug info
        DEBUG(4, "M5B[#" << info.coi << " soi=" << hex_t(info.soi) 
                         << " time=" << pcint::timeval_type(info.toi) << "]" << endl);

        // Compare the previous interrupt arrival time with the current one
        // (if we have a previous one) so we can really warn if it's not
        // close enough to 'pps_duration'!!!

        // Now we finally have a delta 1PPS. Complain loudly and bitterly
        // if we're more than one our allowed maximum off!
        if( oldinfo.toi.tv_sec ) {
            d1pps = ::fabs(last_1pps - pcint::timeval_type(oldinfo.toi));
            if( ::fabs(d1pps-pps_duration)>=maxdelta ) {
                DEBUG(-1, "\n****************************************************\n" <<
                          "WARNING Time between 1PPS outside " << pps_duration << "s +/- " << maxdelta << "s; it's " << d1pps << "s!!!\n" <<
                          "   last_1pps = " << last_1pps << "\n" <<
                          " oldinfo.toi = "  << pcint::timeval_type(oldinfo.toi) << endl <<
                          "****************************************************\n");
            }
        }
        oldinfo = info;
    }
    // If we exit we unconditionally set the DOT back to zero - so anyone
    // should find out quite soon that the DOT has become invalid
    current_dot = pcint::timeval_type();
    if( r!=0 ) {
        DEBUG(0, "FAILED to wait for next mk5b interrupt - " << ::strerror(errno) << endl);
    }
    if( pr!=0 ) {
        DEBUG(0, "FAILED to lock or unlock the DOT mutex" << ::strerror(pr) << endl);
    }
    // and we're done
    return (void*)0;
}


// dotclock_init(ioboard&)
//   only makes sense if running on Mk5B/DIM
//   start a monitoring thread and
//   enables 1PPS interrupts and initializes
//   the dot to current system time
void dotclock_init(ioboard_type& iob) {
    int            fd = -1;
    sigset_t       oss, nss;
    pthread_t*     tmptid = 0;
    pthread_attr_t tattr;

    // Make sure we don't do this twice
    EZASSERT2_ZERO(dot_tid, dotclock, EZINFO("DOT clock already initialized"));
    DEBUG(0, "dotclock_init: setting up Mark5B DOT clock" << endl);

    // (attempt to) open the Mark5B driver
    ASSERT2_POS(fd=::open("/dev/mk5b0", O_RDWR), SCINFO("Failed to open the mark5b driver?!"));

    // Set up for a joinable thread with ALL signals blocked
    ASSERT2_ZERO( sigfillset(&nss), ::close(fd) );
    PTHREAD2_CALL( ::pthread_attr_init(&tattr), ::close(fd) );
    PTHREAD2_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE), ::close(fd) );
    PTHREAD2_CALL( ::pthread_sigmask(SIG_SETMASK, &nss, &oss), ::close(fd) );

    // HV: Lock the dot mutex - the thread will try to lock it too. It will
    // continue after we've unlocked it. This allows *us* to control when 
    // the thread function will actually start reading from 'm5b_fd'; there
    // was a race condition here: pps_handler started to read from m5b_fd
    // before it was filled in. (Observed: Jon Quick/HartRAO, diagnosed:
    // Bob Eldering)
    PTHREAD2_CALL( ::pthread_mutex_lock(&dot_mtx), ::close(fd) );

    // Already start the thread
    tmptid = new pthread_t;
    PTHREAD2_CALL( ::pthread_create(tmptid, &tattr, &pps_handler, 0),
                   ::close(fd); ::pthread_mutex_unlock(&dot_mtx) );

    // good. put back old sigmask + clean up resources
    PTHREAD2_CALL( ::pthread_sigmask(SIG_SETMASK, &oss, 0),
                   ::close(fd); ::pthread_cancel(*tmptid); ::pthread_mutex_unlock(&dot_mtx); delete tmptid);
    PTHREAD2_CALL( ::pthread_attr_destroy(&tattr),
                   ::close(fd); ::pthread_cancel(*tmptid); ::pthread_mutex_unlock(&dot_mtx); delete tmptid);

    // Great! Now that we know everything's being created OK (and cleaned
    // up) we can safely overwrite the global 'm5b_fd' variable and let the
    // PPS handler rip!
    m5b_fd  = fd;
    PTHREAD2_CALL( ::pthread_mutex_unlock(&dot_mtx),
                   ::close(m5b_fd); m5b_fd=-1; ::pthread_cancel(*tmptid); delete tmptid);

    // And enable in the hardware
    iob[mk5breg::DIM_SELDIM] = 0;
    iob[mk5breg::DIM_SELDOT] = 1;

    // Finally remember the thread id of the PPS handler
    dot_tid = tmptid;
    return;
}


void dotclock_cleanup( void ) {
    // If no dot-clock running we're done quickly
    if( !dot_tid )
        return;

    // Close the mk5b filedescriptor and kick the 
    // thread using a SIG_USR1 to get it out
    // of the blocking wait
    ::close(m5b_fd);
    ::pthread_kill(*dot_tid, SIGUSR1);
    ::pthread_join(*dot_tid, 0);
    delete dot_tid;
    m5b_fd = -1;
    dot_tid = 0;
}

// Set the request_dot - it will be set at the next 1PPS
// Return false if the DOT clock isn't running!
bool set_dot(const pcint::timeval_type& dot) {
    pcint::timeval_type  func_entry( pcint::timeval_type::now() );
    bool                 rv;

    // It is an error to set a DOT with a non-zero
    // fractional seconds part
    EZASSERT2(dot.timeValue.tv_usec==0, dotclock, EZINFO("can only set DOT with integer timestamps"));
    PTHREAD_CALL( ::pthread_mutex_lock(&dot_mtx) );
    
    // In fact: only request setting a new DOT if
    // there are interrupts coming!
    if( (rv=(last_1pps.timeValue.tv_sec>0))==true ) {
        request_dot        = dot;
        request_dot_issued = func_entry;
        request_dot_honour = 0.0;
    }
    PTHREAD_CALL( ::pthread_mutex_unlock(&dot_mtx) );
    return rv;
}

double get_set_dot_delay( void ) {
    return request_dot_honour;
}


dot_type::dot_type(const pcint::timeval_type& d, const pcint::timeval_type& l):
    dot(d), lcl(l)
{}

dot_type::operator bool(void) const {
    return (dot.timeValue.tv_sec || dot.timeValue.tv_usec ||
            lcl.timeValue.tv_sec || lcl.timeValue.tv_usec);
}


// Get the current DOT time and the difference between
// DOT and local time
dot_type get_dot( void ) {
    pcint::timeval_type  now( pcint::timeval_type::now() );
    pcint::timeval_type  dot;
    pcint::timeval_type  time_at_last_pps;

    // Make copies of the two important variables
    PTHREAD_CALL( ::pthread_mutex_lock(&dot_mtx) );
    dot              = current_dot;
    time_at_last_pps = last_1pps;
    PTHREAD_CALL( ::pthread_mutex_unlock(&dot_mtx) );

    // In 'dot' we have the DOT value at exactly the last 
    // 1PPS. Now we figure out how long that was ago and
    // add that to the DOT value to get an approximation
    // of what the *current* DOT is
    // Note: if the dot clock isn't running yet return
    //       a WILDLY wrong clock - the operator should
    //       be well aware that time ain't tickin' yet!
    if( dot.timeValue.tv_sec==0 )
        return dot_type( pcint::timeval_type(), now );

    // HV: 25-sep-2013
    // If the time since the last 1PPS is >> the set pps_duration
    // this means that there are no DOT interrupts coming,
    // i.e. the DOT clock is broken. Signal that in return
    // value as well as setting the error and warn on the terminal ...
    if( (now - time_at_last_pps) > 1.1*pps_duration ) {
        // Warn on the terminal
        DEBUG(-1, "\n****************************************************\n" <<
                  "DOT clock seems not to be running - haven't seen a DOT1PPS" <<
                  "interrupt for " << (now-time_at_last_pps) << "s!" <<
                  "DOT1PPS interrupts should happen every " << pps_duration << "s" <<
                  "****************************************************\n");
        // push an error on the error queue
        push_error( error_type(2000, "DOT clock not updating - no DOT1PPS interrupts arriving") );

        // And return with a dot_type with BOTH times set to
        // 1970 Jan 1st, 00:00:00 such that the caller can use
        // that as sentinel to detect this
        return dot_type(pcint::timeval_type(), pcint::timeval_type());
    }

    // Now return our best estimate of what the dot is
    // HV: 14 sep 2013 - Now that we support DOT PPS
    //                   rates not being 1PPS, we must
    //                   account for that in this
    //                   computation; we must compensate
    //                   by a factor "wallclock / pps_length"
    return dot_type((dot + (now - time_at_last_pps)/pps_duration), now);
}


// Only increment dot clock if the dot clock is running
bool inc_dot(int nsec) {
    bool    retval;
    pcint::timeval_type  prev_dot, new_dot;
    PTHREAD_CALL( ::pthread_mutex_lock(&dot_mtx) );
    prev_dot = current_dot;
    if( (retval=(current_dot.timeValue.tv_sec!=0))==true )
        current_dot.timeValue.tv_sec += nsec;
    new_dot  = current_dot;
    PTHREAD_CALL( ::pthread_mutex_unlock(&dot_mtx) );
    DEBUG(4, "inc_dot/" << nsec << "s = " << prev_dot << " -> " << new_dot << endl);
    return retval;
}

// Set the length of a DOT PPS in units of wall-clock seconds
void set_pps_length(double ppsl) {
    pps_duration = ppsl;
    DEBUG(-1, "Set 1PPS length to " << pps_duration << " wall clock seconds" << endl);
}


template <typename T>
const char* format_s(T*) {
    EZASSERT2( false, dotclock,
               EZINFO("Attempt to use an undefined formatting generator") );
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

        EZASSERT2_NZERO( cpy, dotclock, EZINFO("Failed to duplicate string") );

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
                EZASSERT2( nxt==0, dotclock,
                           EZINFO("Timeformat fields not in strict sequence");
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
        EZASSERT2( doy_cvt==true, dotclock, EZINFO("Failed to convert Day-Of-Year " << doy));
        result.tm_mon  = month;
        result.tm_mday = daymonth + 1;

        fields_parsed++;
    }

    if( hh!=not_given ) {
        EZASSERT2( hh<=23, dotclock, EZINFO("Hourvalue " << hh << " out of range") );
        result.tm_hour = hh;
        fields_parsed++;
    }
    if( mm!=not_given ) {
        EZASSERT2( mm<=59, dotclock, EZINFO("Minutevalue " << mm << " out of range") );
        result.tm_min = mm;
        fields_parsed++;
    }
    if( ((int)ss)!=not_given ) {
        EZASSERT2( ss<60, dotclock, EZINFO("Secondsvalue " << ss << " out of range") );
        microseconds = (unsigned int)round( modf(ss, &ss) * 1000000 );
        result.tm_sec = (unsigned int)round(ss);
        fields_parsed++;
    }
    return fields_parsed;

}

unsigned int seconds_in_year(struct tm& tm) {
    int daynr;

    // tm_mon is zero based, tm_mday is one based, DayConversion is all zero based
    EZASSERT( DayConversion::dayMonthDayToNr(daynr, tm.tm_mon, tm.tm_mday - 1, tm.tm_year + 1900), dotclock );
    EZASSERT( daynr >= 0, dotclock );
    
    return (unsigned int)daynr * (unsigned int)DayConversion::secondsPerDay + 
        (((unsigned int)tm.tm_hour * 60) + (unsigned int)tm.tm_min) * 60 + (unsigned int)tm.tm_sec;

}
