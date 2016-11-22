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
#include <version.h>
#include <iostream>
#include <chain.h>

using namespace std;


typedef per_runtime<string> error_cache_type;


// Note: the conditioning guard cannot be changed into a "final" function
// simply because there is no processing chain executing .... drat!
// BE/HV: 26-Jun-2014 But this is about to change!
//                    We'll make a fake processing chain with
//                    a proper cleanup function. This "chain"
//                    can be given to the runtime as "yes, this is
//                    what we're doing". The practical upshot is
//                    that upon "^C" the chain gets properly
//                    destroyed and the cleanup function gets run.
//                    Win all over!
void conditionStart(outq_type<bool>* oqptr, sync_type<runtime*>* args) {
    bool             cancelled     = false;
    XLRCODE(runtime* rteptr = *args->userdata);
    S_DEVSTATUS      status;

    DEBUG(0, "conditioning: start" << endl);


    // Wait until we're cancelled or conditioning finishes
    while( !cancelled ) {
        ::sleep( 1 ); // copying SSErase behaviour here
                      // HV: well, 3 seconds is a bit long for 
                      //     "^C" response time
        XLRCALL( ::XLRGetDeviceStatus(rteptr->xlrdev.sshandle(), &status) );
        if( !status.Recording )
            break;

        args->lock();
        cancelled = args->cancelled;
        args->unlock();

        if( cancelled )
            break;
    }
    oqptr->push( false );
}

struct end_args_type {
    runtime*                   rteptr;
    error_cache_type::iterator errMsgPtr;

    end_args_type(runtime* rp, error_cache_type::iterator emp):
        rteptr( rp ), errMsgPtr( emp )
    { EZASSERT2(rteptr, cmdexception, EZINFO("Cannot have null runtime pointer")); }

    private:
        // prevent default construction
        end_args_type();
};

void conditionEnd(inq_type<bool>* iqptr, sync_type<end_args_type>* args) {
    // Phase one: wait for termination
    bool                       dummy;
    runtime*                   rteptr = args->userdata->rteptr;
    error_cache_type::iterator errMsgPtr = args->userdata->errMsgPtr;

    iqptr->pop(dummy);

    // Do stop the device
    DEBUG(0, "conditioning: stopping" << endl);

    // Always do the Stop twice. We still don't know
    // exactly *why* but it doesn't hurt and mostly works
    try {
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );

        if( rteptr->disk_state_mask & runtime::erase_flag )
            rteptr->xlrdev.write_state( "Erased" );
    }
    catch ( const std::exception& e) {
        errMsgPtr->second += (string("stop condition fails ") + e.what());
        DEBUG(-1, "conditionEnd/caught an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        errMsgPtr->second += string("stop condition fails of unknown exception");
        DEBUG(-1, "conditionEnd/caught an unknown exception" << endl);
    }
}

// By the time the guard function is run, no more threads are active
// and it's safe to put back the transfer mode to nothing
void conditionGuard(runtime* rteptr) {
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer);
}




////////////////////////////////////////////////////////////////////
//
//  The actual reset command implementation
//
////////////////////////////////////////////////////////////////////
string reset_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream           reply;
    static error_cache_type errMsg;

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    if ( q ) {
        reply << "2 : only implemented as command ;";
        return reply.str();
    }

    // Assert that there *is* at least an argument!
    EZASSERT2( OPTARG(1, args).empty()==false, Error_Code_6_Exception,
               EZINFO(args[0] << " needs at least one argument") );

    // First handle the "reset=abort"
    if ( args[1] == "abort" ) {
        // in case of error, set transfer to no transfer
        // the idea is: it is better to be in an unknown state that might work
        // than in a state that is known but useless
        if ( rte.transfermode == disk2net ||
             rte.transfermode == disk2file || 
             rte.transfermode == file2disk 
             ) {
            try {
                rte.processingchain.stop();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
                rte.transfermode = no_transfer;
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
                rte.transfermode = no_transfer;
            }
        }
        else if ( rte.transfermode == condition ) {
            rte.processingchain.stop();
            rte.processingchain = chain();
            // Check if any error messages were reported
            // Note: used to reply '1' (initiated but not completed)
            //       but with the new chain-based approach we are
            //       sure that when we get out of the "stop()"
            //       method, the conditioning has finished so
            //       a '0' should be appropriate, if no errors
            //       reported.
            if( errMsg[&rte].empty()==false )
                reply << "4 : " << errMsg[&rte] << " ;";
            else
                reply << "0 ;";
        }
        else {
            reply << "6 : nothing running to abort ;";
        }
        return reply.str();
    }

    // If we want to progress past this point nothing should be running
    INPROGRESS(rte, reply, rte.transfermode!=no_transfer);

    if ( rte.protected_count == 0 ) {
        reply << "6 : need an immediate preceding protect=off ;";
        return reply.str();
    }


    // HV: 28-Feb-2014  Chet changed the parameter of "reset=erase"
    //                  and the defaults, for Mark5A, DIMino.
    //                  jive5ab will follow suit.
    //                  Default is now Mark5B16DisksSDKn
    //                  (thus 64k scans by default). Use
    //                  "reset = erase : s;" to force old, 1k scan,
    //                  user directories
    // reset = erase [ : [ <desired layout> | "s" ] [ : <erase mode> ]
    //  <desired layout> = force a specific user directory layout
    //                     e.g. Mark5A16DisksSDK9 or Mark5C
    //                     see "userdir.mpl"
    //  <erase mode>     = "legacy" | "bigblock"
    //                     (see below)
    //
    if ( args[1] == "erase" ) {
        string       layout;
        const string arg2( OPTARG(2, args) );

        // set the requested layout unless it was "s"
        if ( !(arg2.empty() || arg2=="s") ) {
            layout = arg2;
        }
        else {
            // set the default layout, depends on Mark5 type and SDK
#if WDAPIVER>999
            std::string sdk = "SDK9";
#else
            std::string sdk = "SDK8";
#endif
            std::string mark5 = (arg2=="s" ? "Mark5A" : "Mark5B");
            
            if ( rte.ioboard.hardware() & ioboard_type::mk5c_flag ) {
                layout = "Mark5CLayout";
            }
            else {
                // always use 16 disks and no BankB as default
                // HV: 9/Nov/2016 Well, let's be a bit more smart about the
                //                defaults:
                //                - look at bank/non bank mode for #-of-disks
                //                - in non-bank mode add 'BankB'
                const bool inBankMode(rte.xlrdev.bankMode()==SS_BANKMODE_NORMAL);
                layout = mark5 + (inBankMode ? "8Disks" : "16Disks") + sdk + (inBankMode ? "" : "BankB");
            }
        }

        // For SDK versions > 1031 [>libwdapi1031.so] we can use
        // "big block" mode 
        //
        // Decide on which erase mode to use
        // 1) "Legacy" VLBI = SS_OVERWRITE_PARTITION [cf. Chet Ruszczyk]
        //    implies "small block size" (64kB). Compatible with pre
        //    SDK9.3.2 firmware
        // 2) SS_OVERWRITE_BIGBLOCK, or "large block mode", 
        //    formatted in 256kB blocks. This is 
        //    needed to be able to do 4Gbps on a Mark5C
        //
        // Our default will follow the following logic:
        // * start with Legacy mode
        // * if running on M5C + non-bank mode configured => user must
        //   be wanting to do 4Gbps => default to large block mode
        //
        // Then, finally, support overriding by usr
        SS_OWMODE         owm = SS_OVERWRITE_PARTITION;
        const string      erasemode_s( ::tolower(OPTARG(3, args)) );

#if WDAPIVER>1110
        // Set other default on 5C/non-bank mode
        // HV: 01-Apr-2014 Add constraint that firmware on the 
        //                 board must be >= 16.39
        const string         fwstring( rte.xlrdev.swRev().FirmwareVersion );
        string::size_type    dot = fwstring.find('.');
        const swversion_type firmware_version( ((dot!=string::npos)?fwstring.substr(0, dot):string()).c_str(),
                                               ((dot!=string::npos)?fwstring.substr(dot+1):string()).c_str() );
        const swversion_type support_largeblock(16, 39);

        if( (rte.ioboard.hardware() & ioboard_type::mk5c_flag) &&
            (rte.xlrdev.bankMode() == SS_BANKMODE_DISABLED) &&
            (firmware_version>=support_largeblock) )
                owm = SS_OVERWRITE_LARGE_BLOCK;
#endif
        // Check override
        if( !erasemode_s.empty() ) {
#if WDAPIVER>1110
            // Better be something recognized
            EZASSERT2(erasemode_s=="legacy" || erasemode_s=="bigblock", 
                      Error_Code_6_Exception, EZINFO("Unrecognized erase mode"));

            // HV: 01-Apr-2014 As per Conduant's suggestion, _ONLY_ allow bigblock 
            //                 erases IF we're in non-bank mode AND the
            //                 firmware is sufficiently new such that it
            //                 supports erasing in large block mode
            EZASSERT2(erasemode_s!="bigblock" || (rte.xlrdev.bankMode()==SS_BANKMODE_DISABLED && firmware_version>=support_largeblock),
                      Error_Code_6_Exception,
                      EZINFO("erasing in large block mode only possible when system in non-bank mode and firmware supports it, "
                             << "need:" << support_largeblock << " or newer, current:" << firmware_version));

            if( erasemode_s=="legacy" )
                owm = SS_OVERWRITE_PARTITION;
            else
                owm = SS_OVERWRITE_LARGE_BLOCK;
#else
            EZASSERT2(erasemode_s=="legacy", Error_Code_6_Exception,
                      EZINFO("This SDK (" << version_constant("SSAPIROOT") << ") does not support erase mode " << erasemode_s));
#endif
        }

        rte.xlrdev.erase( layout, owm );
        rte.pp_current = 0;
        if( rte.disk_state_mask & runtime::erase_flag )
            rte.xlrdev.write_state( "Erased" );
    }
    else if ( args[1] == "erase_last_scan" ) {
        rte.xlrdev.erase_last_scan();
    }
    else if ( args[1] == "condition" ) {
        // Creata a chain to do the conditioning for us
        chain         c;

        // Make sure the error cache for this one (1) exists
        // and (2) is empty
        errMsg[&rte] = string();

        c.add(&conditionStart, 1, &rte);
        c.add(&conditionEnd, end_args_type(&rte, errMsg.find(&rte)));
        c.register_final(&conditionGuard, &rte);

        rte.xlrdev.start_condition();
        rte.transfersubmode.clr_all();
        rte.processingchain = c;
        rte.processingchain.run();

        // HV: Already set transfermode to 'condition'. There was a race
        //     between 'reset=erase' returning 0 and the actual thread
        //     starting; the system ends up in an inconsistent state.
        rte.transfermode = condition;

        // realistically we should return '1' but let's not change that w/o
        // consulting ...
        reply << "1 ;";
        return reply.str();
    }
    else {
        reply << "2 : unrecognized control argument '" << args[1] << "' ;";
        return reply.str();
    }
    reply << "0 ;";
    return reply.str();
}
