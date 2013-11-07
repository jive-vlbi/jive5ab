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
#include <limits.h>
#include <iostream>

using namespace std;


string scandir_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;

    reply << "!" << args[0] << (q?('?'):('='));

    // This function (both qry and cmd version)
    // should only be unavailable if the disks are unavailable
    INPROGRESS(rte, reply, diskunavail(rte.transfermode))

    unsigned int   scannum( 0 );
    const string   scan( OPTARG(1, args) );

    reply << " 0 : " << rte.xlrdev.nScans();
    if( !scan.empty() ) {
        unsigned long int    v;
       
        errno = 0;
        v     = ::strtoul(scan.c_str(), 0, 0);

        if( ((v==ULONG_MAX) && errno==ERANGE) || v>=UINT_MAX )
            throw cmdexception("value for scannum is out-of-range");
        scannum = (unsigned int)v; 
    }
    if( scannum<rte.xlrdev.nScans() ) {
        ROScanPointer  rosp( rte.xlrdev.getScan(scannum) );

        reply << " : " << rosp.name() << " : " << rosp.start() << " : " << rosp.length();
    } else {
        reply << " : <scan # " << scannum << "> out of range";
    }
    reply << " ;";
    return reply.str();
}
