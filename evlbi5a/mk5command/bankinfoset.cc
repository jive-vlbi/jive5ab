// bank_set + bank_info
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
#include <iostream>
#include <signal.h>

using namespace std;

// One-shot thread function which does the actual bank switch
struct bswitchargs {
    bswitchargs(runtime* rtep, unsigned int bnkid):
        rteptr( rtep ), bankid( bnkid )
    { EZASSERT2(rteptr, cmdexception, EZINFO("Don't construct thread args with NULL pointer!")); }

    runtime*     rteptr;
    unsigned int bankid;

    private:
        bswitchargs();
};

void* bankswitch_thrd(void* args) {
    bswitchargs*    bankswitch = static_cast<bswitchargs*>(args);

    if( !bankswitch ) {
        DEBUG(-1, "bankswitch_thrd/Do not start thread function with NULL pointer for arguments" << endl);
        // we cannot put back the runtime's state because we don't have a
        // pointer to the runtime!
        return (void*)0;
    }
    DEBUG(3, "bankswitch_thrd/start switch to bank " << bankswitch->bankid << endl);

    // Attempt to do the bank switch
    try {
        XLRCALL( ::XLRSelectBank(bankswitch->rteptr->xlrdev.sshandle(), bankswitch->bankid) );
        // force a check of mount status
        bankswitch->rteptr->xlrdev.update_mount_status();
    }
    catch( const std::exception& e ) {
        DEBUG(-1, "bankswitch_thrd/failed to do bank switch to " << 
                  bankswitch->bankid << " - " << e.what() << endl);
        push_error( error_type(1006, string("Bank switch failed - ")+e.what()) );
    }
    sleep( 5 );
    DEBUG(3, "bankswitch_thrd/clearing runtime's transfer mode to no_transfer" << endl);
    // In the runtime, set the transfer mode back to no_transfer
    RTEEXEC(*bankswitch->rteptr, bankswitch->rteptr->transfermode = no_transfer);

    // Free the storage space - it was allocated using "operator new"
    delete bankswitch;

    return (void*)0;
}


#define BANKID(str) ((str=="A")?((UINT)BANK_A):((str=="B")?((UINT)BANK_B):(UINT)-1))

// Bank handling commands
// Do both "bank_set" and "bank_info" - most of the logic is identical
// Also handle the execution of disk state query (command is handled in its own function)
string bankinfoset_fn( bool qry, const vector<string>& args, runtime& rte) {
    const unsigned int  inactive = (unsigned int)-1;
    const string        bl[] = {"A", "B"};
    S_BANKSTATUS        bs[2];
    unsigned int        selected = inactive;
    unsigned int        nactive = 0;
    transfer_type       ctm = rte.transfermode;
    ostringstream       reply;
    const S_BANKMODE    curbm = rte.xlrdev.bankMode();


    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('='));

    // This one only supports "bank_info?" and "bank_set[?=]"
    EZASSERT2( args[0]=="bank_info" || args[0]=="bank_set", Error_Code_6_Exception,
               EZINFO(args[0] << " not supported by this function") );

    // bank_info is only available as query
    if( args[0]=="bank_info" && !qry ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // Verify that we are eligible to execute in the first place
    INPROGRESS( rte, reply,
                // no bank_set command whilst doing *anything* with the disks
                (args[0]=="bank_set" && !qry && (todisk(ctm) || fromdisk(ctm))) ||
                // no query (neither bank_info?/bank_set?) whilst doing bankswitch or condition
                (qry && (ctm==bankswitch || ctm==condition)) );
#if 0
    // Can't do bank_set if we're doing anything with the disks
    if( args[0]=="bank_set" && ctm!=no_transfer && !qry ) {
        reply << " 6 : cannot change banks whilst doing " << ctm << " ;";
        return reply.str();
    }

    if ( rte.transfermode == condition ) {
        reply << " 6 : not possible during " << rte.transfermode << " ;";
        return reply.str();
    }
#endif 
    // If we're not in bankmode none of these can return anything
    // sensible, isn't it?
    if( curbm!=SS_BANKMODE_NORMAL && curbm!=SS_BANKMODE_AUTO_ON_FULL ) {
        reply << " 6 : not in bank mode ; ";
        return reply.str();
    }

    // Ok. Inspect the banksz0rz!
    XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_A, &bs[0]) );
    XLRCALL( ::XLRGetBankStatus(GETSSHANDLE(rte), BANK_B, &bs[1]) );
    for(unsigned int bnk=0; bnk<2; bnk++ ) {
        if( bs[bnk].State==STATE_READY ) {
            nactive++;
            if( bs[bnk].Selected ) 
                selected = bnk;
        }
    }
   
    // If we're doing bank_set as a command ...
    // For "bank_set=inc" there's three cases:
    //    0 active banks => return error 6
    //    1 active bank  => return 0 [cyclic rotation to self]
    //    2 active banks => return 1, fire background thread
    if( args[0]=="bank_set" && !qry ) {
        int          code     = 0;
        string       bank_str = ::toupper(OPTARG(1, args));
        const string curbank_str = (selected!=inactive?bl[selected]:"");

        // Not saying which bank is a parameter error (code "8")
        EZASSERT2( bank_str.empty()==false, Error_Code_8_Exception,
                   EZINFO("You must specify which bank to set active" ));
        // can't do inc if the selected bank is inactive. This will be code 6:
        // conflicting request. Note: this also covers the case where
        // 0 banks are active (for bank_set=inc)
        EZASSERT2( (bank_str!="INC") || (bank_str=="INC" && nactive>0 && selected!=inactive),
                   Error_Code_6_Exception, EZINFO("No bank selected so can't toggle using 'inc'") );

        // we've already verified that there *is* a bank selected
        // [if "inc" is requested]
        if( bank_str=="INC" ) {
            if( nactive>1 )
                bank_str = bl[ 1 - selected ];
            else
                bank_str = bl[ selected ];
        }

        ASSERT2_COND( bank_str=="A" || bank_str=="B",
                      SCINFO("invalid bank requested") );

        // If the bank to switch to is not the selected one, we
        // fire up a background thread to do the switch for us
        if( bank_str!=curbank_str ) {
            sigset_t        oss, nss;
            pthread_t       bswitchid;
            pthread_attr_t  tattr;

            code             = 1;
            rte.transfermode = bankswitch;

            // set up for a detached thread with ALL signals blocked
            ASSERT_ZERO( sigfillset(&nss) );
            PTHREAD_CALL( ::pthread_attr_init(&tattr) );
            PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) );
            PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &nss, &oss) );
            PTHREAD_CALL( ::pthread_create(&bswitchid, &tattr, bankswitch_thrd, (void*)(new bswitchargs(&rte, BANKID(bank_str)))) );
            // good. put back old sigmask + clean up resources
            PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &oss, 0) );
            PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );
        }
        reply << code << " ;";
        return reply.str();
    }

    // Here we handle the query case for bank_set and bank_info
    reply << " 0 ";
    for(unsigned int i=0; i<2; i++) {
        // output the selected bank first if any
        unsigned int selected_index = i;
        if (selected == 1) {
            selected_index = 1 - i;
        }
        if( bs[selected_index].State!=STATE_READY ) {
            if( args[0]=="bank_info" )
                reply << ": - : 0 ";
            else
                reply << ": - :   ";
        } else {
            const S_BANKSTATUS&  bank( bs[ selected_index ] );
            reply << ": " << bl[ selected_index ] << " : ";
            if( args[0]=="bank_info" ) {
                long page_size = ::sysconf(_SC_PAGESIZE);
                reply << ((bank.TotalCapacity * (uint64_t)page_size) - bank.Length);
            } else {
                pair<string, string> vsn_state = disk_states::split_vsn_state(string(bank.Label));
                if ( args[0] == "bank_set" ) {
                    reply << vsn_state.first;
                }
                else { // disk_state
                    reply << vsn_state.second;
                }
            }
            reply << " ";
        }
    }
    reply << ";";
    return reply.str();
}


// This one uses the bankinfoset_fn, in case of query
string disk_state_fn( bool qry, const vector<string>& args, runtime& rte) {
    if ( qry ) {
        // forward it to bankinfo_fn
        return bankinfoset_fn( qry, args, rte );
    }

    // handle the command
    ostringstream reply;
    reply << "!" << args[0] << ((qry)?('?'):('='));
    
    if ( rte.transfermode != no_transfer ) {
        reply << " 6 : cannot set state while doing " << rte.transfermode << " ;";
        return reply.str();
    }

    if ( args.size() < 2 ) {
        reply << " 8 : command requires an argument ;";
        return reply.str();
    }

    if ( rte.protected_count == 0 ) {
        reply << " 6 : need an immediate preceding protect=off ;";
        return reply.str();
    }
    // ok, we're allowed to write state

    if ( args[1].empty() ) {
        reply << " 8 : argument is empty ;";
        return reply.str();
    }

    string new_state = args[1];
    new_state[0] = ::toupper(new_state[0]);

    if ( disk_states::all_set.find(new_state) == disk_states::all_set.end() ) {
        reply << " 8 : unrecognized disk state ;";
        return reply.str();
    }

    rte.xlrdev.write_state( new_state );
    
    reply << " 0 : " << new_state << " ;";
    return reply.str();
}
