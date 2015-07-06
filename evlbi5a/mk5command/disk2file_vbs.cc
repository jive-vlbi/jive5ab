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
#include <data_check.h>
#include <libvbs.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

struct d2f_vbs_data_type {
    string          open_mode;
    string          file_name;
    chain::stepid   vbs_stepid;
    chain::stepid   file_stepid;
    fdreaderargs    disk_args;

    d2f_vbs_data_type():
        vbs_stepid( chain::invalid_stepid ),
        file_stepid( chain::invalid_stepid )
    {}
};

typedef per_runtime<d2f_vbs_data_type>  d2f_data_store_type;


void disk2file_vbs_guard_fun(runtime* rteptr, d2f_data_store_type::iterator p) {
    try {
        DEBUG(3, "disk2file(vbs) guard function: transfer done" << endl);
     
        // Close the recording 
        ::vbs_close(p->second.disk_args.fd);     

        // See, this is where we close the file
        rteptr->processingchain.communicate(p->second.file_stepid, &close_filedescriptor);

        // Don't need the step ids any more
        p->second.file_stepid = chain::invalid_stepid;
        p->second.vbs_stepid = chain::invalid_stepid;
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "disk2file(vbs) guard caught an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "disk2file(vbs) guard caught an unknown exception" << std::endl );        
    }
    // No matter how we exited, this has to be done
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
}

string disk2file_vbs_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream              reply;
    const transfer_type        ctm( rte.transfermode ); // current transfer mode
    static d2f_data_store_type d2f_data;

    reply << "!" << args[0] << ((qry)?('?'):('='));

    // Query may execute always, command only if nothing else happening
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer))

    if ( qry ) {
        // Attempt to locate data for this runtime
        d2f_data_store_type::const_iterator   ptr = d2f_data.find(&rte);

        if ( (ctm == disk2file) && (rte.transfersubmode & run_flag) ) {
            // BEFORE USING 'ptr' we MUST assert that 'ptr!=d2f_data.end()'!
            ASSERT_COND( ptr!=d2f_data.end() );
            const d2f_vbs_data_type&  d2f( ptr->second );
           
            // Now it's safe to use 'd2f.*' 
            uint64_t start   = d2f.disk_args.start;
            uint64_t current = rte.statistics.counter(d2f.vbs_stepid) + start;
            uint64_t end     = d2f.disk_args.end;

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
    // Make sure that a scan has been set
    EZASSERT2(rte.mk6info.scanName.empty()==false, cmdexception, 
              EZINFO(" no scan was set using scan_set="));

    // The following statement makes sure an entry will exist in d2f_data
    // and then we can take a reference to the mapped value for ez access
    d2f_data_store_type::iterator ptr = d2f_data.insert( make_pair(&rte, d2f_vbs_data_type()) ).first; 
    d2f_vbs_data_type&            d2f( ptr->second );

    d2f.file_name = OPTARG(1, args);

    // No file name given?
    if( d2f.file_name.empty() )
        d2f.file_name = rte.mk6info.scanName + ".m5a";

    // Parse data range - default to current set range
    char*         eocptr;
    off_t         start  = rte.mk6info.fpStart;
    off_t         end    = rte.mk6info.fpEnd;
    off_t         offset = 0;

    // Can only set absolute byte numbers here
    if ( (args.size() > 2) && !args[2].empty() ) {
        if( args[2][0]=='+' || args[2][0]=='-' ) {
            reply << " 8 : relative byteoffsets not allowed here ;";
            return reply.str();
        }
        errno = 0;
        start = ::strtoll(args[2].c_str(), &eocptr, 0);
        ASSERT2_COND( *eocptr=='\0' && eocptr!=args[2].c_str() && errno==0,
                      SCINFO("Failed to parse 'start' value") );
    }

    if ( (args.size() > 3) && !args[3].empty() ) {
        const bool  plus  = (args[3][0] == '+');
        const char* c_str = args[3].c_str() + ( plus ? 1 : 0 );

        errno = 0;
        end   = ::strtoll(c_str, &eocptr, 0);
        ASSERT2_COND( *eocptr=='\0' && eocptr!=c_str && errno==0 && end>=0,
                      SCINFO("Failed to parse 'end' value") );
        if ( plus ) {
            end += start;
        }
    }
    
    // Default open mode is 'n', unless overriden by user
    d2f.open_mode = "n";
    if ( args.size() > 4 ) {
        if ( !(args[4] == "n" || args[4] == "w" || args[4] == "a" || args[4] == "resume") ) {
            reply << " 8 : open mode must be n, w or a ;";
            return reply.str();
        }
        d2f.open_mode = args[4];

        // resume is only a logical, not a physical open mode
        // we need to get the file size and reset the
        // file open mode to "a", for append
        if( d2f.open_mode=="resume" ) {
            struct stat  fstat;

            // the file _may_ exist, but if something's wrong
            // that's not an error HERE.
            if( ::stat(d2f.file_name.c_str(), &fstat)==0 )
                offset = fstat.st_size;

            d2f.open_mode = "a";
        }
    }

    uint64_t bytes_to_cache = numeric_limits<uint64_t>::max();
    if ( args.size() > 5 ) {
        errno          = 0;
        bytes_to_cache = ::strtoull(args[5].c_str(), &eocptr, 0);
        ASSERT2_COND( *eocptr=='\0' && eocptr!=args[5].c_str() && errno==0,
                      SCINFO("Failed to parse 'bytes to cache' value") );
        if( bytes_to_cache==0 ) {
            reply << " 8 : bytes to cache must be > 0 ;";
            return reply.str();
        }
    }

    // (attempt to) open the recording
    fdreaderargs* vbs = open_vbs(rte.mk6info.scanName, &rte);

    // Now we're good to go
    d2f.disk_args = *vbs;
    d2f.disk_args.set_start( start+offset );
    d2f.disk_args.set_end( end );
    d2f.disk_args.set_variable_block_size( true );
    d2f.disk_args.set_run( true );
    d2f.disk_args.set_bytes_to_cache( bytes_to_cache );

    delete vbs;

    // Compute transfer block sizes based on settings in net_protocol and a
    // format less transfer
    rte.sizes = constrain(rte.netparms, headersearch_type(), rte.solution);

    // Almost there!
    chain  c;
    d2f.vbs_stepid  = c.add(vbsreader, 10, d2f.disk_args);
    d2f.file_stepid = c.add(&fdwriter<block>, &open_file, d2f.file_name + "," + d2f.open_mode, &rte); 

    c.register_cancel(d2f.file_stepid, &close_filedescriptor);

    // And register the cleanup function. Gets a pointer to the data such
    // that it can close the file
    c.register_final(&disk2file_vbs_guard_fun, &rte, ptr);

    // reset statistics counters
    rte.statistics.clear();

    // install and run the chain
    rte.processingchain = c;

    rte.processingchain.run();

    rte.transfersubmode.clr_all().set( run_flag );
    rte.transfermode = disk2file;
    
    reply << " 1 ;";

    return reply.str();
    
}
