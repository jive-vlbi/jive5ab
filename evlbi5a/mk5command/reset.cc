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

using namespace std;


// Note: the conditioning guard cannot be changed into a "final" function
// simply because there is no processing chain executing .... drat!
struct conditionguardargs_type {
    runtime*    rteptr;

    conditionguardargs_type( runtime& r ) : 
        rteptr(&r)
    {}
    
private:
    conditionguardargs_type();
};

void* conditionguard_fun(void* args) {
    // takes ownership of args
    conditionguardargs_type* guard_args = (conditionguardargs_type*)args;
    try {
        S_DEVSTATUS status;
        do {
            sleep( 3 ); // copying SSErase behaviour here
            XLRCALL( ::XLRGetDeviceStatus(guard_args->rteptr->xlrdev.sshandle(),
                                          &status) );
        } while ( status.Recording );
        DEBUG(3, "condition guard function: transfer done" << endl);
        
        if ( guard_args->rteptr->disk_state_mask & runtime::erase_flag ) {
            guard_args->rteptr->xlrdev.write_state( "Erased" );
        }

        RTEEXEC( *guard_args->rteptr, guard_args->rteptr->transfermode = no_transfer);

    }
    catch ( const std::exception& e) {
        DEBUG(-1, "conditioning execution threw an exception: " << e.what() << std::endl );
        guard_args->rteptr->transfermode = no_transfer;
    }
    catch ( ... ) {
        DEBUG(-1, "conditioning execution threw an unknown exception" << std::endl );        
        guard_args->rteptr->transfermode = no_transfer;
    }

    delete guard_args;
    return NULL;
}

string reset_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream          reply;

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
            try {
                // has to be called twice (according to docs)
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop streamstor: " << e.what() << " ;";
                rte.transfermode = no_transfer;
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop streamstor, unknown exception ;";
                rte.transfermode = no_transfer;
            }
            reply << "1 ;";
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
            layout = args[2];
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
                layout = mark5 + "16Disks" + sdk;
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
        SS_OWMODE    owm = SS_OVERWRITE_PARTITION;
        const string erasemode_s( ::tolower(OPTARG(3, args)) );

#if WDAPIVER>1031
        // Set other default on 5C/non-bank mode
        if( rte.ioboard.hardware() & ioboard_type::mk5c_flag &&
            rte.xlrdev.bankMode() == SS_BANKMODE_DISABLED )
                owm = SS_OVERWRITE_BIGBLOCK;
#endif
        // Check override
        if( !erasemode_s.empty() ) {
#if WDAPIVER>1031
            // Better be something recognized
            EZASSERT2(erasemode_s=="legacy" || erasemode_s=="bigblock", 
                      Error_Code_6_Exception, EZINFO("Unrecognized erase mode"));

            if( erasemode_s=="legacy" )
                owm = SS_OVERWRITE_PARTITION;
            else
                owm = SS_OVERWRITE_BIGBLOCK;
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
        rte.xlrdev.start_condition();
        rte.transfermode = condition;
        // create a thread to automatically stop the transfer when done
        pthread_t thread_id;
        pthread_attr_t tattr;
        PTHREAD_CALL( ::pthread_attr_init(&tattr) );
        PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) );
        conditionguardargs_type* guard_args = new conditionguardargs_type( rte );
        PTHREAD2_CALL( ::pthread_create( &thread_id, &tattr, conditionguard_fun, guard_args ),
                       delete guard_args );
    }
    else {
        reply << "2 : unrecognized control argument '" << args[1] << "' ;";
        return reply.str();
    }
    reply << "0 ;";
    return reply.str();
}
