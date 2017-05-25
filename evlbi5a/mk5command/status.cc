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
        case vbsrecord:
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


    // only really call streamstor API functions if we're compiled with
    // SSAPI support
#ifndef NOSSAPI
    S_BANKSTATUS     bs[2];
    const S_BANKMODE bm = rte.xlrdev.bankMode();

    // make sure it's all zeroes
    ::memset(&bs[0], 0, sizeof(bs));

    // Depending on which mode the device is in, do the appropriate query
    if( bm==SS_BANKMODE_NORMAL ) {
        // Query individual banks
        XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_A, &bs[0]) );
        XLRCALL( ::XLRGetBankStatus(rte.xlrdev.sshandle(), BANK_B, &bs[1]) );
    } else if( bm==SS_BANKMODE_DISABLED ) {
        // In non-bank mode it's a tad more difficult
        bool               status[ 2 ] = { true, true };
        // SDK8 calls the return value of XLRGetLength() DWORDLONG, SDK9 calls it UINT64 
        // and neiter bother to typedef an opaque type for an application
        // writer that he/she can use to store the result of XLRGetLength()
        // in. Inspection of the SDK8/SDK9 header files reveals that both
        // are typedefs for 'unsigned long long'. So why don't we just
        // forego the SDK's types and go for the underlying type?
        // [because it's bad - but what can I do if Conduant decide to
        //  change it yet again inna future 128 bit release??? (let's hope
        //  we'll *never* get to see that day!)]
        unsigned long long length; 
        unsigned int       nDisk[ 2 ]  = { 0   , 0    };

        // How do we know that both packs are 'active'?
        // Maybe just loop over the number of buses?
        // one pack = 4 buses * 2 (master/slave)
        // so if we have >4 buses we're good?
        do_xlr_lock();

        // We must get the recorded length in order to tell wether or not
        // the disks are EMPTY (see below)
        length = ::XLRGetLength(GETSSHANDLE(rte));

        for(unsigned int nr = 0; nr<16; nr++) {
            S_DRIVEINFO        di;
            XLR_RETURN_CODE    dsk  = XLR_FAIL;
            const unsigned int bus  = nr/2;
            const unsigned int bank = bus/4;
            const DRIVETYPE    type = ((nr%2)==0) ? XLR_MASTER_DRIVE : XLR_SLAVE_DRIVE; //slave = (nr % 2);

            XLRCODE( dsk = ::XLRGetDriveInfo(GETSSHANDLE(rte), bus, type, &di) );
            if( dsk==XLR_SUCCESS ) {
                // detected a disk in bank 'bank'
                nDisk[ bank ]++;

                if( di.SMARTCapable )
                    status[ bank ] = (status[ bank ] && di.SMARTState);
            }
        }
        do_xlr_unlock();

        // Now see what we got. Transform the per-disk into per bank stuff
        for(unsigned int i=0; i<2; i++) {
            bs[ i ].Selected = nDisk[i]>0 ? TRUE : FALSE;
            bs[ i ].State    = nDisk[i]>0 ? STATE_READY : STATE_NOT_READY;
            bs[ i ].MediaStatus = (status[i]==false) ? MEDIASTATUS_FAULTED :
                                      ((length==0 && nDisk[i]) ? MEDIASTATUS_EMPTY : 
                                        (nDisk[i] ? MEDIASTATUS_NOT_EMPTY : 0)); 
        }
    }

    // Now report status of the individual banks (even in non-bank mode ... (jeez)
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
#endif

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
