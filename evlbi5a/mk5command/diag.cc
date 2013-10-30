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


// set/qry Mark5C daughter board registers
string diag_fn(bool qry, const vector<string>& args, runtime& XLRCODE(rte)) {
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    if( qry ) {
        reply << " 4 : only available as command  ;";
        return reply.str();
    }

    // Make sure we have at least one argument
    long           address, value;
    char*          eocptr;
    const string   address_str = OPTARG(1, args);
    const string   value_str   = OPTARG(2, args);

    if( address_str.empty() ) {
        reply << " 4 : expect at least one argument ;";
        return reply.str();
    }

    // Verify it's a number [we accept all bases]
    errno   = 0;
    address = ::strtol(address_str.c_str(), &eocptr, 0);
    EZASSERT2( eocptr!=address_str.c_str() && *eocptr=='\0' && !(address==0 && errno==ERANGE),
               Error_Code_8_Exception, EZINFO("Daughter board register address should be a number") );

    // Shouldn't be <0 or >0x32
    EZASSERT2( address>=0 && address<=0x32, Error_Code_8_Exception,
               EZINFO("Daughter board register address out of range") );

    // Value set? Write it. Otherwise read it
    if( value_str.empty() ) {
        UINT32      regval = 0xDEADC0DE;
        XLRCALL( ::XLRReadDBReg32(rte.xlrdev.sshandle(), (UINT32)address, &regval) );
        reply << " 0 : " << hex_t(address) << " : " << hex_t(regval) << " ;";
    } else {
        errno   = 0;
        value = ::strtol(value_str.c_str(), &eocptr, 0);
        EZASSERT2( eocptr!=value_str.c_str() && *eocptr=='\0' && !(value==0 && errno==ERANGE),
                   Error_Code_8_Exception, EZINFO("Daughter board register value should be a number") );
        XLRCALL( ::XLRWriteDBReg32(rte.xlrdev.sshandle(), (UINT32)address, value) );
        reply << " 0 ";
    }
    return reply.str();
}
