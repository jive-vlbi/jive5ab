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


// Set/query mk5c fill pattern
string mk5c_fill_pattern_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    // Can only be used on the 5C
    ASSERT_COND( rte.ioboard.hardware() & ioboard_type::mk5c_flag );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // query can always be done
    // Command only allowed if the "i/o" board is not being used
    // (the Mark5C does not have an i/o board as such but we'll
    //  treat the 10GigE daughterboard as conceptual i/o board, because,
    //  logically, it performs the same function as the 5A/5B I/O boards)
    INPROGRESS(rte, reply, !qry && (fromio(ctm) || toio(ctm)))

    if( qry ) {
        reply << "0 : " << hex_t(*rte.xlrdev[ xlrreg::TENG_FILL_PATTERN ]) << " ;";
        return reply.str();
    }

    // Parse the numbah - it should be hex number. Follow DIMino -
    // e.g. the "mode=ext: ... " accepts decimal numbers there as well ...
    char*             eocptr;
    const string      fillpatstr( OPTARG(1, args) );
    unsigned long int fillpat;

    // check if there is at least one argument
    if( fillpatstr.empty() ) {
        reply << "8 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    errno   = 0;
    fillpat = ::strtoul(fillpatstr.c_str(), &eocptr, 0);
    ASSERT2_COND( eocptr!=fillpatstr.c_str() && *eocptr=='\0' &&
                  errno!=ERANGE && errno!=EINVAL && fillpat<=0xffffffff, 
                  SCINFO("invalid number for fill pattern") );

    // And write it in the hardware
    rte.xlrdev[ xlrreg::TENG_FILL_PATTERN ] = (UINT32)fillpat;

    reply << " 0 ;";

    return reply.str();
}
