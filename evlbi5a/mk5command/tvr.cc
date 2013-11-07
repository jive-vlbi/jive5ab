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


string tvr_fn(bool q, const vector<string>& args, runtime& rte) {
    // NOTE: this function is basically untested, as we don't have a vsi data generator
    ostringstream         reply;
    per_runtime<uint64_t> bitstreammask;

    reply << "!" << args[0] << (q?('?'):('='));

    // According to the documentation, "tvr?" is only allowed *after*
    // TVR has started.
    // TVR command only acceptable if not doing anything or already doing
    // tvr (to switch it off)
    //     (q && ctm!=tvr) || (!q && (ctm!=no_transfer && ctm!=tvr))  =>
    //     (q && ctm!=tvr) || (!q && !(ctm==no_transfer || ctm==tvr))  =>
    //     (q && ctm!=tvr) || !(q || (ctm==no_transfer || ctm==tvr))
    INPROGRESS(rte, reply,
               (q  && rte.transfermode!=tvr) ||
               !(q || rte.transfermode==no_transfer || rte.transfermode==tvr))

    if ( q ) {
        reply << " 0 : " << (rte.ioboard[ mk5breg::DIM_GOCOM ] & rte.ioboard[ mk5breg::DIM_CHECK ]) 
              << " : " << hex_t( (rte.ioboard[ mk5breg::DIM_TVRMASK_H ] << 16) | rte.ioboard[ mk5breg::DIM_TVRMASK_L ])
              << " : " << (rte.ioboard[ mk5breg::DIM_ERF ] & rte.ioboard[ mk5breg::DIM_CHECK ]) << " ;";
        rte.ioboard[ mk5breg::DIM_ERF ] = 0;
        return reply.str();
    }

    // command

    uint64_t new_mask = 0;
    if ( (args.size() > 1) && !args[1].empty() ) {
        char* eocptr;
        errno    = 0;
        new_mask = ::strtoull(args[1].c_str(), &eocptr, 0);
        ASSERT2_COND( !(new_mask==0 && eocptr==args[1].c_str()) && !(new_mask==~((uint64_t)0) && errno==ERANGE),
                  SCINFO("Failed to parse bit-stream mask") );
    }
    else {
        if ( bitstreammask.find(&rte) == bitstreammask.end() ) {
            reply << " 6 : no current bit-stream mask set yet, need an argument ;";
            return reply.str();
        }
        new_mask = bitstreammask[&rte];
    }

    if( new_mask == 0 ) {
        // turn off TVR
        rte.ioboard[ mk5breg::DIM_GOCOM ] = 0;
        rte.transfermode                  = no_transfer;
    }
    else {
        rte.ioboard[ mk5breg::DIM_TVRMASK_H ] = (new_mask >> 16);
        rte.ioboard[ mk5breg::DIM_TVRMASK_L ] = (new_mask & 0xffff);
        rte.ioboard[ mk5breg::DIM_GOCOM ]     = 1;
        rte.transfermode                      = tvr;
        bitstreammask[&rte]                   = new_mask;
    }

    reply << " 0 ;";
    return reply.str();

}
