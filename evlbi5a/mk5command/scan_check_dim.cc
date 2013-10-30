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


string scan_check_dim_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot do a data check while " << rte.transfermode << " is in progress ;";
        return reply.str();
    }

    if ( rte.current_scan >= rte.xlrdev.nScans() ) {
        reply << " 6 : current scan (#" << (rte.current_scan + 1) << ") not within bounds of number of recorded scans (" << rte.xlrdev.nScans() << ") ;";
        return reply.str();
    }

    int64_t length = rte.pp_end - rte.pp_current;
    if ( length < 0 ) {
        reply << " 6 : scan start pointer is set beyond scan end pointer ;";
        return reply.str();
    }
    
    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
    if ( length < bytes_to_read ) {
      reply << " 6 : scan too short to check ;";
      return reply.str();
    }
    

    ROScanPointer scan_pointer(rte.xlrdev.getScan(rte.current_scan));

    reply << " 0 : " << (rte.current_scan + 1) << " : " << scan_pointer.name() << " : ";

    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));
    playpointer read_pointer( rte.pp_current );
    
    XLRCODE(
    S_READDESC readdesc;
    readdesc.XferLength = bytes_to_read;
    readdesc.AddrHi     = read_pointer.AddrHi;
    readdesc.AddrLo     = read_pointer.AddrLo;
    readdesc.BufferAddr = buffer->data;
            );
    // make sure SS is ready for reading
    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
    // read the data
    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

    data_check_type found_data_type;

    unsigned int first_valid;
    unsigned int first_invalid;
    
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
        // get tvg and date code from data before we re-use it
        const m5b_header& header_data = *(const m5b_header*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset]);
        const mk5b_ts& header_ts = *(const mk5b_ts*)(&((unsigned char*)buffer->data)[found_data_type.byte_offset + 8]);

        bool tvg = header_data.tvg;
        ostringstream date_code;
        date_code << (int)header_ts.J2 << (int)header_ts.J1 << (int)header_ts.J0;

        // found something at start of the scan, check for the same format at the end
        read_pointer += ( length - bytes_to_read );
        XLRCODE(
        readdesc.AddrHi     = read_pointer.AddrHi;
        readdesc.AddrLo     = read_pointer.AddrLo;
        readdesc.BufferAddr = buffer->data;
                );
        XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );
        
        data_check_type end_data_type;
        headersearch_type header_format(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);
        if ( is_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, header_format, strict, end_data_type.byte_offset, end_data_type.time) ) {
            struct tm time_struct;
            const m5b_header& end_header_data = *(const m5b_header*)(&((unsigned char*)buffer->data)[end_data_type.byte_offset]);

            if (tvg == (bool)end_header_data.tvg) {

                ::gmtime_r( &found_data_type.time.tv_sec, &time_struct );

                if ( tvg ) {
                    reply << "tvg : ";
                }
                else {
                    reply << "- : ";
                }
                
                double track_frame_period = (double)header_format.payloadsize * 8 / (double)(header_format.trackbitrate * header_format.ntrack);
                double time_diff = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
                    (end_data_type.time.tv_nsec - found_data_type.time.tv_nsec) / 1000000000.0;
                int64_t expected_bytes_diff = (int64_t)round(time_diff * header_format.framesize / track_frame_period);
                int64_t missing_bytes = (int64_t)length - bytes_to_read - (int64_t)found_data_type.byte_offset + (int64_t)end_data_type.byte_offset - expected_bytes_diff;
                
                reply << date_code.str() << " : ";
                // start time
                reply <<  tm2vex(time_struct, found_data_type.time.tv_nsec) << " : ";
                // scan length
                double scan_length = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
                    ((int)end_data_type.time.tv_nsec - (int)found_data_type.time.tv_nsec) / 1e9 + 
                    (bytes_to_read - end_data_type.byte_offset) / (header_format.framesize / track_frame_period);// assume the bytes to the end have valid data
                
                scan_length = round(scan_length / track_frame_period) * track_frame_period; // round it to the nearest possible value

                reply << scan_length << "s : ";
                // total recording rate
                reply << (header_format.trackbitrate * header_format.ntrack / 1e6) << "Mbps : ";
                reply << (-missing_bytes) << " ;";
                return reply.str();
            }
        }
            
    }
    else if ( is_ss_test_pattern( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << "SS : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
        return reply.str();
    }
    reply << "? ;";

    return reply.str();
}
