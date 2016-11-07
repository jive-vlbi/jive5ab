// To help signalling threads that are in a blocking read/write
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
#ifndef EVLBI5A_THREADUTIL_H
#define EVLBI5A_THREADUTIL_H

// Installs a dummy signal handler for the given
// signal in the current thread.
// The idea is that you can pthread_kill(2) the
// thread to wake it up after you close(2)'d a
// filedescriptor on which the thread was performing
// a blocking read/write
//
// Note: the handler is a dummy one ...
void install_zig_for_this_thread(int sig);
void uninstall_zig_for_this_thread(int sig);

#endif
