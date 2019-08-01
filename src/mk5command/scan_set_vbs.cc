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
#include <countedpointer.h>

#include <algorithm>
#include <iostream>

using namespace std;


string scan_set_vbs_fn(bool q, const vector<string>& args, runtime& rte) {
    // note that we store current_scan zero based, 
    // but user communication is one based
    mk6info_type&              mk6info( rte.mk6info );
    ostringstream              reply;
    scanlist_type&             dirList = mk6info.dirList;
    const transfer_type        ctm( rte.transfermode );
    const bool                 isRecording( ctm==vbsrecord || ctm==fill2vbs );
    static per_runtime<string> previous_search_string;

    // The new scan settings
    string                           scanName = mk6info.scanName;
    scanlist_type::reverse_iterator  next_scan = (scanName.empty() ? dirList.rend() :
                                                                     std::find(dirList.rbegin(), dirList.rend(), scanName));
    countedpointer<vbs_reader_base>  vbsrec;

    reply << "!" << args[0] << (q?('?'):('='));

    if ( q ) {
        reply << " 0 : " <<
                 ((next_scan!=dirList.rend()) ? std::distance(next_scan, dirList.rbegin()) : -1 ) << 
                 " : " << scanName << " : " << mk6info.fpStart << " : " << mk6info.fpEnd << " ;";
        return reply.str();
    }



    // Without any arguments at all or no explicit scan name, set last scan that wasn't recording?
    if ( args.size() < 2 || args[1].empty() ) {
        // Are there any scans at all?
        if( dirList.size()==0 ) {
            reply << " 6 : there are no scans recorded in this session ;";
            return reply.str();
        }

        // Because we're starting at the end, we must work our way back.  We
        // can safely move from .rend() to one position less  because we've
        // checked before that dirList has non-zero size

        // start with setting the last scan
        next_scan = dirList.rend();
        std::advance(next_scan, -1);

        // if we're currently recording, we must set the last-but-one scan,
        // if such a scan exists
        if( isRecording ) {
            // If we're pointing at .rbegin() there ain't not 'nuf scans
            if( next_scan==dirList.rbegin() ) {
                reply << " 6 : not enough scans recorded yet (recording first scan) ;";
                return reply.str();
            } 
            // Now we can safely move to the previous element
            std::advance(next_scan, -1);
        }

        scanName = *next_scan;

        // Now, if args.size()<2 (literal:
        // "scan_set=") we must return to
        // caller (setting defaults to whole scan), otherwise we must go on, processing other arguments
        if( args.size()<2 ) {
            mk6info.scanName = scanName;
            mk6info.fpStart  = 0;
            mk6info.fpEnd    = 0;
            reply << " 0 ;";
            return reply.str();
        }
    }

    // first argument is a scan number, search string, flexbuf recording name, "inc", "dec" or "next"
    // (the first argument being empty has already been handled just above)

    if ( args[1] == "inc" ) {
        if( next_scan==dirList.rend() ) {
            reply << " 6 : there is no recorded scan to increment from;";
            return reply.str();
        }
        std::advance(next_scan, 1);

        // and cycle through the end to the beginning
        if( next_scan==dirList.rend() )
            next_scan = dirList.rbegin();
        scanName = *next_scan;
    }
    else if ( args[1] == "dec" ) {
        if( next_scan==dirList.rend() ) {
            reply << " 6 : there is no recorded scan to decrement from ;";
            return reply.str();
        }

        if( next_scan==dirList.rbegin() )
            next_scan = dirList.rend();

        // again, because we've verified dirList is non-empty,
        // we can get away with doing just this
        std::advance(next_scan, -1);
        scanName = *next_scan;
    }
    else if( !args[1].empty() ) {
        char*    endptr;
        size_t   n2check = dirList.size();
        long int parsed  = strtol(args[1].c_str(), &endptr, 10);

        // Not a number or invalid index?
        if ( (*endptr != '\0') || (parsed < 1) || (parsed > (long int)dirList.size()) ) {
            // try to interpret it as a search string
            string   search_string = args[1];

            // Start at beginning of dirList
            // or continue from last (in case of 'next')
            // 'next_scan' already points at the scan in dirList with the
            // last found scan in that case.
            if ( search_string == "next" ) {
                if ( previous_search_string.find(&rte) == previous_search_string.end() ) {
                    reply << " 4 : no search string given yet ;";
                    return reply.str();
                }
                search_string = previous_search_string[&rte];

                std::advance(next_scan, 1);
            } else {
                // No matter where the current scan was pointing at,
                // we need search from the beginning
                next_scan = dirList.rbegin();
            }

            // remember search string
            previous_search_string[&rte] = search_string;

            // search string is case insensitive, convert both to uppercase
            search_string = toupper(search_string);
            
            // Check for next match! 
            while( n2check ) {
                // cycle through the end of the dirList
                if( next_scan==dirList.rend() )
                    next_scan = dirList.rbegin();
                // Are we now at a scan we were looking for?
                if( toupper(*next_scan).find(search_string) != string::npos )
                    break;
                std::advance(next_scan, 1);
                n2check--;
            }
            // Checked all recorded scans without match?
            // then we must see if we can open a recording with the
            // given name
            if ( n2check==0 ) {
                // case of 'search_string' may have been changed wrt to what
                // the user entered (searching in the list is done case
                // insensitive) so we must revert to the original
                // spelling
                search_string = previous_search_string[&rte];

                // 'vbsrec' is a counted pointer which will be
                // checked later, below. If we have the recording
                // already opened here, we don't have to do it later on
                try {
                    vbsrec = new vbs_reader_base(search_string, mk6info.mountpoints);
                }
                catch( const vbs_reader_except& e) {
                    // We've checked all entries of dirList and tried to
                    // open a recording but everything failed.
                    reply << " 8 : " << e.what() << " ;";
                    return reply.str();
                }
                // Ok!
                scanName = search_string;
            } else {
                // found a matching scan in dirList; next_scan points at it
                scanName = *next_scan;
            }
        }
        else {
            // number within range 1-length(dirList)
            next_scan = dirList.rbegin();
            std::advance(next_scan, parsed-1);
            scanName = *next_scan;
        }
    }

    // post condition of parsing argument 1 is:
    //  (1) either we don't end up here at all [returned to caller with an
    //      error
    //  (2) 'scanName' has been set to the correct vbs scan
    //
    // If 'vbsrec' is non-null it has already been opened and as such we
    // don't have to do that again
    if( !vbsrec )
        vbsrec = new vbs_reader_base(scanName, mk6info.mountpoints);

    // two optional argument can shift the scan start and end pointers
    
    // as the offset might be given in time, we might need the data format 
    // to compute the data rate, do that data check only once.
    // Start with defaults: whole scan
    bool            data_checked = false;
    off_t           fpStart( 0 ), fpEnd( vbsrec->length() );
    data_check_type found_data_type;

    fpStart = 0;
    fpEnd   = vbsrec->length();

    for ( unsigned int argument_position = 2; argument_position < min((size_t)4, args.size()); argument_position++ ) {
        const string  arg( args[argument_position] );

        // The defaults have already been set
        if( arg.empty() )
            continue;

        // for the start byte offset, we have the option to set it to 
        // s (start), c (center), e (end) and s+ (a bit past start)
        if ( argument_position==2 ) {
            bool recognized = true;
            if ( arg == "s" ) {
                // Synonym for the default
            }
            else if ( arg == "c" ) {
                fpStart = vbsrec->length() / 2;
            }
            else if ( arg == "e" ) {
                // as per documentation, ~1M before end
                fpStart = vbsrec->length() - 1000000;
            }
            else if ( arg == "s+" ) {
                fpStart = 65536;
            } else {
                recognized = false;
            }
            // If we recognized this one, we're done for now
            if( recognized )
                continue;
        }
        // Ok, first try to interpret it as a time
        bool         is_time = true;
        bool         relative_time;
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
            is_time = false;
        }

        if ( is_time ) {
            // We can only do this if the disks are not being used
            INPROGRESS(rte, reply, (ctm==vbsrecord || ctm==fill2vbs))

            // we need a data format to compute a byte offset from the time offset 
            if ( !data_checked ) {
                uint64_t                  scan_length = vbsrec->length();
                static const unsigned int bytes_to_read = 10000000 & ~0x7;  // read 10MB, be sure it's a multiple of 8
                if ( bytes_to_read > scan_length ) {
                    reply << " 4 : scan is too short to check data format needed to compute byte offset in scan ;";
                    return reply.str();
                }
                countedpointer<XLR_Buffer> buffer(new XLR_Buffer(bytes_to_read));

                vbsrec->read_into( (unsigned char*)buffer->data, 0, bytes_to_read );
                
                bool               failed = false;
                const unsigned int track = 4; // have to pick one
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

                    vbsrec->read_into( (unsigned char*)buffer->data, offset, bytes_to_read );
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
                DEBUG(3, "scan_set: detected data format " << found_data_type << endl);
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
                int64_t      byte_offset;
                unsigned int seconds = seconds_in_year( parsed_time );

                byte_offset = (int64_t)round( (seconds + microseconds / 1e6) * data_rate );

                // Negative values are wrt to end of scan
                if ( arg[0] == '-' ) {
                    byte_offset = fpEnd - byte_offset;
                } else {
                    byte_offset = fpStart + byte_offset;
                }
                // if parsing end byte, it's relative wrt start pos
                if ( argument_position == 2 )
                    fpStart = byte_offset;
                else
                    fpEnd   = byte_offset;
            }
            else {
                // re-run the parsing, but now default it to the data time
                ASSERT_COND( gmtime_r(&found_data_type.time.tv_sec, &parsed_time ) );
                microseconds = (unsigned int)::round( boost::rational_cast<double>(found_data_type.time.tv_subsecond * 1000000) );
                
                unsigned int fields = parse_vex_time(args[argument_position], parsed_time, microseconds);
                ASSERT_COND( fields > 0 );

                time_t requested_time = ::mktime( &parsed_time );
                ASSERT_COND( requested_time != (time_t)-1 );

                // check if the requested time if before or after the data time
                if ( (requested_time < found_data_type.time.tv_sec) || 
                     ((requested_time == found_data_type.time.tv_sec) && (microseconds < (found_data_type.time.tv_subsecond*1000000)) ) ) {
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
                // the byte offset for absolute time is always wrt to start of scan, i.e. wrt to '0' because
                // on vbs it's "just a file"
                int64_t      byte_offset = seconds * data_rate 
                    - (uint64_t)::round(
                      boost::rational_cast<double>(
                          (found_data_type.time.tv_subsecond.as<highresdelta_type>() - highresdelta_type(microseconds, 1000000)) * data_rate) )
                    + found_data_type.byte_offset;

                if ( argument_position == 2 ) 
                    fpStart = byte_offset;
                else
                    fpEnd   = byte_offset;
            }
        }
        else {
            // failed to parse input as time, should be byte offset then
            char*   endptr;
            int64_t byte_offset;

            // According to scan_set= docs, byte numbers may ONLY be relative!
            if( arg[0]!='+' && arg[0]!='-' ) {
                reply << " 8 : only relative byte numbers allowed in this command ;";
                return reply.str();
            }

            errno = 0;
            byte_offset = strtoll(arg.c_str(), &endptr, 0);
            ASSERT2_COND( *endptr=='\0' && endptr!=arg.c_str() && errno==0,
                          SCINFO(" failed to parse byte offset or time code from '" << arg << "'") );

            if ( byte_offset<0 )
                byte_offset += fpEnd;
            else if( arg[0]=='+' )
                byte_offset += fpStart;

            // Check which pointer we're adjusting
            if ( argument_position == 2 ) 
                fpStart = byte_offset;
            else
                fpEnd   = byte_offset;
        }
    }

    // check that the byte positions are within scan bounds
    // if not, set them to the scan bound themselves and return error 8
    if ( (fpStart < 0) || (fpEnd > vbsrec->length()) ) {
        reply << " 8 : scan pointer offset(s) are out of bound ;";
        return reply.str();
    }

    // Now we can safely transfer the values of the new current scan into
    // the runtime/mk6info
    mk6info.scanName = scanName;
    mk6info.fpStart  = fpStart;
    mk6info.fpEnd    = fpEnd;

    reply << " 0 ;";

    return reply.str();
}
