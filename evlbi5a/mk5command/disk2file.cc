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

struct d2f_data_type {
    string          open_mode;
    string          file_name;
    chain::stepid   disk_stepid;
    chain::stepid   file_stepid;
    diskreaderargs  disk_args;

    d2f_data_type():
        disk_stepid( chain::invalid_stepid ),
        file_stepid( chain::invalid_stepid )
    {}
};

typedef per_runtime<d2f_data_type>  d2f_data_store_type;


void disk2fileguard_fun(runtime* rteptr, d2f_data_store_type::iterator p) {
    try {
        DEBUG(3, "disk2file guard function: transfer done" << endl);
       
        // Do the streamstor stopping inside try/catch because we MUST
        // be able to close the file descriptor too 
        try {
            // apparently we need to call stop twice
            XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
            XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        }
        catch( const std::exception& e ) {
            DEBUG(-1, "disk2file guard function: failed to stop streamstor - " << e.what() << endl);
        }

        // Change disk state mask, if that's enabled
        if( rteptr->disk_state_mask & runtime::play_flag )
            rteptr->xlrdev.write_state( "Played" );


        // See, this is where we close the file
        rteptr->processingchain.communicate(p->second.file_stepid, &close_filedescriptor);

        // Don't need the step ids any more
        p->second.file_stepid = chain::invalid_stepid;
        p->second.disk_stepid = chain::invalid_stepid;
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "disk2file guard caught an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "disk2file guard caught an unknown exception" << std::endl );        
    }
    // No matter how we exited, this has to be done
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
}



string disk2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream               reply;
    const transfer_type         ctm( rte.transfermode ); // current transfer mode
    static d2f_data_store_type  d2f_data;

    reply << "!" << args[0] << ((qry)?('?'):('='));

    // Query may execute always, command only if nothing else happening
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer))

    if ( qry ) {
        // Attempt to locate data for this runtime
        d2f_data_store_type::const_iterator   ptr = d2f_data.find(&rte);

        if ( (ctm == disk2file) && (rte.transfersubmode & run_flag) ) {
            // BEFORE USING 'ptr' we MUST assert that 'ptr!=d2f_data.end()'!
            ASSERT_COND( ptr!=d2f_data.end() );
            const d2f_data_type&  d2f( ptr->second );
           
            // Now it's safe to use 'd2f.*' 
            uint64_t start   = d2f.disk_args.pp_start.Addr;
            uint64_t current = rte.statistics.counter(d2f.disk_stepid) + start;
            uint64_t end     = d2f.disk_args.pp_end.Addr;

            reply << " 0 : active : " << d2f.file_name << " : " << start << " : " << current << " : "
                  << end << " : " << d2f.open_mode;

            // print the bytes to cache if it isn't the default 
            // (to stay consistent with documentation)
            uint64_t bytes_to_cache = rte.processingchain.communicate(d2f.file_stepid, &fdreaderargs::get_bytes_to_cache);
            if ( bytes_to_cache != numeric_limits<uint64_t>::max() ) {
                reply << " : " << bytes_to_cache;
            }

            reply << " ;";
        }
        else {
            reply << " 0 : inactive";
            if( ptr!=d2f_data.end() )
                reply << " : " << ptr->second.file_name;
            reply << " ;";
        }
        return reply.str();
    }

    // Must have been a command!
    //
    // The following statement makes sure an entry will exist in d2f_data
    // and then we can take a reference to the mapped value for ez access
    d2f_data_store_type::iterator ptr = d2f_data.insert( make_pair(&rte, d2f_data_type()) ).first; 
    d2f_data_type&                d2f( ptr->second );

    d2f.file_name = OPTARG(1, args);

    // No file name given?
    if( d2f.file_name.empty() ) {
        string        suffix = ".m5a";
        ROScanPointer current_scan = rte.xlrdev.getScan(rte.current_scan);

        if ( rte.ioboard.hardware() & ioboard_type::dim_flag)  {
            char                tmp[9];
            mk5b_inputmode_type curipm;

            rte.get_input( curipm );
            sprintf(tmp, "%08x", curipm.bitstreammask);
            suffix = string("_bm=0x") + string(tmp) + ".m5b";
        }
        d2f.file_name = current_scan.name() + suffix;
    }

    // Parse data range - default to current set range
    char*  eocptr;
    off_t  start = rte.pp_current.Addr;
    off_t  end   = rte.pp_end.Addr;

    if ( (args.size() > 2) && !args[2].empty() ) {
        errno = 0;
        start = ::strtoull(args[2].c_str(), &eocptr, 0);
        ASSERT2_COND( start >= 0 && !(start==0 && eocptr==args[2].c_str()) && !((uint64_t)start==~((uint64_t)0) && errno==ERANGE),
                      SCINFO("Failed to parse 'start' value") );
    }

    if ( (args.size() > 3) && !args[3].empty() ) {
        const bool  plus  = (args[3][0] == '+');
        const char* c_str = args[3].c_str() + ( plus ? 1 : 0 );
        errno = 0;
        end   = ::strtoull(c_str, &eocptr, 0);
        ASSERT2_COND( end >= 0 && !(end==0 && eocptr==c_str) && !((uint64_t)end==~((uint64_t)0) && errno==ERANGE),
                      SCINFO("Failed to parse 'end' value") );
        if ( plus ) {
            end += start;
        }
    }
    
    // sanity checks
    uint64_t length = ::XLRGetLength(rte.xlrdev.sshandle());
    ASSERT2_COND( (start >= 0) && (start < end) && (end <= (int64_t)length),
                   SCINFO("start " << start << " and end " << end << " values are not valid for a recording of length " << length) );

    // Default open mode is 'n', unless overriden by user
    d2f.open_mode = "n";
    if ( args.size() > 4 ) {
        if ( !(args[4] == "n" || args[4] == "w" || args[4] == "a") ) {
            reply << " 8 : open mode must be n, w or a ;";
            return reply.str();
        }
        d2f.open_mode = args[4];
    }

    uint64_t bytes_to_cache = numeric_limits<uint64_t>::max();
    if ( args.size() > 5 ) {
        errno          = 0;
        bytes_to_cache = ::strtoull(args[5].c_str(), &eocptr, 0);
        ASSERT2_COND( !(bytes_to_cache==0 && eocptr==args[5].c_str()) && !(bytes_to_cache==~((uint64_t)0) && errno==ERANGE),
                      SCINFO("Failed to parse 'bytes to cache' value") );
    }

    // Now we're good to go
    d2f.disk_args = diskreaderargs(&rte);
    d2f.disk_args.set_start( start );
    d2f.disk_args.set_end( end );
    d2f.disk_args.set_variable_block_size( true );
    d2f.disk_args.set_run( true );

    // Compute transfer block sizes based on settings in net_protocol and a
    // format less transfer
    rte.sizes = constrain(rte.netparms, headersearch_type(), rte.solution);

    // Almost there!
    chain  c;
    d2f.disk_stepid = c.add(diskreader, 10, d2f.disk_args);
    d2f.file_stepid = c.add(&fdwriter<block>, &open_file, d2f.file_name + "," + d2f.open_mode, &rte); 

    c.register_cancel(d2f.file_stepid, &close_filedescriptor);

    // And register the cleanup function. Gets a pointer to the data such
    // that it can close the file
    c.register_final(&disk2fileguard_fun, &rte, ptr);

    XLRCODE(SSHANDLE ss( rte.xlrdev.sshandle() ));
    XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRClearChannels(ss) );
    XLRCALL( ::XLRBindOutputChannel(ss, 0) );
    XLRCALL( ::XLRSelectChannel(ss, 0) );

    // reset statistics counters
    rte.statistics.clear();

    // install and run the chain
    rte.processingchain = c;

    rte.processingchain.run();

    rte.processingchain.communicate(d2f.file_stepid, &fdreaderargs::set_bytes_to_cache, bytes_to_cache);

    rte.transfersubmode.clr_all().set( run_flag );
    rte.transfermode = disk2file;
    
    reply << " 1 ;";

    return reply.str();
    
}
