// implementation
// Copyright (C) 2007-2013 Harro Verkouter
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
#include <threadutil.h>
#include <mutex_locker.h>

#include <stdlib.h>   // for ::free(), ::srandom_r, ::initstate_r
#include <string.h>   // for ::strerror_r()
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <netdb.h>
#include <dosyscall.h>

#include <stdexcept>
#include <iostream>

// dummy empty function meant to install as signal-handler
// that doesn't actually do anything.
// it is necessary to be able to, under linux, wake up
// a thread from a blocking systemcall: send it an explicit
// signal. in order to limit sideeffects we use SIGUSR1
// and install a signal handler "dat don't do nuttin'"
void zig_func(int) {}

void install_zig_for_this_thread(int sig) {
    sigset_t         set;
    struct sigaction act;

    // Unblock the indicated SIGNAL from the set of all signals
    sigfillset(&set);
    sigdelset(&set, sig);

    // install the empty handler 'zig()' for this signal
    act.sa_handler = &zig_func;
    act.sa_flags   = 0;
    sigfillset(&act.sa_mask);
    sigdelset(&act.sa_mask, sig);
    ASSERT_ZERO( ::sigaction(sig, &act, 0) );

    // We do not care about the existing signalset
    ::pthread_sigmask(SIG_SETMASK, &set, 0);
}

void uninstall_zig_for_this_thread(int ) {
    sigset_t         set;

    // Just block all signals
    sigfillset(&set);

    // We do not care about the existing signalset - just block the
    // indicated signal
    ::pthread_sigmask(SIG_SETMASK, &set, 0);
}


protodetails_type::protodetails_type():
    p_proto( -1 )
{}

protodetails_type::protodetails_type(struct protoent* pptr):
    p_proto( pptr->p_proto ), p_name( pptr->p_name )
{}


namespace evlbi5a {
    // Simplest is just to use mutexes to make sure not more than one thread
    // calls "strerror(3)", "getprotobyname(3)" or "random(3)" at the same time
    static pthread_mutex_t  strerror_lock = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t  random_lock   = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t  protoent_lock = PTHREAD_MUTEX_INITIALIZER;

    std::string strerror(int errnum) {
        mutex_locker     scopedLock( strerror_lock );
        return std::string( ::strerror(errnum) );
    }

    long int random( void ) {
        mutex_locker     scopedLock( random_lock );
        return ::random();
    }

    long int lrand48( void ) {
        mutex_locker     scopedLock( random_lock );
        return ::lrand48();
    }

    protodetails_type getprotobyname(char const* name) {
        mutex_locker     scopedLock( protoent_lock );
        struct protoent* pptr;

        if( (pptr=::getprotobyname(name))==0 )
            throw std::runtime_error(std::string("getprotent(")+name+") fails - no such protocol found");
        return protodetails_type(pptr);
    }
#if 0
    // We don't need the advanced information just yet. Leave this here for
    // reference how to do that thread-safe
    // Get the protocolnumber for the requested protocol
    EZASSERT2_ZERO(pent_rv = ::getprotobyname_r(realproto.c_str(), &pent, pbuf, sizeof(pbuf), &pptr), std::runtime_error,
                   EZINFO(" (proto: '" << realproto << "') - " << evlbi5a::strerror(pent_rv)));
    EZASSERT2(pptr, std::runtime_error,
              EZINFO("::getprotobyname_r yielded NULL pointer (proto: '" << realproto << "') - " << evlbi5a::strerror(pent_rv)));
#endif
} // namespace evlbi5a
