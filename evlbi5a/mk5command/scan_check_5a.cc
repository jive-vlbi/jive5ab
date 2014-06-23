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
#include <countedpointer.h>

#include <iostream>

using namespace std;


//
// Usage:
// scan_check ? [ strictness ] [ : number of bytes to read ]
// file_check ? [ strictness ] : [ number of bytes to read ] : file name
//

string scan_check_5a_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Query is only available if disks are available/not busy
    INPROGRESS(rte, reply, streamstorbusy(rte.transfermode))
    const bool from_file = ( args[0] == "file_check" );

    if ( !from_file && (rte.current_scan >= rte.xlrdev.nScans()) ) {
        reply << " 6 : current scan (#" << (rte.current_scan + 1) << ") not within bounds of number of recorded scans (" << rte.xlrdev.nScans() << ") ;";
        return reply.str();
    }

    countedpointer<data_reader_type> data_reader;
    if ( from_file ) {
        const string filename = OPTARG(3, args);
        if ( filename.empty() ) {
            reply << " 8 : no filename given ;";
            return reply.str();
        }
        data_reader = countedpointer<data_reader_type>( new file_reader_type(filename) );
    }
    else {
        data_reader = countedpointer<data_reader_type>( new streamstor_reader_type(rte.xlrdev.sshandle(), rte.pp_current, rte.pp_end) );
        if ( data_reader->length() < 0 ) {
            reply << " 6 : scan start pointer is set beyond scan end pointer ;";
            return reply.str();
        }
    }

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

    if ( data_reader->length() < (int64_t)bytes_to_read ) {
      reply << " 6 : scan too short to check ;";
      return reply.str();
    }
    
    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));

    if ( from_file ) {
        reply << " 0 : ";
    }
    else {
        ROScanPointer scan_pointer(rte.xlrdev.getScan(rte.current_scan));

        reply << " 0 : " << (rte.current_scan + 1) << " : " << scan_pointer.name() << " : ";
        
    }
    data_reader->read_into( (unsigned char*)buffer->data, 0, bytes_to_read );
    
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
    if ( find_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, strict, found_data_type) ) {
        // found something at start of the scan, check for the same format at the end
        uint64_t read_offset;
        if ( is_vdif(found_data_type.format) ) {
            // round the start of data to read to a multiple of 
            // VDIF frame size, in the hope of being on a frame
            read_offset = (data_reader->length() - bytes_to_read) 
                    / found_data_type.vdif_frame_size * found_data_type.vdif_frame_size;
        }
        else {
            read_offset = data_reader->length() - bytes_to_read;
        }
    
        read_offset = data_reader->read_into( (unsigned char*)buffer->data, read_offset, bytes_to_read );
        
        struct tm time_struct;
        ::gmtime_r( &found_data_type.time.tv_sec, &time_struct );

        data_check_type end_data_type = found_data_type;
        headersearch_type header_format
            ( found_data_type.format, 
              found_data_type.ntrack, 
              (is_vdif(found_data_type.format) ? headersearch_type::UNKNOWN_TRACKBITRATE : found_data_type.trackbitrate), 
              (is_vdif(found_data_type.format) ? found_data_type.vdif_frame_size - headersize(found_data_type.format, 1): 0)
              );
        if ( is_data_format( (unsigned char*)buffer->data, bytes_to_read, 4, header_format, strict, found_data_type.vdif_threads, end_data_type.byte_offset, end_data_type.time, end_data_type.frame_number) ) {
            if (found_data_type.format == fmt_mark4_st) {
                reply << "st : mark4 : ";
            }
            else if (found_data_type.format == fmt_vlba_st) {
                reply << "st : vlba : ";
            }
            else {
                reply << found_data_type.format << " : ";
                if ( is_vdif(found_data_type.format) ) {
                    if ( found_data_type.vdif_threads == 0 ) {
                        // found a heterogenous set of VDIF threads,
                        // best way to report that seems to be the '?' for number of tracks
                        reply << "? : ";
                    }
                    else {
                        reply << ( found_data_type.ntrack * found_data_type.vdif_threads ) << " : ";
                    }
                }
                else {
                    reply << found_data_type.ntrack << " : ";
                }
            }

            // if either data check result has no subsecond information,
            // try to fill in the blanks by combining the two results
            if ( !combine_data_check_results(found_data_type, end_data_type, read_offset) ) {
                // no subsecond information, print what we do know

                // start time
                reply << tm2vex(time_struct, found_data_type.time.tv_nsec) << " : ";
            
                reply << (end_data_type.time.tv_sec - found_data_type.time.tv_sec) << ".****s : " <<
                    "? : " << // bit rate
                    "? ;"; // missing bytes
            }
            else {
                // start time 
                reply << tm2vex(time_struct, found_data_type.time.tv_nsec) << " : ";
                
                unsigned int vdif_threads = (is_vdif(found_data_type.format) ? found_data_type.vdif_threads : 1);
                double track_frame_period = (double)header_format.payloadsize * 8 / (double)(found_data_type.trackbitrate * found_data_type.ntrack);
                double time_diff = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
                    (end_data_type.time.tv_nsec - found_data_type.time.tv_nsec) / 1000000000.0;
                int64_t expected_bytes_diff = (int64_t)round(time_diff * header_format.framesize * vdif_threads / track_frame_period);
                int64_t missing_bytes = (int64_t)read_offset - (int64_t)found_data_type.byte_offset + (int64_t)end_data_type.byte_offset - expected_bytes_diff;

                // scan length (seconds)
                double scan_length = (end_data_type.time.tv_sec - found_data_type.time.tv_sec) + 
                    ((int)end_data_type.time.tv_nsec - (int)found_data_type.time.tv_nsec) / 1e9 + 
                    (bytes_to_read - end_data_type.byte_offset) / (header_format.framesize / track_frame_period);// assume the bytes to the end have valid data
                reply << scan_length << "s : ";
                reply << (found_data_type.trackbitrate / 1e6) << "Mbps : ";
                reply << (-missing_bytes) << " ;";
            }
            return reply.str();
        }

    }
    else if ( is_mark5a_tvg( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << "tvg : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
        return reply.str();
    }
    else if ( is_ss_test_pattern( (unsigned char*)buffer->data, bytes_to_read, first_valid, first_invalid) ) {
        reply << "SS : " << first_valid << " : " << first_invalid << " : " << bytes_to_read << " ;";
        return reply.str();
    }
    reply << "? ;";

    return reply.str();
}
