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
