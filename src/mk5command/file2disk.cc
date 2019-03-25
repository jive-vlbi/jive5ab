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
#include <scan_label.h>
#include <countedpointer.h>

#include <iostream>

#include <sys/stat.h>   // for ::fstat()
#include <libgen.h>     // for ::basename(3)

using namespace std;

struct f2d_data_type {
    string          open_mode;
    string          file_name;
    ScanPointer     scan_pointer;
    cfdreaderargs   file_args;
    chain::stepid   file_stepid;

    f2d_data_type():
        file_stepid( chain::invalid_stepid )
    {}

    ~f2d_data_type() { }

    private:
        f2d_data_type(f2d_data_type const&);
        f2d_data_type const& operator=(f2d_data_type const&);
};

typedef countedpointer<f2d_data_type> f2d_ptr_type;
typedef per_runtime<f2d_ptr_type>     f2d_map_type;

// The guard function which finalizes the "file2disk" transfer
void file2diskguard_fun(f2d_map_type::iterator p) {
    f2d_ptr_type f2d_ptr = p->second;
    runtime*     rteptr = f2d_ptr->file_args->rteptr;
    try {
        DEBUG(3, "file2disk guard function: transfer done" << endl);
        // apparently we need to call stop twice
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        
        // store the results in the user directory
        rteptr->xlrdev.finishScan( f2d_ptr->scan_pointer );
        
        if ( rteptr->disk_state_mask & runtime::record_flag )
            rteptr->xlrdev.write_state( "Recorded" );

        // Close the file handle
        if( f2d_ptr->file_stepid!=chain::invalid_stepid )
            rteptr->processingchain.communicate(f2d_ptr->file_stepid, &close_filedescriptor_c);

        // Don't need the step ids any more
        f2d_ptr->file_stepid = chain::invalid_stepid;
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "file2disk guard function/caught exception: " << e.what() << std::endl );
        rteptr->xlrdev.stopRecordingFailure();
    }
    catch ( ... ) {
        DEBUG(-1, "file2disk guard function/caught unknown exception" << std::endl );        
        rteptr->xlrdev.stopRecordingFailure();
    }
    // No matter how we exited, this has to be done
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
}

string file2disk_fn(bool qry, const vector<string>& args, runtime& rte ) {
    static f2d_map_type  f2d_map;
    ostringstream        reply;
    const transfer_type  ctm( rte.transfermode ); // current transfer mode

    reply << "!" << args[0] << ((qry)?('?'):('='));

    // Query is always possible, command only when not doing anything
    // because file2disk has no "states"
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer));

    if ( qry ) {
        // Attempt to locate data for this runtime
        f2d_map_type::const_iterator   ptr = f2d_map.find(&rte);

        if ( (ctm == file2disk) && (rte.transfersubmode & run_flag) ) {
            // BEFORE USING 'ptr' we MUST assert that 'ptr!=d2f_data.end()'!
            EZASSERT( ptr!=f2d_map.end(), cmdexception );
            const f2d_ptr_type  f2d_ptr( ptr->second );

            reply << " 0 : active : " << f2d_ptr->file_name << " : " << f2d_ptr->file_args->start << " : " 
                  << (rte.statistics.counter(f2d_ptr->file_stepid) + f2d_ptr->file_args->start) << " : "
                  << f2d_ptr->file_args->end << " : " << (f2d_ptr->scan_pointer.index() + 1) << " : "
                  << ROScanPointer::strip_asterisk( f2d_ptr->scan_pointer.name() ) << " ;";
        }
        else {
            reply << " 0 : inactive ;";
        }
        return reply.str();
    }

    // Ok it was a command
    // The following statement makes sure an entry will exist in d2f_data
    // and then we can take a reference to the mapped value for ez access
    f2d_map_type::iterator f2dm_ptr = f2d_map.find( &rte );
    f2d_ptr_type           f2d( new f2d_data_type() );

    // According to the docs, the default for file name is
    // "save.data" or last used, if any

    // copy previous, if exists and also replace with the new f2d_data
    // or insert new one if necessary
    if( f2dm_ptr!=f2d_map.end() ) {
        f2d->file_name   = f2dm_ptr->second->file_name;
        f2dm_ptr->second = f2d;
    } else {
        pair<f2d_map_type::iterator, bool> insres = f2d_map.insert( make_pair(&rte, f2d) );
        EZASSERT2(insres.second, cmdexception, EZINFO("Failed to insert file2disk metadata entry into map?!"));
        f2dm_ptr = insres.first;
    }

    // overwrite with default if empty
    if( f2d->file_name.empty() )
        f2d->file_name = "save.data";

    // Did user specify a file name?
    const string  fn( OPTARG(1, args) );

    if( !fn.empty() )
        f2d->file_name = fn;

    // Parse data range
    char*         eocptr;
    off_t         start = 0, end = 0;
    string        scan_label( OPTARG(4, args) );
    const string  start_s( OPTARG(2, args) );
    const string  end_s( OPTARG(3, args) );

    // end defaults to end-of-file unless overwritten by usr
    {
        struct stat   f_stat;

        ASSERT2_ZERO( ::stat(f2d->file_name.c_str(), &f_stat), SCINFO(" - " << f2d->file_name));
        EZASSERT2((f_stat.st_mode&S_IFREG)==S_IFREG, cmdexception, EZINFO(f2d->file_name << " not a regular file"));
        end = f_stat.st_size;
    }

    // check if user overrode start and/or end value
    if( !start_s.empty() ) {
        errno = 0;
        start = ::strtoull(start_s.c_str(), &eocptr, 0);
        EZASSERT2( start >= 0 && !(start==0 && eocptr==start_s.c_str()) && !((uint64_t)start==~((uint64_t)0) && errno==ERANGE),
                   cmdexception, EZINFO("Failed to parse 'start' value") );
    }
    if( !end_s.empty() ) {
        errno = 0;
        end   = ::strtoull(end_s.c_str(), &eocptr, 0);
        EZASSERT2( end >= 0 && !(end==0 && eocptr==end_s.c_str()) && !((uint64_t)end==~((uint64_t)0) && errno==ERANGE),
                   cmdexception, EZINFO("Failed to parse 'end' value") );
    }
        
    // Scan label defaults to filename sans suffix.
    if( scan_label.empty() ) {
        // First do a "basename()" on the file name - we drop the path info
        char  *fncp, *fnbp;
       
        EZASSERT2( (fncp=::strdup( f2d->file_name.c_str()))!=0, cmdexception, EZINFO("Failed to duplicate file name"));
        ASSERT2_COND( (fnbp=::basename(fncp))!=0, SCINFO(" ::basename(3) failed on " << f2d->file_name); ::free(fncp));
       
        const string filename( fnbp ); 
        // Ok, we copied the basename of the file. Now we don't need the
        // "char*" copy any more. Delete it before we can throw more
        // exception.
        ::free( fncp );
        // Strip the file name extension
        scan_label = filename.substr(0, filename.rfind('.'));

        // And finally make sure we *have* a scan label
        EZASSERT2( scan_label.size()>0, cmdexception,
                   EZINFO("No scan name remains after stripping extension from filename " << f2d->file_name) );
        scan_label = scan_label::create_scan_label(scan_label::file_name, scan_label);
    }
    else {
        scan_label = scan_label::create_scan_label(scan_label::command, scan_label);
    }


    // Looks like we have everything in place
    // Constrain sizes based on formatless transfer 
    rte.sizes = constrain(rte.netparms, headersearch_type(), rte.solution);

    f2d->file_args        = open_file(f2d->file_name + ",r", &rte);
    f2d->file_args->start = start;
    f2d->file_args->end   = end;
    f2d->file_args->run   = true;
    f2d->file_args->allow_variable_block_size = true;
    
    chain c;
    c.register_cancel( f2d->file_stepid = c.add(&fdreader_c, 32, f2d->file_args),
                       &close_filedescriptor_c);
    c.add(fifowriter, &rte);

    // Set up streamstor for recording
    XLRCODE(SSHANDLE ss( rte.xlrdev.sshandle() ));
    XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRClearChannels(ss) );
    XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

    f2d->scan_pointer = rte.xlrdev.startScan( scan_label );

    // Register the cleanup function
    c.register_final(&file2diskguard_fun, f2dm_ptr);

    // and start the recording
    XLRCALL( ::XLRAppend(ss) );

    rte.transfersubmode.clr_all();
    // reset statistics counters
    rte.statistics.clear();

    // install and run the chain
    rte.processingchain = c;

    rte.processingchain.run();

    rte.transfermode = file2disk;
    rte.transfersubmode.clr_all().set( run_flag );

    reply << " 1 ;";
    return reply.str();
}
