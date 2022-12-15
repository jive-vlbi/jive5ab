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
#include <scan_check.h>
#include <iostream>

using namespace std;

//
// Usage:
// (scan|file)_check ? verbose
//   !(scan|file)_check ? 0 : verbose : (true|false)
//   explicitly ask for verbosity value in the current runtime
// scan_check ? [ strictness ] [ : number of bytes to read ]
// file_check ? [ strictness ] : [ number of bytes to read ] : file name
//
// Set verbose to an explicit value in the current runtime
// (scan|file)_check = verbose : (true|1|false|0)
//

string scan_check_vbs_fn(bool q, const vector<string>& args, runtime& rte) {
    const bool    from_file       = ( args[0] == "file_check" );
    const bool    have_streamstor = ( rte.ioboard.hardware() & ioboard_type::streamstor_flag );
    const bool    is_dim          = ( rte.ioboard.hardware() & ioboard_type::dim_flag );
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if( !q ) {
        const string verbose_s( ::tolower(OPTARG(1, args)) );
        if( verbose_s=="verbose") {
            const string verbose_arg = ::tolower( OPTARG(2, args) );

            if( verbose_arg.empty() ) {
                reply << " 8 : verbose command needs an argument ;";
                return reply.str();
            }

            if( verbose_arg=="1" || verbose_arg=="true" )
                rte.verbose_scancheck = true;
            else if( verbose_arg=="0" || verbose_arg=="false" )
                rte.verbose_scancheck = false;
            else {
                reply << " 8 : unsupported argument to verbose command (not 0, false, 1, true) ;";
                return reply.str();
            }
            reply << " 0 ;";
            return reply.str();
        }
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Check for extra-special specific query
    const string arg1( ::tolower(OPTARG(1, args)) );
    if( arg1=="verbose" ) {
        // only accept if it's the *only* non-empty argument to the query
        vector<string>::const_iterator p = args.begin();

        // skip the first two known-good terms
        p++; p++;

        while( p!=args.end() ) {
           if( !p->empty() )
              break;
        }
        if( p!=args.end() ) {
            reply << " 8 : malformed verbose query, non-empty arguments found ;";
            return reply.str();
        }
        // At this point we know the input looked like:
        //  (scan|file)_check ? verbose
        reply << " 0 : verbose : " << (rte.verbose_scancheck ? "true" : "false") << " ;";
        return reply.str();
    }

    // Query is only available if disks are available/not busy
    INPROGRESS(rte, reply, have_streamstor && streamstorbusy(rte.transfermode))

    std::string                      scan_id; // for holding "<scan number> : <scan name>" if known
    countedpointer<data_reader_type> data_reader;
    if ( from_file ) {
        // explicit file_check? issued
        const string filename = OPTARG(3, args);
        if ( filename.empty() ) {
            reply << " 8 : no file name given ;";
            return reply.str();
        }
        data_reader = countedpointer<data_reader_type>( new file_reader_type(filename) );
        // do not touch scan_id; user has given file name specifically so no
        // need to reply it back to them
    } else if( have_streamstor ) {
        // scan_check? issued on Mark5A, B or C
        ROScanPointer      scan_pointer(rte.xlrdev.getScan(rte.current_scan));
        std::ostringstream id;

        data_reader = countedpointer<data_reader_type>( new streamstor_reader_type(rte.xlrdev.sshandle(),
                                                                                   rte.pp_current, rte.pp_end) );

        id << " : " << (rte.current_scan + 1) << " : " << scan_pointer.name();
        scan_id = id.str();

    } else {
        // scan_check? issued on a system that doesn't have streamstor => Mark6 or FlexBuff
        const mk6info_type& mk6info( rte.mk6info );

        if ( mk6info.scanName.empty() ) {
            reply << " 8 : no scan name given ;";
            return reply.str();
        }
        // Construct reader from values set by "scan_set="
        data_reader = countedpointer<data_reader_type>( (mk6info.scanName=="null") ? new null_reader_type() :
                                                        new vbs_reader_base(mk6info.scanName, mk6info.mountpoints,
                                                                            mk6info.fpStart,  mk6info.fpEnd) );
        // unknown scan number but we DO know the scan name
        scan_id = " : ? : "+mk6info.scanName;
    }

    //
    // Handle the "bytes to read" argument, if given
    //
    string   bytes_to_read_arg = OPTARG(2, args);
    uint64_t bytes_to_read = 1000000;  // read 1MB by default

    if ( !bytes_to_read_arg.empty() ) {
        char*             eptr;
        unsigned long int v = ::strtoull(bytes_to_read_arg.c_str(), &eptr, 0);

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
      reply << " 6 : scan too short to check, need " << bytes_to_read << "bytes have " << data_reader->length() << " ;";
      return reply.str();
    }

    //
    // Handle the "strict" argument, if given
    //
    bool   strict = true;
    string strict_arg = OPTARG(1, args);

    if ( !strict_arg.empty() ) {
        if (strict_arg == "0" ) {
            strict = false;
        }
        else if (strict_arg != "1" ) {
            reply << " 8 : strict argument has to be 0 or 1 ;";
            return reply.str();
        }
    }
    // Actually perform the analysis/algorithm
    // By saving the result we can output it as debug info in full and not
    // just the vsi/s summarised output
    scan_check_type sct( scan_check_fn(data_reader, bytes_to_read, strict, rte.verbose_scancheck) );

    DEBUG(4, sct << std::endl);

    // Form the reply:
    // <return code> : [<scan identification (number+name)] <scan check result>
    reply << " 0" << scan_id << vsi_format(sct, is_dim ? vsi_format::VSI_S_TOTALDATARATE : vsi_format::VSI_S_NONE);
    return reply.str();
}
