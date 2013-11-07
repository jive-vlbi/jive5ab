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
#include <stringutil.h>
#include <iostream>
#include <map>

using namespace std;

typedef map<S_BANKMODE, string> bmMap_type;
bmMap_type mk_bmMap( void );

static bmMap_type  bmMap = mk_bmMap();

// The mark5C "personality" command. For now we only support
// "mark5c" and "bank" or "nonbank" [note that jive5ab removes
//  embedded white space]
string personality_fn(bool q, const vector<string>& args, runtime& rte) {
    static string       my_personality = "mark5C"; 
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    reply << "!" << args[0] << (q?('?'):('='));

    // Query is acceptable when disks are available, command only when doing
    // nothing [note: command is also impossible when disks are unavailable]
    INPROGRESS(rte, reply, diskunavail(ctm) || !(q || ctm==no_transfer)) 

    if( q ) {
        // Figure out in which bank mode we're running
        bmMap_type::const_iterator bmptr = bmMap.find( rte.xlrdev.bankMode() );
       
        if( bmptr!=bmMap.end() ) 
            reply << " 0 : " << my_personality << " : " << bmptr->second << " ;";
        else
            reply << " 4 : " << my_personality << " : unsupported bank mode (#" << rte.xlrdev.bankMode() << " ) ;";
        return reply.str();
    }

    // Command!
    const string  personality( ::tolower(OPTARG(1, args)) );
    const string  bankmode( ::tolower(OPTARG(2, args)) );

    // Verify user input
    EZASSERT2(!(personality.empty() || bankmode.empty()), Error_Code_6_Exception, EZINFO("command must have two arguments"));
    EZASSERT2(personality=="mark5c", Error_Code_6_Exception, EZINFO("Unfortunately only mark5C personality supported"));
    EZASSERT2(bankmode=="bank" || bankmode=="nonbank", Error_Code_6_Exception, EZINFO("Invalid bank mode specified"));

    // Execute
    my_personality = personality;
    rte.xlrdev.setBankMode( (bankmode=="bank")?SS_BANKMODE_NORMAL:SS_BANKMODE_DISABLED );

    reply << " 0 ;";

    return reply.str();
}


bmMap_type mk_bmMap( void ) {
    bmMap_type  rv;

    rv[SS_BANKMODE_NORMAL]   = "bank";
    rv[SS_BANKMODE_DISABLED] = "non bank";
    return rv;
}
