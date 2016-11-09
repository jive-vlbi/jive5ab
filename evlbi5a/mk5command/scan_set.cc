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
    const transfer_type        ctm( rte.transfermode );
    static per_runtime<string> previous_search_string;

    reply << "!" << args[0] << (q?('?'):('='));

    // Query available if disks available, command only when doing
    // nothing with the disks.
    // BE/HV 19-Nov-2013 - In fact, scan_set= can always be done, even
    //                     during recording
    INPROGRESS(rte, reply, diskunavail(ctm))

    const bool         isRecording = rte.xlrdev.isScanRecording();
    const unsigned int nScans      = rte.xlrdev.nScans();

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

    if ( nScans == 0 ) {
        reply << " 6 : disk does not have any scans present ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        // If we're recording the first scan, we cannot go to the
        // last-but-one
        if( isRecording && nScans<2 ) {
            reply << " 6 : there is no last-but-one scan yet (recording first scan);";
        } else {
            rte.setCurrentScan( nScans - (isRecording ? 2 : 1) );
        }
        return reply.str();
    }

    // From here on we have to come up with a new scan number
    // and (possibly) adjusted start/end byte numbers
    // We capture the info in local variables first such that on
    // error the state is not changed.
    int64_t        ppStart, ppEnd, ppLength;
    unsigned int   newScan;

    // first argument is a scan number, search string, "inc", "dec" or "next"

    if ( args[1].empty() ) {
        if( isRecording && nScans<2 ) {
            reply << " 6 : there is no last-but-one scan yet (recording first scan);";
            return reply.str();
        }
        newScan = nScans - (isRecording ? 2 : 1);
    }
    else if ( args[1] == "inc" ) {
        newScan = rte.current_scan + 1;
        if ( newScan >= nScans ) {
            newScan = 0;
        }
    }
    else if ( args[1] == "dec" ) {
        // use temporary int to deal with <0
        int scan = (int)rte.current_scan - 1;
        if ( scan < 0 ) {
            scan = nScans - 1;
        }
        newScan = (unsigned int)scan;
    }
    else {
        char*    endptr;
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
            newScan = next_scan % nScans;
        }
        else {
            newScan = (unsigned int)parsed - 1;
        }
    }

    // two optional argument can shift the scan start and end pointers
    // but let's initialize them with the values from the selected scan
    ppStart  = rte.xlrdev.getScan( newScan ).start();
    ppLength = rte.xlrdev.getScan( newScan ).length();
    ppEnd    = ppStart + ppLength ;
    
    // as the offset might be given in time, we might need the data format 
    // to compute the data rate, do that data check only once
    bool            data_checked = false;
    const int64_t   ppStartOrg = ppStart; // need to remember this 
    data_check_type found_data_type;

    for ( unsigned int argument_position = 2; argument_position < min((size_t)4, args.size()); argument_position++ ) {
        const string  arg = OPTARG(argument_position, args);

        // The defaults for the arguments have alreday been set
        // so if nothing specified we can skip the arg
        if( arg.empty() )
            continue;

        // for the start byte offset, we have the option to set it to 
        // s (start), c (center), e (end) and s+ (a bit past start)
        if( argument_position==2 ) {
            bool recognized = true;
            if( arg=="s" ) { 
                // this is a the same as the default
            } else if( arg=="s+" ) {
                // 'a bit past start'
                ppStart += 65536;
            } else if( arg=="c" )  {
                ppStart += ppLength/2;
            } else if( arg=="e" ) {
                // as per documentation, ~1M before end
                ppStart = ppEnd - 1000000;
            } else {
                recognized = false;
            }
            // If we already recognized the start value, we're done
            if( recognized )
                continue;
        }
        // Ok, it wasn't any of the special start values
        // first try to interpret it as a time
        bool         is_time = true;
        bool         relative_time = false;
        struct ::tm  parsed_time;
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
            relative_time = ( (arg[0] == '+') || (arg[0] == '-') );
            
            ASSERT_COND( parse_vex_time(arg.substr(relative_time ? 1 : 0), parsed_time, microseconds) > 0 );
        }
        catch ( ... ) {
            // ok, probably wasn't a time then!
            is_time = false;
        }

        if ( is_time ) {
            // We can only do this if the disks are not being used
            INPROGRESS(rte, reply, streamstorbusy(ctm))

            // we need a data format to compute a byte offset from the time offset 
            if ( !data_checked ) {
                uint64_t                  scan_length = (uint64_t)ppLength;
                static const unsigned int bytes_to_read = 1000000 & ~0x7;  // read 1MB, be sure it's a multiple of 8

                if ( bytes_to_read > scan_length ) {
                    reply << " 4 : scan is too short to check data format needed to compute byte offset in scan ;";
                    return reply.str();
                }

                bool                   failed = false;
                playpointer            start( ppStart );
                playpointer            end( ppStart + ppLength );
                const unsigned int     track = 4; // have to pick one
                auto_ptr<XLR_Buffer>   buffer(new XLR_Buffer(bytes_to_read));
                streamstor_reader_type data_reader( rte.xlrdev.sshandle(), start, end );

                data_reader.read_into( (unsigned char*)buffer->data, 0, bytes_to_read );
                
                if ( !find_data_format( (unsigned char*)buffer->data, bytes_to_read, track, true, found_data_type) ) {
                    failed = true;
                }
                else if ( found_data_type.is_partial() ) {
                    // look at the end of the scan to complete the data check
                    uint64_t offset = (scan_length - bytes_to_read) & ~0x7;

                    // round the start of data to read to a multiple of 
                    // VDIF frame size, in the hope of being on a frame
                    // boundary
                    if( is_vdif(found_data_type.format) )
                        offset = ((scan_length-bytes_to_read) / found_data_type.vdif_frame_size) * found_data_type.vdif_frame_size;

                    data_reader.read_into( (unsigned char*)buffer->data, offset, bytes_to_read );
                    data_check_type end_data_type = found_data_type;
                    headersearch_type header_format
                        ( found_data_type.format, 
                          found_data_type.ntrack, 
                          (is_vdif(found_data_type.format) ? headersearch_type::UNKNOWN_TRACKBITRATE : found_data_type.trackbitrate), 
                          (is_vdif(found_data_type.format) ? found_data_type.vdif_frame_size - headersize(found_data_type.format, 1): 0)
                          );
                    if ( !is_data_format( (unsigned char*)buffer->data, bytes_to_read, track, header_format, true, found_data_type.vdif_threads, end_data_type.byte_offset, end_data_type.time, end_data_type.frame_number) ) {
                        failed = true;
                    }
                    else if ( !combine_data_check_results(found_data_type, end_data_type, offset) ) {
                        failed = true;
                    }
                }
                if ( failed ) {
                    reply << " 4 : failed to find data format needed to compute byte offset in scan ;";
                    return reply.str();
                }
            }
            data_checked = true;
            // We really must know the recorded data rate!
            EZASSERT2(found_data_type.trackbitrate!=headersearch_type::UNKNOWN_TRACKBITRATE, cmdexception,
                      EZINFO("could not deduce recorded data rate for time based scan_set"));
            headersearch_type headersearch 
                ( found_data_type.format, 
                  found_data_type.ntrack, 
                  found_data_type.trackbitrate, 
                  (is_vdif(found_data_type.format) ? found_data_type.vdif_frame_size - headersize(found_data_type.format, 1): 0)
                  );
            // data rate in B/s, taking into account header overhead
            const uint64_t data_rate = (uint64_t)::round(
              boost::rational_cast<double>(
                headersearch.trackbitrate *
                headersearch.ntrack * 
                ( is_vdif(found_data_type.format) ? found_data_type.vdif_threads : 1 ) *
                headersearch.framesize /
                headersearch.payloadsize / 
                8 ));
            
            if ( relative_time ) {
                // the year (if given) is ignored
                uint64_t     byte_offset;
                unsigned int seconds = seconds_in_year( parsed_time );

                byte_offset = (uint64_t)round( (seconds + microseconds / 1e6) * data_rate );

                // Negative time offsets count backwards from end of scan, for
                // both 'start' and 'end' values. 
                if ( arg[0] == '-' ) {
                    byte_offset = ppEnd - byte_offset;
                } else {
                    byte_offset = ppStart + byte_offset;
                }
                // now check wether we're changing start or end
                if( argument_position==2 )
                    ppStart = byte_offset;
                else
                    ppEnd   = byte_offset;
            }
            else {
                uint64_t     byte_offset;
                // re-run the parsing, but now default it to the data time
                ASSERT_COND( gmtime_r(&found_data_type.time.tv_sec, &parsed_time ) );
                microseconds = (unsigned int)::round( boost::rational_cast<double>(found_data_type.time.tv_subsecond * 1000000) );
                
                unsigned int fields = parse_vex_time(args[argument_position], parsed_time, microseconds);
                ASSERT_COND( fields > 0 );

                time_t requested_time = ::mktime( &parsed_time );
                ASSERT_COND( requested_time != (time_t)-1 );

                // check if the requested time if before or after the data time
                if ( (requested_time < found_data_type.time.tv_sec) || 
                     ((requested_time == found_data_type.time.tv_sec) && (microseconds < (found_data_type.time.tv_subsecond * 1000000))) ) {
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
                               (microseconds >= (found_data_type.time.tv_subsecond*1000000)) ) );

                unsigned int seconds = requested_time - found_data_type.time.tv_sec;

                // When computing the byte offset, take into account that it is
                // relative to the *original* start of the scan! (We have to be prepared
                // to deal with the fact that the actual start position may have been shifted, e.g.
                // if we're currently parsing the end byte)
                byte_offset = ppStartOrg + seconds * data_rate 
                    - (uint64_t)::round(
                        boost::rational_cast<double>(
                            (found_data_type.time.tv_subsecond.as<highresdelta_type>() - highresdelta_type(microseconds, 1000000)) * data_rate) )
                    + found_data_type.byte_offset;

                // Not relative time; offset is wrt to start of scan
                // So all we need to do is see which one we're changing
                if( argument_position==2 )
                    ppStart = byte_offset;
                else
                    ppEnd   = byte_offset;

            }
        }
        else {
            // failed to parse input as time, should be byte offset then
            char*      endptr;
            int64_t    byte_offset;
            const char startchar( arg[0] );

            if( startchar!='+' && startchar!='-' ) {
                reply << " 8 : absolute byte numbers are not supported by scan_set ;";
                return reply.str();
            }

            byte_offset = strtoll(arg.c_str(), &endptr, 0);
            
            if( ! ((*endptr == '\0') && 
                   ( ((byte_offset != std::numeric_limits<int64_t>::max())
                        && (byte_offset != std::numeric_limits<int64_t>::min())) ||
                     (errno!=ERANGE))) ) {
                reply << " 8 : failed to parse byte offset or time code from '" << arg << "' ;";
                return reply.str();
            }

            // Parsing of byte offset depends on wether we're parsing start or end byte
            // Note that "ppEnd" positive offset is counted from a (potentially) shifted
            // start position, "ppStart" positive offset is always counted from the original
            // start-of-scan
            if( argument_position==2 )
                ppStart = ((byte_offset < 0) ? (ppEnd + byte_offset) : (ppStartOrg + byte_offset));
            else 
                ppEnd   = ((byte_offset < 0) ? (ppEnd + byte_offset) : (ppStart    + byte_offset));
        }
    }

    // check that the byte positions are within scan bounds
    // if not, set them to the scan bound themselves and return error 8
    ROScanPointer current_scan = rte.xlrdev.getScan( newScan );
    
    if ( ((uint64_t)ppStart < current_scan.start()) ||
         ((uint64_t)ppEnd > (current_scan.start() + current_scan.length())) ) {
        reply << " 8 : scan pointer offset(s) are out of bound ;";
        return reply.str();
    }
    // Great. All checks out, can now set the scan for real
    // After having set it, we modify the playpointers in the runtime
    rte.setCurrentScan( newScan );
    rte.pp_current = ppStart;
    rte.pp_end     = ppEnd;

    reply << " 0 ;";

    return reply.str();
}
