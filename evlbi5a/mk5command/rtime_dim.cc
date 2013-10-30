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


string rtime_dim_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    reply << "!" << args[0] << (q?('?'):('='));
    uint64_t length = ::XLRGetLength(rte.xlrdev.sshandle());
    long page_size = ::sysconf(_SC_PAGESIZE);
    uint64_t capacity = (uint64_t)rte.xlrdev.devInfo().TotalCapacity * (uint64_t)page_size;

    mk5b_inputmode_type inputmode;
    rte.get_input(inputmode);
    headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                 (unsigned int)rte.trackbitrate(),
                                 rte.vdifframesize());
    double track_data_rate = (double)dataformat.trackbitrate * (double)dataformat.framesize / (double)dataformat.payloadsize;
    double total_recording_rate = track_data_rate * dataformat.ntrack;

    reply << " 0 : " 
          << ((capacity - length) / total_recording_rate * 8) << "s : "
          << ((capacity - length) / 1e9) << "GB : " 
          << ((capacity - length) * 100.0 / capacity) << "% : "
          << ((inputmode.tvg == 0 || inputmode.tvg == 3) ? "ext" : "tvg") << " : "
          << hex << "0x" << inputmode.bitstreammask << dec << " : " 
          << ( 1 << inputmode.j ) << " : "
          << (total_recording_rate/1e6) << "Mbps ;";
        
    return reply.str();
}
