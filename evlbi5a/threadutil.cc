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

#include <signal.h>
#include <pthread.h>
#include <dosyscall.h>

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
