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
#include <iostream>

#include <sys/stat.h>

using namespace std;


// The guard function which finalizes the "file2disk" transfer
void file2diskguard_fun(runtime* rteptr, ScanPointer scan) {
    try {
        DEBUG(3, "file2disk guard function: transfer done" << endl);
        // apparently we need to call stop twice
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        
        // store the results in the user directory
        rteptr->xlrdev.finishScan( scan );
        
        if ( rteptr->disk_state_mask & runtime::record_flag )
            rteptr->xlrdev.write_state( "Recorded" );

        RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "file2disk execution threw an exception: " << e.what() << std::endl );
        rteptr->transfermode = no_transfer;
        rteptr->xlrdev.stopRecordingFailure();
    }
    catch ( ... ) {
        DEBUG(-1, "file2disk execution threw an unknown exception" << std::endl );        
        rteptr->transfermode = no_transfer;
        rteptr->xlrdev.stopRecordingFailure();
    }
}

string file2disk_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream reply;
    static per_runtime<chain::stepid> file_stepid;
    static per_runtime<fdreaderargs>  file_args;
    static per_runtime<string>        file_name;
    static per_runtime<ScanPointer>   scan_pointer;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    reply << "!" << args[0] << ((qry)?('?'):('='));

    // Query is always possible, command only when not doing anything
    // because file2disk has no "states"
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer));

    if ( qry ) {
        if ( (ctm == file2disk) && (rte.transfersubmode & run_flag) ) {
            ASSERT_COND( file_stepid.find(&rte) != file_stepid.end() && 
                         file_args.find(&rte) != file_args.end() &&
                         file_name.find(&rte) != file_name.end() &&
                         scan_pointer.find(&rte) != scan_pointer.end()
                         );

            reply << " 0 : active : " << file_name[&rte] << " : " << file_args[&rte].start << " : " 
                  << (rte.statistics.counter(file_stepid[&rte]) + file_args[&rte].start) << " : "
                  << file_args[&rte].end << " : " << (scan_pointer[&rte].index() + 1) << " : "
                  << ROScanPointer::strip_asterisk( scan_pointer[&rte].name() ) << " ;";
        }
        else {
            reply << " 0 : inactive ;";
        }
    }
    else {
        // Ok it was a command

        if ( args.size() > 1 ) {
            file_name[&rte] = args[1];
        }
        else {
            if ( file_name.find(&rte) == file_name.end() ) {
                file_name[&rte] = "save.data";
            }
        }

        char*        eocptr;
        off_t        start = 0, end = 0;
        string       scan_label( OPTARG(4, args) );
        const string start_s( OPTARG(2, args) );
        const string end_s( OPTARG(3, args) );

        // end defaults to end-of-file unless overwritten by usr
        {
            struct stat   f_stat;

            ASSERT2_ZERO( ::stat(file_name[&rte].c_str(), &f_stat), SCINFO(" - " << file_name[&rte]));
            EZASSERT2((f_stat.st_mode&S_IFREG)==S_IFREG, cmdexception, EZINFO(file_name[&rte] << " not a regular file"));
            end = f_stat.st_size;
        }

        if( !start_s.empty() ) {
            errno = 0;
            start = ::strtoull(start_s.c_str(), &eocptr, 0);
            ASSERT2_COND( start >= 0 && !(start==0 && eocptr==start_s.c_str()) && !((uint64_t)start==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'start' value") );
        }
        if( !end_s.empty() ) {
            errno = 0;
            end   = ::strtoull(end_s.c_str(), &eocptr, 0);
            ASSERT2_COND( end >= 0 && !(end==0 && eocptr==end_s.c_str()) && !((uint64_t)end==~((uint64_t)0) && errno==ERANGE),
                          SCINFO("Failed to parse 'end' value") );
        }
        
        // default to filename sans suffix
        if( scan_label.empty() )
            scan_label = file_name[&rte].substr(file_name[&rte].rfind('.'));

        rte.sizes = constrain(rte.netparms, headersearch_type(), rte.solution);

        auto_ptr<fdreaderargs> fdreaderargs_pointer( open_file(file_name[&rte] + ",r", &rte) );
        file_args[&rte] = fdreaderargs(*fdreaderargs_pointer);

        file_args[&rte].start = start;
        file_args[&rte].end = end;
        
        chain c;
        c.register_cancel( file_stepid[&rte] = c.add(&fdreader, 32, file_args[&rte]),
                           &close_filedescriptor);
        c.add(fifowriter, &rte);

        XLRCODE(SSHANDLE ss( rte.xlrdev.sshandle() ));
        XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
        XLRCALL( ::XLRClearChannels(ss) );
        XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

        scan_pointer[&rte] = rte.xlrdev.startScan( scan_label );

        // Register the cleanup function
        c.register_final(&file2diskguard_fun, &rte, scan_pointer[&rte]);

        // and start the recording
        XLRCALL( ::XLRAppend(ss) );

        rte.transfersubmode.clr_all();
        // reset statistics counters
        rte.statistics.clear();

        // install and run the chain
        rte.processingchain = c;

        rte.processingchain.run();

        rte.processingchain.communicate(0, &fdreaderargs::set_variable_block_size, true);
        rte.processingchain.communicate(0, &fdreaderargs::set_run, true);
        
        rte.transfermode = file2disk;
        rte.transfersubmode.clr_all().set( run_flag );

        reply << " 1 ;";
    }

    return reply.str();
    
}
