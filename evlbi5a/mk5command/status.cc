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

using namespace std;


// status? 
string status_fn(bool q, const vector<string>&, runtime& rte) {

    if( !q )
        return "!status= 2 : only available as query ;";

    // flag definitions for readability and consistency
    const unsigned int record_flag   = 0x1<<6; 
    const unsigned int playback_flag = 0x1<<8; 
    // automatic variables
    error_type         error( peek_error() );
    unsigned int       st;
    ostringstream      reply;

    // Check that we may execute. Apparently the only time we cannot be
    // called is during conditioning or bankswitching
    reply << "!status? ";

    INPROGRESS(rte, reply, diskunavail(rte.transfermode))

    // compile the hex status word
    st = 1; // 0x1 == ready
    switch( rte.transfermode ) {
        case in2disk:
        case in2memfork:
            st |= record_flag;
            break;
        case disk2file:
            st |= playback_flag;
            st |= (0x1<<12); // bit 12: disk2file
            break;
        case file2disk:
            st |= record_flag;
            st |= (0x1<<13); // bit 13: disk2file
            break;
        case disk2net:
            st |= playback_flag;
            st |= (0x1<<14); // bit 14: disk2net active
            break;
        case net2disk:
            st |= record_flag;
            st |= (0x1<<15); // bit 15: net2disk
            break;
        case in2net:
            st |= record_flag;
            st |= (0x1<<16); // bit 16: in2net active/waiting
            break;
        case net2out:
            st |= playback_flag;
            st |= (0x1<<17); // bit 17: net2out active
            break;
        default:
            // d'oh
            break;
    }
    if( error ) {
        st |= (0x1 << 1); // bit 1, error message pending
    }
    if( rte.transfermode != no_transfer ) {
        st |= (0x3 << 3); // bit 3 and 4, delayed completion command pending
    }

    S_BANKSTATUS bs[2];

    // only really call streamstor API functions if we're compiled with
    // SSAPI support
#ifdef NOSSAPI
    // make sure it's all zeroes
    ::memset(&bs[0], 0, sizeof(bs));
#else
    XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_A, &bs[0]) );
    XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_B, &bs[1]) );
#endif
    for ( unsigned int bank = 0; bank < 2; bank++ ) {
        if ( bs[bank].Selected ) {
            st |= (0x1 << (20 + bank * 4)); // bit 20/24, bank selected
        }
        if ( bs[bank].State == STATE_READY ) {
            st |= (0x1 << (21 + bank * 4)); // bit 21/25, bank ready
        }
        if ( bs[bank].MediaStatus == MEDIASTATUS_FULL || bs[bank].MediaStatus == MEDIASTATUS_FAULTED ) {
            st |= (0x1 << (22 + bank * 4)); // bit 22/26, bank full or faulty (not writable)
        }
        if ( bs[bank].WriteProtected ) {
            st |= (0x1 << (23 + bank * 4)); // bit 23/27, bank write protected
        }
    }

    // if need be, we could add the error number & message to the reply ...
    // (this is what DIMino does)
    reply << " 0 : " << hex_t(st);

    if( error )
       reply << " : " << error.number
             << " : " << error.message
             << " : " << pcint::timeval_type(error.time);
    reply << " ;";
    return reply.str();
}
