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
#include <carrayutil.h>
#include <iostream>

using namespace std;


struct tmps {
    xlrreg::teng_register   reg;
    const string            desc;
};

string debug_fn( bool q, const vector<string>& args, runtime& rte ) {
    if( !q )
        return string("!")+args[0]+"= 2 : only available as query;";

    if( (rte.ioboard.hardware()&ioboard_type::mk5a_flag) || 
        (rte.ioboard.hardware()&ioboard_type::mk5b_flag) ) {
            rte.ioboard.dbg();
    } else if( rte.ioboard.hardware()&ioboard_type::mk5c_flag ) {
        const tmps tmpar[] = { 
                     {xlrreg::TENG_BYTE_LENGTH_CHECK_ENABLE, "ByteLenghtCheck Enable"}
                   , {xlrreg::TENG_DISABLE_MAC_FILTER, "Disable MAC filter"}
                   , {xlrreg::TENG_PROMISCUOUS, "Promiscuous"}
                   , {xlrreg::TENG_CRC_CHECK_DISABLE, "CRC Check disable"}
                   , {xlrreg::TENG_DISABLE_ETH_FILTER, "Disable ETH filter"}
                   , {xlrreg::TENG_PACKET_LENGTH_CHECK_ENABLE, "Packet length check"}
                   , {xlrreg::TENG_MAC_F_EN, "MAC 0xF enable"}
                   , {xlrreg::TENG_DPOFST, "DPOFST"}
                   , {xlrreg::TENG_DFOFST, "DFOFST"}
                   , {xlrreg::TENG_PACKET_LENGTH, "PACKET_LENGTH"}
                   , {xlrreg::TENG_BYTE_LENGTH, "BYTE_LENGTH"}
                   , {xlrreg::TENG_PSN_MODES, "PSN_MODES"}
                   , {xlrreg::TENG_PSNOFST, "PSNOFST"}
        };
        for(size_t i=0; i<array_size(tmpar); i++) 
            cout << tmpar[i].desc << "\t:" << *rte.xlrdev[tmpar[i].reg] << endl;
    }
    return string("!")+args[0]+"? 0 ;";
}
