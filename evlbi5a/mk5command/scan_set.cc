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
#include <dotzooi.h>
#include <iostream>

using namespace std;


string scan_set_fn(bool q, const vector<string>& args, runtime& rte) {
    // note that we store current_scan zero based, 
    // but user communication is one based
    ostringstream              reply;
    static per_runtime<string> previous_search_string;

    reply << "!" << args[0] << (q?('?'):('='));

    const unsigned int nScans = rte.xlrdev.nScans();

    if ( q ) {
        
        reply << " 0 : " << (rte.current_scan + 1) << " : ";
        if ( rte.current_scan < nScans ) {
            ROScanPointer scan = rte.xlrdev.getScan( rte.current_scan );
            reply << scan.name() << " : " << rte.pp_current << " : " << rte.pp_end << " ;";
            
        }
        else {
            reply << " scan is out of range of current disk (" << nScans << ") ;";
        }
        
        return reply.str();
    }

    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot set scan during " << rte.transfermode << " ;";
        return reply.str();
    }

    if ( nScans == 0 ) {
        reply << " 6 : disk does not have any scans present ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        rte.setCurrentScan( nScans - (rte.xlrdev.isScanRecording() ? 2 : 1) );
        return reply.str();
    }

    // first argument is a scan number, search string, "inc", "dec" or "next"

    if ( args[1].empty() ) {
        rte.setCurrentScan( nScans - (rte.xlrdev.isScanRecording() ? 2 : 1) );
    }
    else if ( args[1] == "inc" ) {
        unsigned int scan = rte.current_scan + 1;
        if ( scan >= nScans ) {
            scan = 0;
        }
        rte.setCurrentScan( scan );
    }
    else if ( args[1] == "dec" ) {
        int scan = (int)rte.current_scan - 1;
        if ( scan < 0 ) {
            scan = nScans - 1;
        }
        rte.setCurrentScan( scan );
    }
    else {
        char* endptr;
        long int parsed = strtol(args[1].c_str(), &endptr, 10);

        if ( (*endptr != '\0') || (parsed < 1) || (parsed > (long int)nScans) ) {
            // try to interpret it as a search string
            string search_string = args[1];
            unsigned int next_scan = 0;
            if ( args[1] == "next" ) {
                if ( previous_search_string.find(&rte) == previous_search_string.end() ) {
                    reply << " 4 : no search string given yet ;";
                    return reply.str();
                }
                search_string = previous_search_string[&rte];
                next_scan = rte.current_scan + 1;
            }
            unsigned int end_scan = next_scan + nScans;
            previous_search_string[&rte] = search_string;
            // search string is case insensitive, convert both to uppercase
            search_string = toupper(search_string);
            
            while ( (next_scan != end_scan) && 
                    (toupper(rte.xlrdev.getScan(next_scan % nScans).name()).find(search_string) == string::npos)
                    ) {
                next_scan++;
            }
            if ( next_scan == end_scan ) {
                reply << " 8 : failed to find scan matching '" << search_string << "' ;";
                return reply.str();
            }
            rte.setCurrentScan( next_scan % nScans );
        }
        else {
            rte.setCurrentScan( (unsigned int)parsed - 1 );
        }
    }

    // two optional argument can shift the scan start and end pointers
    
    // as the offset might be given in time, we might need the data format 
    // to compute the data rate, do that data check only once
    data_check_type found_data_type;
    bool data_checked = false;

    for ( unsigned int argument_position = 2; argument_position < min((size_t)4, args.size()); argument_position++ ) {
        int64_t byte_offset;
        if ( args[argument_position].empty() ) {
            if ( argument_position == 2 ) {
                // start default is start of scan
                byte_offset = 0;
            }
            else {
                // stop default is end of scan
                byte_offset = rte.xlrdev.getScan( rte.current_scan ).length();
            }
        }
        // for the start byte offset, we have the option to set it to 
        // s (start), c (center), e (end) and s+ (a bit past start)
        else if ( (argument_position == 2) && (args[2] == "s") ) {
            byte_offset = 0;
        }
        else if ( (argument_position == 2) && (args[2] == "c") ) {
            byte_offset = rte.xlrdev.getScan( rte.current_scan ).length() / 2;
        }
        else if ( (argument_position == 2) && (args[2] == "e") ) {
            // as per documentation, ~1M before end
            byte_offset = rte.xlrdev.getScan( rte.current_scan ).length() - 1000000;

        }
        else if ( (argument_position == 2) && (args[2] == "s+") ) {
            byte_offset = 65536;
        }
        else {
            // first try to interpret it as a time
            bool is_time = true;
            bool relative_time;
            struct ::tm parsed_time;
            unsigned int microseconds;
            try {
                // default it to "zero"
                parsed_time.tm_year = 0;
                parsed_time.tm_mon = 0;
                parsed_time.tm_mday = 1;
                parsed_time.tm_hour = 0;
                parsed_time.tm_min = 0;
                parsed_time.tm_sec = 0;
                microseconds = 0;

                // time might be prefixed with a '+' or '-', dont try to parse that
                relative_time = ( (args[argument_position][0] == '+') ||
                                  (args[argument_position][0] == '-') );
                
                ASSERT_COND( parse_vex_time(args[argument_position].substr(relative_time ? 1 : 0), parsed_time, microseconds) > 0 );
            }
            catch ( ... ) {
                is_time = false;
            }

            if ( is_time ) {
                // we need a data format to compute a byte offset from the time offset 
                if ( !data_checked ) {
                    static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8
                    auto_ptr<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));

                    XLRCODE(
                    S_READDESC readdesc;
                    readdesc.XferLength = bytes_to_read;
                            );
                    playpointer pp( rte.xlrdev.getScan( rte.current_scan ).start() );
                    
                    XLRCODE(
                    readdesc.AddrHi     = pp.AddrHi;
                    readdesc.AddrLo     = pp.AddrLo;
                    readdesc.BufferAddr = buffer->data;
                            );
                
                    // make sure SS is ready for reading
                    XLRCALL( ::XLRSetMode(rte.xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
                    XLRCALL( ::XLRBindOutputChannel(rte.xlrdev.sshandle(), 0) );
                    XLRCALL( ::XLRSelectChannel(rte.xlrdev.sshandle(), 0) );
                    XLRCALL( ::XLRRead(rte.xlrdev.sshandle(), &readdesc) );

                    const unsigned int track = 4; // have to pick one
                    if ( !find_data_format( (unsigned char*)buffer->data, bytes_to_read, track, true, found_data_type) ) {
                        reply << " 4 : failed to find data format needed to compute byte offset in scan ;";
                        return reply.str();
                    }
                }
                data_checked = true;
                headersearch_type headersearch(found_data_type.format, found_data_type.ntrack, found_data_type.trackbitrate, 0);
                // data rate in B/s, taking into account header overhead
                const uint64_t data_rate = 
                    (uint64_t)headersearch.ntrack * 
                    (uint64_t)headersearch.trackbitrate *
                    (uint64_t)headersearch.framesize /
                    (uint64_t)headersearch.payloadsize / 
                    8;
                
                if ( relative_time ) {
                    // the year (if given) is ignored
                    unsigned int seconds = seconds_in_year( parsed_time );
                    byte_offset = (int64_t)round( (seconds + microseconds / 1e6) * data_rate );
                    if ( args[argument_position][0] == '-' ) {
                        byte_offset = -byte_offset;
                    }
                }
                else {
                    // re-run the parsing, but now default it to the data time
                    ASSERT_COND( gmtime_r(&found_data_type.time.tv_sec, &parsed_time ) );
                    microseconds = (unsigned int)round(found_data_type.time.tv_nsec / 1e3);
                    
                    unsigned int fields = parse_vex_time(args[argument_position], parsed_time, microseconds);
                    ASSERT_COND( fields > 0 );

                    time_t requested_time = ::mktime( &parsed_time );
                    ASSERT_COND( requested_time != (time_t)-1 );

                    // check if the requested time if before or after the data time
                    if ( (requested_time < found_data_type.time.tv_sec) || 
                         ((requested_time == found_data_type.time.tv_sec) && (((long)microseconds * 1000) < found_data_type.time.tv_nsec)) ) {
                        // we need to be at the next "mark"
                        // this means increasing the first field 
                        // that was not defined by one

                        // second, minute, hour, day, year 
                        // (we take the same shortcut for years as in Mark5A/dimino)
                        const unsigned int field_second_values[] = { 
                            1, 
                            60, 
                            60 * 60, 
                            24 * 60 * 60, 
                            365 * 24 * 60 * 60};
                        requested_time += field_second_values[ min((size_t)fields, sizeof(field_second_values)/sizeof(field_second_values[0]) - 1) ];
                    }

                    // verify that increasing the requested time actually 
                    // puts us past the data time
                    ASSERT_COND( (requested_time > found_data_type.time.tv_sec) || 
                                 ( (requested_time == found_data_type.time.tv_sec) && 
                                   (((long)microseconds * 1000) >= found_data_type.time.tv_nsec) ) );

                    unsigned int seconds = requested_time - found_data_type.time.tv_sec;
                    byte_offset = seconds * data_rate 
                        - (uint64_t)round((found_data_type.time.tv_nsec / 1e9 - microseconds /1e6) * data_rate) 
                        + found_data_type.byte_offset;
                }
                
            }
            else {
                // failed to parse input as time, should be byte offset then
                char* endptr;
                byte_offset = strtoll(args[argument_position].c_str(), &endptr, 0);
                
                if( ! ((*endptr == '\0') && 
                       ( ((byte_offset != std::numeric_limits<int64_t>::max())
                            && (byte_offset != std::numeric_limits<int64_t>::min())) ||
                         (errno!=ERANGE))) ) {
                    reply << " 8 : failed to parse byte offset or time code from '" << args[argument_position] << "' ;";
                    return reply.str();
                }
            }
        }

        // we should have a byte offset now
        if ( argument_position == 2 ) {
            // apply it to the start byte position
            if (byte_offset >= 0) {
                rte.pp_current += byte_offset;
            }
            else {
                rte.pp_current = rte.pp_end;
                rte.pp_current -= -byte_offset;
            }
        }
        else {
            // apply it to the end byte position
            if (byte_offset >= 0) {
                rte.pp_end = rte.xlrdev.getScan( rte.current_scan ).start();
                rte.pp_end += byte_offset;
            }
            else {
                rte.pp_end -= -byte_offset;
            }
        }
    }

    // check that the byte positions are within scan bounds
    // if not, set them to the scan bound themselves and return error 8
    ROScanPointer current_scan = rte.xlrdev.getScan( rte.current_scan );
    if ( (rte.pp_current < current_scan.start()) ||
         (rte.pp_end > current_scan.start() + current_scan.length()) ) {
        rte.setCurrentScan( rte.current_scan );
        reply << " 8 : scan pointer offset(s) are out of bound ;";
        return reply.str();
    }

    reply << " 0 ;";

    return reply.str();
}
