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
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <iostream>

using namespace std;


string skip_fn( bool q, const vector<string>& args, runtime& rte ) {
    static per_runtime<int64_t> skips;
    // local variables
    int64_t        nskip;
    ostringstream  reply;
    
    reply << "!" << args[0] << (q?('?'):('='));

    // Query is always possible. Allow command if doing a 
    // transfer to which it sensibly applies:
    // We cannot do this command (something is in progress)
    // if it's a command and we're not writing to the I/O board
    //     !q && !toio() => !(q || toio())
    INPROGRESS(rte, reply, !(q || toio(rte.transfermode)))
    
    if( q ) {
        reply << " 0 : " << skips[&rte] << " ;";
        return reply.str();
    }

    // We rilly need an argument
    if( args.size()<2 || args[1].empty() ) {
        reply << " 8 : Command needs argument! ;";
        return reply.str();
    }

    // Now see how much to skip
    // TODO FIXME Better string-to-integer conversion!
    nskip    = ::strtol(args[1].c_str(), 0, 0);

    // Attempt to do the skip. Return value is always
    // positive so must remember to get the sign right
    // before testing if the skip was achieved
    // Must serialize access to the StreamStor, therefore
    // use do_xlr_lock/do_xlr_unlock
    do_xlr_lock();
    skips[&rte] = ::XLRSkip( rte.xlrdev.sshandle(),
                             (CHANNELTYPE)(::llabs(nskip)), (nskip>=0) );
    do_xlr_unlock();
    if( nskip<0 )
        skips[&rte] = -skips[&rte];

    // If the achieved skip is not the expected skip ...
    reply << " 0";
    if( skips[&rte]!=nskip )
        reply << " : Requested skip was not achieved";
    reply << " ;";
    return reply.str();
}
