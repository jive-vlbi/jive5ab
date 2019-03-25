// mount/unmount a bank [key on/off or off/on through software!]
// Copyright (C) 2007-2017 Harro Verkouter
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
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <iostream>
#include <set>
#include <signal.h>

using namespace std;


typedef XLR_RETURN_CODE (*mountfn_type)(SSHANDLE, UINT32);
typedef std::set<UINT32> mountlist_type;

// One-shot thread function which does the actual bank switch
struct mountargs {

    mountargs(runtime* rtep, mountlist_type const& mlt, mountfn_type fn):
        rteptr( rtep ),  mount_fn( fn ), banks( mlt )
    { EZASSERT2(rteptr, cmdexception, EZINFO("Don't construct thread args with NULL pointer!"));
      EZASSERT2(mount_fn, cmdexception, EZINFO("mount args constructed with NULL mountfn pointer!")); }

    runtime* const       rteptr;
    const mountfn_type   mount_fn;
    const mountlist_type banks;

    private:
        mountargs();
};


void mount_fn_impl(runtime* const rteptr, mountlist_type const& banks, mountfn_type const XLRCODE(mount_fn)) {
    // Attempt to do the (un)mount
    static const char       bankChar[] = {'A', 'B', '*'};
    mountlist_type::const_iterator ptr = banks.begin();
    try {
        // On the V100/VXF2 clearchannels is not good enough :-(
        XLRCALL( ::XLRSetMode(rteptr->xlrdev.sshandle(), SS_MODE_SINGLE_CHANNEL) );
        for(; ptr!=banks.end(); ptr++) {
            DEBUG(3, "mount_fn_impl/processing bank " << *ptr << endl)
            XLRCALL( mount_fn(rteptr->xlrdev.sshandle(), *ptr) );
        }
    }
    catch( const std::exception& e ) {
        DEBUG(-1, "mount_fn_impl/failed to do (un)mount " << 
                  (ptr==banks.end() ? bankChar[2] : bankChar[*ptr]) << " - " << e.what() << endl);
        push_error( error_type(1006, string("(un)mount failed - ")+e.what()) );
    }
    try {
        // force a check of mount status
        rteptr->xlrdev.update_mount_status();
    }
    catch( const std::exception& e ) {
        DEBUG(-1, "mount_fn_impl/failed to update mount status - " << e.what() << endl);
    }
    DEBUG(3, "mount_fn_impl/clearing runtime's transfer mode to no_transfer" << endl);
    // In the runtime, set the transfer mode back to no_transfer
    RTEEXEC(*rteptr, rteptr->transfermode = no_transfer);
}

void* mount_thrd(void* args) {
    mountargs*    mount = static_cast<mountargs*>(args);

    if( !mount ) {
        DEBUG(-1, "mount_thrd/Do not start thread function with NULL pointer for arguments" << endl);
        // we cannot put back the runtime's state because we don't have a
        // pointer to the runtime!
        return (void*)0;
    }
    mount_fn_impl(mount->rteptr, mount->banks, mount->mount_fn);
    // Free the storage space - it was allocated using "operator new"
    delete mount;

    return (void*)0;
}




#define BANKID(str) ((str=="A")?((UINT)BANK_A):((str=="B")?((UINT)BANK_B):(UINT)-1))

string mount_fn_bankmode( bool , const vector<string>& , runtime& );
string mount_fn_nonbankmode( bool , const vector<string>& , runtime& );

// High level wrapper
string mount_fn( bool qry, const vector<string>& args, runtime& rte) {
    // This one only handles mount= and unmount=
    if( !(args[0]=="mount" || args[0]=="unmount") )
        return string("!")+args[0]+((qry)?("?"):("="))+" 6 : not handled by this implementation ;";

    // They are really only available as commands
    if( qry )
        return string("!")+args[0]+((qry)?("?"):("="))+" 4 : only available as command ;";

    // Depending on which bankmode we're in defer to the actual mount/unmontfn
    const S_BANKMODE    curbm = rte.xlrdev.bankMode();
    if( curbm==SS_BANKMODE_NORMAL )
        return mount_fn_bankmode(qry, args, rte);
    else if( curbm==SS_BANKMODE_DISABLED )
        return mount_fn_nonbankmode(qry, args, rte);
    else
        return string("!")+args[0]+((qry)?("?"):("="))+" 4 : Neither in bank nor non-bank mode ;";
}



// (Un)Mount in bank mode:
//  mount   = a [ : b : c ...] (Yeah, only two banks but it's easier to
//                              pretend it's a list ...)
//  unmount = a [ : b : c ...]      id.
//
// It should already be pre-verified that this isn't a query so the bool argument
// is unused in here
string mount_fn_bankmode(bool , const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    mountlist_type      banks;
    const transfer_type ctm = rte.transfermode;

    // We require at least one argument!
    EZASSERT2( args.size()>1, Error_Code_8_Exception,
               EZINFO("insufficient number of arguments") );

    // we can already form *this* part of the reply
    reply << '!' << args[0] << '=';

    // Verify that we are eligible to execute in the first place:
    // no mount/unmount command whilst doing *anything* with the disks
    INPROGRESS(rte, reply, streamstorbusy(ctm) || diskunavail(ctm));

    // Collect all the arguments into a list (well, actually it's a set) of
    // banks to (un)mount. Note that we have asserted that there is at least
    // one argument (so "++ begin()" is guaranteed to be valid!)
    for(vector<string>::const_iterator curBank = ++args.begin(); curBank!=args.end(); curBank++) {
        const string bnkStr( ::toupper(*curBank) );
        EZASSERT2(bnkStr=="A" || bnkStr=="B", Error_Code_8_Exception, EZINFO(*curBank << " is not a valid bank - 'A' or 'B'"));
        banks.insert( bnkStr=="A" ? BANK_A : BANK_B );
    }
    rte.transfermode = mounting;
    mount_fn_impl(&rte, banks, (args[0]=="unmount" ? ::XLRDismountBank : ::XLRMountBank) );
    //mount_thrd( new mountargs(&rte, banks, (args[0]=="unmount" ? ::XLRDismountBank : ::XLRMountBank)) );
    reply << " 1; ";
    return reply.str();    
}

string mount_fn_nonbankmode( bool , const vector<string>& args, runtime& ) {
    return string("!")+args[0]+"= 2 : Not implemented in non-bank mode yet";
}
