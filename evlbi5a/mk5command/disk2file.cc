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
#include <sfxc_binary_command.h>
#include <threadfns.h>
#include <tthreadfns.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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

fdreaderargs* mk_fdreaderargs(int sfxcfd, runtime* rteptr) {
    fdreaderargs*  fdr = new fdreaderargs();

    fdr->fd                        = sfxcfd;
    fdr->run                       = true;
    fdr->rteptr                    = rteptr;
    fdr->netparms                  = rteptr->netparms;
    fdr->allow_variable_block_size = true;

    return fdr;
}

void stream2sfxc_guard_fun(d2f_data_type* d2f) {
    DEBUG(3, "stream2sfxc guard function: transfer done" << endl);
    runtime*  rteptr = d2f->disk_args.rteptr;
    try {
        // Do the streamstor stopping inside try/catch because we MUST
        // be able to close the file descriptor too 
        try {
            // apparently we need to call stop twice
            XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
            XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        }
        catch( const std::exception& e ) {
            DEBUG(-1, "stream2sfxc guard function: failed to stop streamstor - " << e.what() << endl);
        }
        catch( ... ) {
            DEBUG(-1, "stream2sfxc guard function: failed to stop streamstor - unknown exception" << endl);
        }

        // Change disk state mask, if that's enabled
        if( rteptr->disk_state_mask & runtime::play_flag )
            rteptr->xlrdev.write_state( "Played" );

        // See, this is where we close the file
        rteptr->processingchain.communicate(d2f->file_stepid, &close_filedescriptor);

        // Don't need the objects no more
        delete d2f;
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "stream2sfxc guard caught an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "stream2sfxc guard caught an unknown exception" << std::endl );        
    }
    // No matter how we exited, this has to be done
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
}

void switch_back_to_bank(SSHANDLE sshandle, unsigned int bnk) {
    DEBUG(2, "switch_back_to_bank/switching to bank " << bnk << endl);
    try {
        XLRCALL( ::XLRSelectBank(sshandle, E_BANK(bnk)) );
    }
    catch( std::exception const& e ) {
        DEBUG(-1, "switch_back_to_bank caught an exception: " << e.what() << std::endl );
    }
    catch( ... ) {
        DEBUG(-1, "switch_back_to_bank caught an unknown exception" << std::endl );        
    }
}

//////////////////////////////////////////////////////////////
//
//       Mark5 command/query "disk2file[=?]"
//
//////////////////////////////////////////////////////////////
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
            ::snprintf(tmp, sizeof(tmp), "%08x", curipm.bitstreammask);
            suffix = string("_bm=0x") + string(tmp) + ".m5b";
        }
        d2f.file_name = current_scan.name() + suffix;
    }

    // Parse data range - default to current set range
    char*  eocptr;
    off_t  start  = rte.pp_current.Addr;
    off_t  end    = rte.pp_end.Addr;
    off_t  offset = 0;

    if ( (args.size() > 2) && !args[2].empty() ) {
        if( args[2][0]=='-' || args[2][0]=='+' ) {
            reply << " 8 : start byte only absolute byte numbers allowed ;";
            return reply.str();
        }
        errno = 0;
        start = ::strtoll(args[2].c_str(), &eocptr, 0);
        ASSERT2_COND( *eocptr=='\0' && eocptr!=args[2].c_str() && errno==0 && start>=0,
                      SCINFO("Failed to parse 'start' value") );
    }

    if ( (args.size() > 3) && !args[3].empty() ) {
        const bool  plus  = (args[3][0] == '+');
        const char* c_str = args[3].c_str() + ( plus ? 1 : 0 );

        if( args[3][0]=='-' ) {
            reply << " 8 : end byte only absolute byte numbers allowed ;";
            return reply.str();
        }
        errno = 0;
        end   = ::strtoll(c_str, &eocptr, 0);
        ASSERT2_COND( *eocptr=='\0' && eocptr!=c_str && errno==0 && end>=0,
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
        if ( !(args[4] == "n" || args[4] == "w" || args[4] == "a" || args[4] == "resume" ) ) {
            reply << " 8 : open mode must be n, w, a or resume;";
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
        ASSERT2_COND( !(bytes_to_cache==0 && eocptr==args[5].c_str()) && !(bytes_to_cache==~((uint64_t)0) && errno==ERANGE),
                      SCINFO("Failed to parse 'bytes to cache' value") );
    }

    // Now we're good to go
    d2f.disk_args = diskreaderargs(&rte);
    d2f.disk_args.set_start( start+offset );
    d2f.disk_args.set_end( end );
    d2f.disk_args.set_variable_block_size( true );
    d2f.disk_args.set_run( true );

    // Compute transfer block sizes based on settings in net_protocol and a
    // format less transfer
    rte.sizes = constrain(rte.netparms, headersearch_type(), rte.solution);

    throw_on_insane_netprotocol(rte);

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

void attempt_stream_to_sfxc(int sfxcfd, mk5read_msg* mk5read_cmd, runtime& rte ) {
    // If runtime already busy don't allow
    EZASSERT2(rte.transfermode==no_transfer, mk5read_exception, EZINFO("Not whilst doing " << rte.transfermode));

    // Check if the requested VSN is loaded in the system.
    unsigned int     previous_bank = (unsigned int)-1;
    const string     VSN((char const*)mk5read_cmd->vsn, sizeof(mk5read_msg::vsn));
    const S_BANKMODE curbm = rte.xlrdev.bankMode();

    if( curbm==SS_BANKMODE_NORMAL ) {
        // fill both vsn's
        S_BANKSTATUS        bs[2];
        unsigned int        vsn_is_in_bank = (unsigned int)-1;
        unsigned int        selected = (unsigned int)-1;

        XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_A, &bs[0]) );
        XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_B, &bs[1]) );
        for(unsigned int bnk=0; bnk<2; bnk++ ) {
            if( bs[bnk].State==STATE_READY ) {
                pair<string, string> vsn_state = disk_states::split_vsn_state(string(bs[bnk].Label));
                vector<string>       vsn_parts = ::split(vsn_state.first, '/');
                if( ::strncmp(vsn_parts[0].c_str(), mk5read_cmd->vsn, sizeof(mk5read_msg::vsn))==0 )
                    vsn_is_in_bank = bnk;
                if( bs[bnk].Selected ) 
                    selected = bnk;
            }
        }
        // Assert that we *did* find the requested VSN
        EZASSERT2(vsn_is_in_bank!=(unsigned int)-1, mk5read_exception,
                  EZINFO("Requested VSN (" << VSN << ") is not mounted in either bank A nor B"));

        // Issue a bank switch in case the VSN is not in the active bank
        if( selected!=vsn_is_in_bank ) {
            // Issue a bank switch and remember to switch back later
            previous_bank = (vsn_is_in_bank == 0)? BANK_B : BANK_A;
            XLRCALL( ::XLRSelectBank(GETSSHANDLE(rte), (vsn_is_in_bank == 0)? BANK_A : BANK_B) );
        }
    } else if( curbm==SS_BANKMODE_DISABLED ) {
        // non-bank mode: only one VSN
        char label[XLR_LABEL_LENGTH + 1];
        label[XLR_LABEL_LENGTH] = '\0';

        XLRCALL( ::XLRGetLabel( GETSSHANDLE(rte), label) );
        pair<string, string> vsn_state = disk_states::split_vsn_state(string(label));
        
        // It had better be the VSN we're looking for
        EZASSERT2(vsn_state.first==VSN, mk5read_exception,
                  EZINFO("Non-bank mode requested VSN (" << VSN << ") not mounted; found " << vsn_state.first));
    } else {
        THROW_EZEXCEPT(mk5read_exception, "The StreamStor is neither in bank nor non-bank mode.");
    }

    // Compute transfer block sizes based on settings in net_protocol and a
    // format less transfer
    rte.sizes = constrain(rte.netparms, headersearch_type(), rte.solution);

    throw_on_insane_netprotocol(rte);

    // Now we're good to go
    d2f_data_type* d2f = new d2f_data_type();

    d2f->disk_args = diskreaderargs(&rte);
    d2f->disk_args.set_start( mk5read_cmd->off );
    d2f->disk_args.set_end( ::XLRGetLength(GETSSHANDLE(rte)) );
    d2f->disk_args.set_variable_block_size( true );
    d2f->disk_args.set_run( true );


    // Almost there!
    chain  c;
    d2f->disk_stepid = c.add(diskreader, 10, d2f->disk_args);
    d2f->file_stepid = c.add(&fdwriter<block>, mk_fdreaderargs, sfxcfd, &rte);

    c.register_cancel(d2f->file_stepid, &close_filedescriptor);

    // And register the cleanup function. Gets a pointer to the data such
    // that it can close the file
    c.register_final(&stream2sfxc_guard_fun, d2f);

    // And if we need to switch back to the old bank ...
    if( previous_bank!=(unsigned int)-1 )
        c.register_final(&switch_back_to_bank, GETSSHANDLE(rte), previous_bank);

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

    // Now it's safe to set the transfermode
    rte.transfersubmode.clr_all().set( run_flag );
    rte.transfermode = stream2sfxc;

    return;
}
