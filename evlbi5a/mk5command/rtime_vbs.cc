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


string rtime_vbs_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << (q?('?'):('='));

    // Query only
    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }
   
    mountpointinfo_type mpi( ::statmountpoints(rte.mk6info.mountpoints) ); 
    headersearch_type   dataformat(rte.trackformat(), rte.ntrack(),
                                   (unsigned int)rte.trackbitrate(),
                                   rte.vdifframesize());
    double track_data_rate      = (double)dataformat.trackbitrate * (double)dataformat.framesize
                                  / (double)(dataformat.valid() ? dataformat.payloadsize : 1);
    double total_recording_rate = track_data_rate * dataformat.ntrack;
    double capacity             = (double)mpi.f_size;
    double length               = (double)(mpi.f_size - mpi.f_free);

    if( !dataformat.valid() ) {
        // Basically no "mode" specified
        reply << " 0 : " 
              << "<unkown> (no mode set yet) : "
              << ((capacity - length) / 1e9) << "GB : " 
              << ((capacity - length) * 100.0 / capacity) << "% : "
              << rte.trackformat()  << " : "
              << rte.ntrack()  << " : " 
              << 0 << " : "
              << "<unknown> Mbps ;";
    } else {
        reply << " 0 : " 
              << ((capacity - length) / total_recording_rate * 8) << "s : "
              << ((capacity - length) / 1e9) << "GB : " 
              << ((capacity - length) * 100.0 / capacity) << "% : "
              << rte.trackformat() /*inputmode.mode*/  << " : "
              << rte.ntrack()  << " : " 
              << 0 << " : "
              << (total_recording_rate/1e6) << "Mbps ;";
    }
        
    return reply.str();
}
