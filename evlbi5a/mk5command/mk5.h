// Generic Mark5 utilities shared between all Mark5 flavours
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
#ifndef JIVE5AB_MK5_H
#define JIVE5AB_MK5_H

// All functions have the signature string (*)(bool, vector<string>, runtime&)
// and for the reply they typically use ostringstream.
// so as a courtesy we make sure all of those are available
#include <runtime.h>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <inttypes.h>

// Some handy defines
#define KB  (1024)
#define MB  (KB*KB)

#define GETSSHANDLE(rte) (rte.xlrdev.sshandle())

// For argument parsing:
// returns the value of s[n] provided that:
//  s.size() > n
// otherwise returns the empty string
#define OPTARG(n, s) \
    ((s.size()>n)?s[n]:std::string())

// 17-Oct-2013 HV: In order to safify the commands and to consistify
//                 the replies the following is one.
//                 Each function is responsible for determining wether
//                 it is allowed to execute, given the current
//                 runtime state, if it's a command or query and/or
//                 the arguments to the command/query.
//                 The reply in case the command/query should not be
//                 executing can be consistently formed using the
//                 following #define
//                 It takes a 'runtime' parameter, a stringstream parameter
//                 and a condition. In case the condition evaluates to TRUE
//                 the reply will be completed by "6 : <reason> ;"
//                 and a "return reply.str()" will be executed.
#define INPROGRESS(runt, repl, cond) \
        if( (cond) ) { \
            (repl) << " 6 : not whilst " << (runt).transfermode << " is in progress ;"; \
            return (repl).str(); \
        }

// Since the actual functions typically operate on a runtime environment
// and sometimes they need to remember something, it makes sense to do
// this on a per-runtime basis. This struct allows easy per-runtime
// saving of state.
// Usage:
//   per_runtime<string>  lasthost;
//   ...
//   swap(rte.netparms.host, lasthost[&rte]);
//   cout << lasthost[&rte] << endl;
//   lasthost[&rte] = "foo.bar.bz";
template <typename T>
struct per_runtime:
    public std::map<runtime const*, T>
{};

// function prototype for fn that programs & starts the
// Mk5B/DIM disk-frame-header-generator at the next
// available moment.
void start_mk5b_dfhg( runtime& rte, double maxsyncwait = 3.0 );

// Based on the information found in the runtime compute
// the theoretical IPD. 
// YOU MUST HAVE FILLED "rte.sizes" WITH THE RESULT OF A constrain()
// FUNCTION CALL BEFORE ACTUALLY CALLING THIS ONE!
void compute_theoretical_ipd( runtime& rte );

// For the data check functions - they must be able to read some data
// into a buffer and easily discard it later on
struct XLR_Buffer {
    READTYPE* data;

    XLR_Buffer( uint64_t len );
    ~XLR_Buffer();
};

// help functions to transfer between user interface and ioboard values
mk5areg::regtype::base_type track2register( unsigned int track );
unsigned int                register2track( mk5areg::regtype::base_type reg );

#endif
