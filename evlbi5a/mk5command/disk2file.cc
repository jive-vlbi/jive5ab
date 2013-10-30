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
#include <threadfns.h>
#include <tthreadfns.h>
#include <iostream>

using namespace std;


void disk2fileguard_fun(runtime* rteptr) {
    try {
        DEBUG(3, "disk2file guard function: transfer done" << endl);
        // apparently we need to call stop twice
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        
        if( rteptr->disk_state_mask & runtime::play_flag )
            rteptr->xlrdev.write_state( "Played" );

        RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "disk2file guard threw an exception: " << e.what() << std::endl );
        rteptr->transfermode = no_transfer;
    }
    catch ( ... ) {
        DEBUG(-1, "disk2file guard threw an unknown exception" << std::endl );        
        rteptr->transfermode = no_transfer;
    }
}

string disk2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    static per_runtime<chain::stepid>  disk_stepid;
    static per_runtime<chain::stepid>  file_stepid;
    static per_runtime<diskreaderargs> disk_args;
    static per_runtime<string>         file_name;
    static per_runtime<string>         open_mode;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    reply << "!" << args[0] << ((qry)?('?'):('='));

    if ( qry ) {
        if ( (ctm == disk2file) && (rte.transfersubmode & run_flag) ) {
            ASSERT_COND( disk_stepid.find(&rte) != disk_stepid.end() && 
                         disk_args.find(&rte) != disk_args.end() &&
                         file_name.find(&rte) != file_name.end() &&
                         open_mode.find(&rte) != open_mode.end() &&
                         file_stepid.find(&rte) != file_stepid.end() );

            uint64_t start = disk_args[&rte].pp_start.Addr;
            uint64_t current = rte.statistics.counter(disk_stepid[&rte]) + start;
            uint64_t end = disk_args[&rte].pp_end.Addr;
            reply << " 0 : active : " << file_name[&rte] << " : " << start << " : " << current << " : " << end << " : " << open_mode[&rte];

            // print the bytes to cache if it isn't the default 
            // (to stay consistent with documentation)
            uint64_t bytes_to_cache = rte.processingchain.communicate(file_stepid[&rte], &fdreaderargs::get_bytes_to_cache);
            if ( bytes_to_cache != numeric_limits<uint64_t>::max() ) {
                reply << " : " << bytes_to_cache;
            }

            reply << " ;";
        }
        else {
            reply << " 0 : inactive";
            if ( file_name.find(&rte) != file_name.end() ) {
                reply << " : " << file_name[&rte];
            }
            reply << " ;";
        }
    }
    else {
        if ( ctm != no_transfer ) {
            reply << " 6 : doing " << ctm << " cannot start " << args[0] << "; ";
            return reply.str();
        }

        if ( (args.size() > 1) && !args[1].empty() ) {
            file_name[&rte] = args[1];
        }
        else {
            ROScanPointer current_scan = rte.xlrdev.getScan(rte.current_scan);
            if ( rte.ioboard.hardware() & ioboard_type::dim_flag)  {
                mk5b_inputmode_type curipm;
                rte.get_input( curipm );
                char tmp[9];
                sprintf(tmp, "%08x", curipm.bitstreammask);
                file_name[&rte] = current_scan.name() + "_bm=0x" + tmp + ".m5b";
            }
            else {
                file_name[&rte] = current_scan.name() + ".m5a";
            }
        }

        char* eocptr;
        off_t start;
        if ( (args.size() > 2) && !args[2].empty() ) {
            errno = 0;
            start = ::strtoull(args[2].c_str(), &eocptr, 0);
            ASSERT2_COND( start >= 0 && !(start==0 && eocptr==args[2].c_str()) && !((uint64_t)start==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'start' value") );
        }
        else {
            start = rte.pp_current.Addr;
        }
        off_t end;
        if ( (args.size() > 3) && !args[3].empty() ) {
            bool plus = (args[3][0] == '+');
            const char* c_str = args[3].c_str() + ( plus ? 1 : 0 );
            errno = 0;
            end   = ::strtoull(c_str, &eocptr, 0);
            ASSERT2_COND( end >= 0 && !(end==0 && eocptr==c_str) && !((uint64_t)end==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'end' value") );
            if ( plus ) {
                end += start;
            }
        }
        else {
            end = rte.pp_end.Addr;
        }
        
        // sanity checks
        uint64_t length = ::XLRGetLength(rte.xlrdev.sshandle());
        ASSERT2_COND( (start >= 0) && (start < end) && (end <= (int64_t)length), SCINFO("start " << start << " and end " << end << " values are not valid for a recording of length " << length) );

        if ( args.size() > 4 ) {
            if ( !(args[4] == "n" || args[4] == "w" || args[4] == "a") ) {
                reply << " 8 : open mode must be n, w or a ;";
                return reply.str();
            }
            open_mode[&rte] = args[4];
        }
        else {
            open_mode[&rte] = "n";
        }

        uint64_t bytes_to_cache = numeric_limits<uint64_t>::max();
        if ( args.size() > 5 ) {
            errno          = 0;
            bytes_to_cache = ::strtoull(args[5].c_str(), &eocptr, 0);
            ASSERT2_COND( !(bytes_to_cache==0 && eocptr==args[5].c_str()) && !(bytes_to_cache==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'bytes to cache' value") );
        }

        disk_args[&rte] = diskreaderargs( &rte );
        disk_args[&rte].set_start( start );
        disk_args[&rte].set_end( end );
        disk_args[&rte].set_variable_block_size( true );
        disk_args[&rte].set_run( true );

        const headersearch_type dataformat(fmt_none, 0 /*rte.ntrack()*/, 0 /*(unsigned int)rte.trackbitrate()*/, 0 /*rte.vdifframesize()*/);
        rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

        chain c;
        disk_stepid[&rte] = c.add( diskreader, 10, disk_args[&rte] );
        file_stepid[&rte] = c.add( &fdwriter<block>, &open_file, file_name[&rte] + "," + open_mode[&rte], &rte ); 
        c.register_cancel( file_stepid[&rte], &close_filedescriptor );

        // And register the cleanup function
        c.register_final(&disk2fileguard_fun, &rte);

        XLRCODE(SSHANDLE ss( rte.xlrdev.sshandle() ));
        XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
        XLRCALL( ::XLRBindOutputChannel(ss, 0) );
        XLRCALL( ::XLRSelectChannel(ss, 0) );

        // reset statistics counters
        rte.statistics.clear();

        // install and run the chain
        rte.processingchain = c;

        rte.processingchain.run();

        rte.processingchain.communicate(file_stepid[&rte], &fdreaderargs::set_bytes_to_cache, bytes_to_cache);

        rte.transfersubmode.clr_all().set( run_flag );
        rte.transfermode = disk2file;
        
        reply << " 1 ;";
    }

    return reply.str();
    
}
