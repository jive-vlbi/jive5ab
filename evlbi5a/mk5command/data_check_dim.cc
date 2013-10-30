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
#include <data_check.h>
#include <iostream>

using namespace std;


string data_check_dim_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot do a data check while " << rte.transfermode << " is in progress ;";
        return reply.str();
    }

    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));

    XLRCODE(
    S_READDESC readdesc;
    readdesc.XferLength = bytes_to_read;
    readdesc.AddrHi     = rte.pp_current.AddrHi;
    readdesc.AddrLo     = rte.pp_current.AddrLo;
    readdesc.BufferAddr = buffer->data;
            );

    // make sure SS is ready for reading
    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
    // read the piece of data
    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

    data_check_type found_data_type;

    // static variables to be able to compute "missing bytes"
    static data_check_type prev_data_type;
    static playpointer prev_play_pointer;

    bool strict = true;
    string strict_arg = OPTARG(1, args);
    if ( !strict_arg.empty() ) {
        if (strict_arg == "0" ) {
            strict = false;
        }
        else if (strict_arg != "1" ) {
            reply << "8 : strict argument has to be 0 or 1 ;";
            return reply.str();
        }
    }
    
    // use track 4 for now
    if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, strict, found_data_type) && (found_data_type.format == fmt_mark5b) ) {
        struct tm time_struct;
        headersearch_type header_format(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);
        const m5b_header& header_data = *(const m5b_header*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset]);
        const mk5b_ts& header_ts = *(const mk5b_ts*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset + 8]);

        reply << " 0 : ";
        if (header_data.tvg) {
            reply << "tvg : ";
        }
        else {
            reply << "ext : ";
        }

        ::gmtime_r( &found_data_type.time.tv_sec, &time_struct );

        double frame_period = (double)header_format.payloadsize * 8 / (double)(header_format.trackbitrate * header_format.ntrack);
        double data_rate_mbps = header_format.framesize * 8 / (frame_period * 1001600); // take out the header overhead
        double time_diff = (found_data_type.time.tv_sec - prev_data_type.time.tv_sec) + 
            (found_data_type.time.tv_nsec - prev_data_type.time.tv_nsec) / 1000000000.0;
        int64_t expected_bytes_diff = (int64_t)round(time_diff * header_format.framesize / frame_period);
        int64_t missing_bytes = (int64_t)(rte.pp_current - prev_play_pointer) + ((int64_t)found_data_type.byte_offset - (int64_t)prev_data_type.byte_offset) - expected_bytes_diff;
 
        reply <<  tm2vex(time_struct, found_data_type.time.tv_nsec) << " : " << (int)header_ts.J2 << (int)header_ts.J1 << (int)header_ts.J0 << " : " << header_data.frameno << " : " << frame_period << "s : " << data_rate_mbps << " : " << found_data_type.byte_offset << " : " << missing_bytes << " ;";

        prev_data_type = found_data_type;
        prev_play_pointer = rte.pp_current;
    }
    else {
        reply << " 0 : ? ;";
    }

    return reply.str();
}
