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


string rtime_5a_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream     reply;

    reply << "!" << args[0] << (q?('?'):('='));

    // Before carrying on .. this is a query only thing
    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }
    // The rtime query is only not possible when the streamstor is
    // unavailable
    INPROGRESS(rte, reply, diskunavail(rte.transfermode))

    S_DEVINFO devinfo;
    rte.xlrdev.copyDevInfo(devinfo);

    uint64_t          length = ::XLRGetLength(rte.xlrdev.sshandle());
    long              page_size = ::sysconf(_SC_PAGESIZE);
    uint64_t          capacity = (uint64_t)devinfo.TotalCapacity * (uint64_t)page_size;
    inputmode_type    inputmode;

    rte.get_input(inputmode);

    headersearch_type       dataformat(rte.trackformat(), rte.ntrack(),
                                       rte.trackbitrate(),
                                       rte.vdifframesize());
    if( !dataformat.valid() ) {
        // Basically no "mode" specified
        reply << " 0 : " 
              << "<unkown> (no mode set yet) : "
              << ((capacity - length) / 1e9) << "GB : " 
              << ((capacity - length) * 100.0 / capacity) << "% : "
              << inputmode.mode << " : "
              << inputmode.submode << " : " 
              << 0 << "MHz : "
              << "<unknown> Mbps ;";
    } else {
        const samplerate_type   track_data_rate      = (dataformat.trackbitrate * dataformat.framesize) / dataformat.payloadsize;
        const samplerate_type   total_recording_rate = track_data_rate * dataformat.ntrack;

        reply << " 0 : " 
              << (capacity - length) / boost::rational_cast<double>(total_recording_rate/8) << "s : "
              << ((capacity - length) / 1e9) << "GB : " 
              << ((capacity - length) * 100.0 / capacity) << "% : "
              << inputmode.mode << " : "
              << inputmode.submode << " : " 
              << boost::rational_cast<double>(track_data_rate/1000000) << "MHz : "
              << boost::rational_cast<double>(total_recording_rate/1000000) << "Mbps ;";
    }        

    return reply.str();
}

