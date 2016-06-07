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
#include <mk5command/in2netsupport.h>


CHANNELTYPE inputchannel(hwtype hw) {
    switch( hw ) {
        case mark5a:
        case mark5b:
            return CHANNEL_FPDP_TOP;
        case mark5c:
            return CHANNEL_10GIGE;
        default:
            THROW_EZEXCEPT(cmdexception, "Attempt to get input channel for non 5A/B/C hw");
    }
}


// Implementations of the vdif size computation functions
unsigned int size_is_request(unsigned int req_vdif, unsigned int framesz) {
    const unsigned int  vdif_sz( (req_vdif==(unsigned int)-1) ? framesz : req_vdif );

    // Do verify that the size is compatible
    EZASSERT2( vdif_sz<=framesz && (framesz % vdif_sz)==0 && (vdif_sz % 8)==0, cmdexception,
               EZINFO("Requested vdif size " << req_vdif << " (=> vdif_sz=" << vdif_sz << ") is not "
                     "a valid vdif size (mod 8) or does not divide the frame size " << framesz) );
    return vdif_sz;
}

// Try finding the largest valid VDIF data array size that is < req_vdif and
// divides framesz
unsigned int size_is_hint(unsigned int req_vdif, unsigned int framesz) {
    unsigned int dataframe_length = 0;

    for(unsigned int i=1; dataframe_length==0 && i<framesz; i++) {
        const unsigned int dfl = framesz/i;

        if( (dfl%8)==0 && (framesz%dfl)==0 && dfl<=req_vdif )
            dataframe_length = dfl;
    }
    EZASSERT2( dataframe_length!=0, cmdexception,
               EZINFO("Could not find a suitable vdif frame size that divides framesz " << framesz <<
                      " and is also <= " << req_vdif) );
    return dataframe_length;
}
