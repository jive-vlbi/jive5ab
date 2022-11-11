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

// Usage: data_check ? [strictness] [ : bytes to read]
string data_check_dim_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Query can only execute when the disks are available
    INPROGRESS(rte, reply, streamstorbusy(rte.transfermode))

    uint64_t bytes_to_read = 1000000;  // read 1MB by default
    string bytes_to_read_arg = OPTARG(2, args);
    if ( !bytes_to_read_arg.empty() ) {
        char*      eptr;
        unsigned long int   v = ::strtoull(bytes_to_read_arg.c_str(), &eptr, 0);

        // was a unit given? [note: all whitespace has already been stripped
        // by the main commandloop]
        EZASSERT2( eptr!=bytes_to_read_arg.c_str() && ::strchr("kM\0", *eptr),
                   cmdexception,
                   EZINFO("invalid number of bytes to read '" << bytes_to_read_arg << "'") );

        // Now we can do this
        bytes_to_read = v * ((*eptr=='k')?KB:(*eptr=='M'?MB:1));
        if ( bytes_to_read > 2ull * KB * KB * KB ) {
            reply << " 8 : maximum value for bytes to read is 2 GB ;";
            return reply.str();
        }
    }
    bytes_to_read &= ~0x7; // be sure it's a multiple of 8
    if ( bytes_to_read == 0 ) {
        reply << " 8 : need to read more than 0 bytes to detect anything ;";
        return reply.str();
    }
    
    countedpointer<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));
    playpointer end( rte.pp_current );
    end += bytes_to_read;    
    streamstor_reader_type data_reader( rte.xlrdev.sshandle(), rte.pp_current, end );
    data_reader.read_into( (unsigned char*)buffer->data, 0, bytes_to_read );
    
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
    if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, strict, rte.verbose_scancheck, found_data_type) &&
         ((found_data_type.format == fmt_mark5b) || is_vdif(found_data_type.format)) ) {
        cerr << "found " << found_data_type << endl;

        headersearch_type header_format(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate,
                is_vdif(found_data_type.format) ? found_data_type.vdif_frame_size - headersize(found_data_type.format, 1) : 0);

        unsigned int frame_number = found_data_type.frame_number;
        bool tvg = false; // VDIF default
        string date_code = ""; // VDIF default
        if ( !is_vdif(found_data_type.format) ) {
            const m5b_header& header_data = *(const m5b_header*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset]);
            const mk5b_ts& header_ts = *(const mk5b_ts*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset + 8]);
            tvg = header_data.tvg;
            ostringstream date_code_stream;
            date_code_stream << (int)header_ts.J2 << (int)header_ts.J1 << (int)header_ts.J0;
            date_code = date_code_stream.str();
        }

        reply << " 0 : ";
        if (tvg) {
            reply << "tvg : ";
        }
        else if ( is_vdif(found_data_type.format) ) {
            reply << found_data_type.format << " : ";
        }
        else {
            reply << "ext : ";
        }

        reply << 
            tm2vex(found_data_type.time) << " : " << 
            date_code << " : " << 
            frame_number << " : ";
        if ( found_data_type.is_partial() ) {
            // no subsecond information, nothing to print
            reply <<
                "? : " << // frame header period
                "? : " << // total recording rate
                found_data_type.byte_offset << " : " <<
                "? ;"; // missing bytes
            return reply.str();
        }

        samplerate_type   frame_period = header_format.get_state().frametime;
        unsigned int      vdif_thread_multiplier = is_vdif(found_data_type.format) ? found_data_type.vdif_threads.size() : 1;
        samplerate_type   data_rate_mbps = found_data_type.trackbitrate * found_data_type.ntrack * vdif_thread_multiplier / 1000000;
        highresdelta_type time_diff = found_data_type.time - prev_data_type.time;
        int64_t           expected_bytes_diff = boost::rational_cast<int64_t>(
                                time_diff * header_format.framesize * vdif_thread_multiplier / frame_period.as<highresdelta_type>()
                            );
        int64_t           missing_bytes = (int64_t)(rte.pp_current - prev_play_pointer) + 
                                          ((int64_t)found_data_type.byte_offset - (int64_t)prev_data_type.byte_offset) -
                                          expected_bytes_diff;

        reply <<
            frame_period << "s : " << 
            data_rate_mbps << " : " << 
            found_data_type.byte_offset << " : " << 
            missing_bytes << " ;";

        prev_data_type    = found_data_type;
        prev_play_pointer = rte.pp_current;
    }
    else {
        reply << " 0 : ? ;";
    }

    return reply.str();
}
