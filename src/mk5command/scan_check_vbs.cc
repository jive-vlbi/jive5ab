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
#include <per_runtime.h>
#include <iostream>

using namespace std;

//
// Usage:
// 
// scan_check ? [ strictness ] [ : number of bytes to read ]
// file_check ? [ strictness ] : [ number of bytes to read ] : file name
//
// Extra configuration options (set/get) for the algorithm.
// Each parameter takes a settable value of "reset" as argument which means
// to reset the value to the compiled-in defaults (see scan_check.{h,cc})
//
// * Verbosity:
// (scan|file)_check ? verbose
//   !(scan|file)_check ? 0 : verbose : (true|false)
//   explicitly ask for verbosity value in the current runtime
//
// Set verbose to an explicit value in the current runtime
// (scan|file)_check = verbose : {true|1|false|0|reset} ;
//
// * Strictness
//   Only as settable parameter to (re)set a (new) default; can still be
//   overridden on a per-invocation base. Because of it being an "officially
//   supported optional argument applying to the current query" we cannot
//   query the current value (unless we define a new protocol for
//   setting/querying the configurable parameters. So:
//
//   (scan|file)_check = strict : {true|1|false|0|reset} ;
//
// * Allow fine tuning the scan_check algorithm parameters; the default(s)
//   were not always optimal for (very) high data rates and/or different
//   recording strategies. So we had to go away from the "the scan_check algorithm does
//   not assume /anything/ about the recording" to enabling some hints to
//   help it maximize finding all information in as little time as possible
//
// VBS/Mk6 data is chunked in 256/10 MB chunks (defaults) but given that we
// do not make assumptions about the underlying recorder we don't know which
// one to chose. Also, depending on the recording configuration, it may
// happen or not that all frames in a chunk are from the same VDIF thread or
// not. By allowing this high-level parameter to be set (default: 256 MB, we
// *are* VBS/FlexBuff minded ofc :innocent: ) we give stations the freedom
// to adapt to /their/ situation in stead of hardcoding a solution.
//
// (scan|file)_check = canonical_chunk_size : {size [kM] | "net_protocol" | "reset" }
//     Set the canonical chunk size for the recording to be checked to size
//     bytes (with base 1024 kM support). The string "net_protocol" means
//     to take the value from net_protocol's i/o block size - for
//     flexbuff/mk6 recorders that value *is* the canonical blocking size.
//
// (scan|file)_check ? canonical_chunk_size ;
//      get the current canonical chunk size in the runtime
//
//
// * Make the hard-coded default of bytes_to_read (=1_000_000 bytes) per sample point
// configurable. It is possible to override this number in each
// (scan|file)_check call, but it could be more convenient to make it a
// settable parameter that persists across (scan|file)_check invocations.
//
// (scan|file)_check = bytes_to_read : {size [kM] | "reset" } ;
// (scan|file)_check ? bytes_to_read ;
//
// * Make the max number of samplings a settable parameter.
// The defaults are backwards compatible with reading 'at most 8 MB' of data
// (for sampling a VDIF recording). For higher data rates and/or multiple
// threads it is possible that you need to sample at more points in the
// recording.
// The code sets a hard limit on this value of 2**31 at most
//
// (scan|file)_check = max_sample : {number | "reset"}
//     Set the maximum number of sample points to <number>, or reset it to
//     whatever the compiled-in default is
//
// (scan|file)_check ? max_sample ;
//      get the current maximum number of samplings in the runtime
//
// * The maximum amount of data to read during "scan_check?"
// The algorithm will try to figure out a maximum number of sampling points
// by different methods (to be able to make a balanced choice), so
// it is also possible to set an overall limit on how much data the
// algorithm should parse in a scan_check? invocation.
// The defaults are backwards compatible with reading 'at most 8 MB' of data
// (for sampling a VDIF recording). For higher data rates and/or multiple
// threads it is possible that you need to read more data, or, sample more
// often and read less than 1 MB per sampling (the default).
//
// (scan|file)_check = max_read : {size [kM] | "reset" }
//     Set the maximum number of bytes the algorithm shall process;
//     "reset" resets the value to whatever the compiled-in default is
//
// (scan|file)_check ? max_read ;
//      get the maximum number of bytes setting in the current runtime
//
// The code imposes a hard limit of at most 2 GB for this setting
//
//
string scan_check_vbs_fn(bool q, const vector<string>& args, runtime& rte) {
    const bool    from_file       = ( args[0] == "file_check" );
    const bool    have_streamstor = ( rte.ioboard.hardware() & ioboard_type::streamstor_flag );
    const bool    is_dim          = ( rte.ioboard.hardware() & ioboard_type::dim_flag );
    ostringstream reply;

    reply << "!" << args[0] << (q?('?'):('=')) ;

    if( !q ) {
        const string            cmd_s( ::tolower(OPTARG(1, args)) );
        scan_check_config_type& config = rte.scan_check_config;

        if( cmd_s=="verbose" || cmd_s=="strict" ) {
            const bool   isVerboseCmd( cmd_s=="verbose" );
            bool&        b_ref( cmd_s=="verbose" ? config.verbose : config.strict );
            const string verbose_arg = ::tolower( OPTARG(2, args) );

            if( verbose_arg.empty() ) {
                reply << " 8 : " << cmd_s << " command needs an argument ;";
                return reply.str();
            }

            if( verbose_arg=="1" || verbose_arg=="true" )
                b_ref = true;
            else if( verbose_arg=="0" || verbose_arg=="false" )
                b_ref = false;
            else if( verbose_arg=="reset" )
                b_ref = (isVerboseCmd ? scan_check_config_type::defVerbose : scan_check_config_type::defStrict);
            else {
                reply << " 8 : unsupported argument to verbose command (not 0, false, 1, true, reset) ;";
                return reply.str();
            }
            reply << " 0 ;";
            return reply.str();
        } else if( cmd_s=="bytes_to_read" || cmd_s=="canonical_chunk_size" || cmd_s=="max_read") {
            const bool              isBytesToReadCmd( cmd_s=="bytes_to_read" );
            const bool              isMaxReadCmd( cmd_s=="max_read" );
            uint64_t&               v_ref( isBytesToReadCmd ? config.bytes_to_read : (isMaxReadCmd ? config.maxRead : config.canonical_chunk_size) );
            const string            size_arg = OPTARG(2, args);

            if( size_arg.empty() ) {
                reply << " 8 : " << cmd_s << " command needs an argument ;";
                return reply.str();
            }

            // Could be "reset"
            if( ::tolower(size_arg)=="reset" ) {
                v_ref = (isBytesToReadCmd ? scan_check_config_type::defBytesToRead : (isMaxReadCmd ? scan_check_config_type::defMaxRead : scan_check_config_type::defCanonicalChunkSize) );
            } else if( ::tolower(size_arg)=="net_protocol" ) {
                // only applies to canonical_chunk_size
                EZASSERT2( !(isBytesToReadCmd || isMaxReadCmd) ,
                           cmdexception,
                           EZINFO(" the `net_protocol` value only applies to the canonical_chunk_size parameter ;") );
                // indicate: take from net_protocol
                // there is no other way to set the value to zero - the code
                // below does not accept setting the value to 0 manually
                config.canonical_chunk_size = 0; 
            } else {
                char*             eptr;
                unsigned long int size = ::strtoull(size_arg.c_str(), &eptr, 0);

                // was a unit given? [note: all whitespace has already been stripped
                // by the main commandloop]
                EZASSERT2( eptr!=size_arg.c_str() && ::strchr("kM\0", *eptr),
                           cmdexception,
                           EZINFO("invalid size argument '" << size_arg << "'") );

                // Now we can do this
                size = size * ((*eptr=='k')?KB:(*eptr=='M'?MB:1));

                // And perform some sanity checks
                EZASSERT2( size <= (2ULL * KB * KB * KB),
                           cmdexception,
                           EZINFO("maximum value for size is 2 GB") );

                if( isBytesToReadCmd ) 
                    size &= ~0x7; // be sure it's a multiple of 8

                EZASSERT2( size > 0,
                           cmdexception,
                           EZINFO("need to configure a non-zero value") );

                // Now we can safely set the value
                v_ref = static_cast<uint64_t>(size);
            }
            reply << " 0 ;";
            return reply.str();
        } else if( cmd_s=="max_sample" ) {
            const string            size_arg = OPTARG(2, args);

            if( size_arg.empty() ) {
                reply << " 8 : " << cmd_s << " command needs an argument ;";
                return reply.str();
            }

            // Could be "reset"
            if( ::tolower(size_arg)=="reset" ) {
                config.maxSample = scan_check_config_type::defMaxSample;
            } else {
                char*             eptr;
                unsigned long int size = ::strtoull(size_arg.c_str(), &eptr, 0);

                // Make sure there's nothing following the number
                EZASSERT2( eptr!=size_arg.c_str() && ::strchr( "\0", *eptr),
                           cmdexception,
                           EZINFO("not a number '" << size_arg << "'") );

                // And perform some sanity checks
                EZASSERT2( size <= (2ULL * KB * KB * KB),
                           cmdexception,
                           EZINFO("maximum value for size exceeded ") );

                EZASSERT2( size > 0,
                           cmdexception,
                           EZINFO("need to configure a non-zero value") );

                // Now we can safely set the value
                config.maxSample = static_cast<unsigned int>(size);
            }
            reply << " 0 ;";
            return reply.str();
        } else if( !cmd_s.empty() ) {
            reply << " 8 : " << cmd_s << " - unrecognized command ;";
            return reply.str();
        }
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Check for extra-special specific query/ies
    const string                  arg1( ::tolower(OPTARG(1, args)) );
    scan_check_config_type const& ro_config = rte.scan_check_config;

    // The magic settable parameters
    if( arg1=="verbose" || arg1=="strict" || arg1=="bytes_to_read" || arg1=="canonical_chunk_size" || arg1=="max_sample" || arg1=="max_read" || arg1=="current_values" ) {
        // only accept if it's the *only* non-empty argument to the query
        vector<string>::const_iterator p = args.begin();

        // skip the first two known-good terms
        p++; p++;

        while( p!=args.end() ) {
           if( !p->empty() )
              break;
        }
        if( p!=args.end() ) {
            reply << " 8 : malformed parameter query, non-empty arguments found ;";
            return reply.str();
        }
        // At this point we know the input looked like:
        //  (scan|file)_check ? <parameter> ;
        const bool showAll = (arg1=="current_values");

        // We can start forming the start of the reply
        reply << " 0";
        if( !showAll )
            reply << " : " << arg1 << " : ";
        if( arg1=="verbose" || showAll )
            reply << (showAll ? " : verbose : " : "") << (ro_config.verbose ? "true" : "false");
        if( arg1=="strict" || showAll )
            reply << (showAll ? " : strict : " : "") << (ro_config.strict ? "true" : "false");
        if( arg1=="bytes_to_read" || showAll )
            reply << (showAll ? " : bytes_to_read : " : "") << ro_config.bytes_to_read;
        if( arg1=="max_sample" || showAll )
            reply << (showAll ? " : max_sample : " : "") << ro_config.maxSample;
        if( arg1=="max_read" )
            reply << (showAll ? " : max_read : " : "") << ro_config.maxRead;
        if( arg1=="canonical_chunk_size" || showAll ) {
            reply << (showAll ? " : canonical_chunk_size : " : "")
                  << (ro_config.canonical_chunk_size == 0 ? rte.netparms.get_blocksize() : ro_config.canonical_chunk_size);
            if( ro_config.canonical_chunk_size == 0 )
               reply << " (current net_protocol setting)";
        }
        reply << ";";
        return reply.str();
    }
    //
    // Handle the "strict" argument, if given
    //
    bool   strict = ro_config.strict ;//true;
    string strict_arg = ::tolower( OPTARG(1, args) );

    if ( !strict_arg.empty() ) {
        if (strict_arg == "0" || strict_arg == "false" ) {
            strict = false;
        } else if (strict_arg == "1" || strict_arg == "true" ) {
            strict = true;
        } else {
            reply << " 8 : strict argument `" << strict_arg << "` is not 0, false, true,  or 1 ;";
            return reply.str();
        }
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

        if( mk6info.scanName.empty() ) {
            reply << " 8 : no scan name given ;";
            return reply.str();
        }
        // cache was cleared but scan name not empty => try to open it
        if( !mk6info.fDescriptor )
            mk6info.fDescriptor = open_vbs(mk6info.scanName, mk6info.mountpoints, mk6info.tryFormat);
        // Construct reader from values set by "scan_set="
        data_reader = countedpointer<data_reader_type>( new vbs_reader_base(mk6info.fDescriptor.__m_fd,
                                                                            mk6info.fpStart, mk6info.fpEnd) );
        // unknown scan number but we DO know the scan name
        scan_id = " : ? : "+mk6info.scanName;
    }

    //
    // Handle the "bytes to read" argument, if given
    //
    string   bytes_to_read_arg = OPTARG(2, args);
    uint64_t bytes_to_read = ro_config.bytes_to_read; //1000000;  // read 1MB by default

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

    // Actually perform the analysis/algorithm
    // By saving the result we can output it as debug info in full and not
    // just the vsi/s summarised output
    // If the canonical chunk size was set to 0 it means
    //     "whatever was set in netparms".
    scan_check_config_type scct = ro_config;

    // Overrides - these are overridable on a per-scan_check call
    scct.strict        = strict;
    scct.bytes_to_read = bytes_to_read;
    if( scct.canonical_chunk_size== 0 )
        scct.canonical_chunk_size = rte.netparms.get_blocksize();
    scan_check_type sct( scan_check_fn(data_reader, scct) );

    DEBUG(4, sct << std::endl);

    // Form the reply:
    // <return code> : [<scan identification (number+name)] <scan check result>
    reply << " 0" << scan_id << vsi_format(sct, is_dim ? vsi_format::VSI_S_TOTALDATARATE : vsi_format::VSI_S_NONE);
    return reply.str();
}
