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
#include <version.h>
#include <fcntl.h>
#include <iostream>

using namespace std;


string dtsid_fn(bool q, const vector<string>& args, runtime& rte) {
    int                         ndim = 0, ndom = 0;
    ostringstream               reply;
    const transfer_type         tm( rte.transfermode );
    ioboard_type::iobflags_type hw = rte.ioboard.hardware();

    reply << "!" << args[0] << (q?"?":"!");

    if( !q ) {
        reply << " 2 : only available as query ;";
        return reply.str();
    }

    // This query can execute always
    reply << " 0 : ";

    // <system type>
    if( hw&ioboard_type::mk5a_flag ) {
        reply << "mark5A";
        ndim = ndom = 1;
    } else if( hw&ioboard_type::mk5b_flag ) {
        reply << "mark5b";
        if( hw&ioboard_type::dim_flag )
            ndim = 1;
        else
            ndom = 1;
    } else if( hw&ioboard_type::mk5c_flag ) {
        reply << "Mark5C";
        ndim = 1;
        ndom = 0;
    } else
        reply << "-";
    // <software revision date> (timestamp of this SW version)
    reply << " : " << version_constant("DATE");
    // <media type>
    // 0 - magnetic tape, 1 - magnetic disk, 2 - realtime/nonrecording
    //  assume that if the transfermode == '*2net' or 'net2out' that we are
    //  NOT recording
    const bool realtime = (tm==in2net || tm==disk2net || tm==net2out);
    reply << " : " << ((realtime==true)?(2):(1));
    // <serial number>
    char   name[128];
    int    fd = ::open("/etc/hardware_id", O_RDONLY);
    string serial;

    if( fd>0 ) {
        int rr;
        if( (rr=::read(fd, name, sizeof(name)))>0 ) {
            vector<string> parts;
            // Use only the first line of that file; use everything up to 
            // the first newline.
            parts  = split(string(name), '\n');
            serial = parts[0];
        } else {
            serial = ::strerror(rr);
        }
        ::close(fd);
    } else {
        vector<string> parts;
        ::gethostname(name, sizeof(name));
        // split at "."'s and keep only first part
        parts = split(string(name), '.');
        serial = parts[0];
        DEBUG(3, "[gethostname]serial = '" << serial << "'" << endl);
    }
    reply << " : " << serial;
    // <#DIM ports>, <#DOM ports>
    reply << " : " << ndim << " : " << ndom;
    // <command set revision>
    if( hw&ioboard_type::mk5a_flag )
        reply << " : 2.7x";
    else if( hw&ioboard_type::mk5b_flag )
        reply << " : 1.12 ";
    else if( hw&ioboard_type::mk5c_flag )
        reply << " : 1.0 ";
    else 
        reply << " : - ";

    if( hw.empty() ) 
        // No Input/Output designrevisions 'cuz there ain't any
        reply << " : - : - ";
    else if( (hw&ioboard_type::mk5a_flag) || (hw&ioboard_type::mk5b_flag) )
        // <Input design revision> & <Output design revision> (in hex)
        reply << " : " << hex_t(rte.ioboard.idr())
              << " : " << hex_t(rte.ioboard.odr());

    reply << " ;";
    return reply.str();
}
