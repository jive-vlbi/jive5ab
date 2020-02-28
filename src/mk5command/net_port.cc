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

using namespace std;


// net_port function
// 28 Feb 2019: support "net_port = [<host>@]<port>" to set
//              local IP address to record data from
//
//              If no leading "<host>@" is found, reset to default,
//              i.e. no local host, i.e. all local interfaces
string net_port_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));

    // Query available always, command only when doing nothing
    INPROGRESS(rte, oss, !(q || rte.transfermode==no_transfer))

    if( q ) {
        oss << " 0 : ";
        if( !np.host.empty() )
           oss << np.host << "@";
        oss << np.get_port() << " ;";
        return oss.str();
    }
 
    // command better have an argument otherwise 
    // it don't mean nothing
    const string    arg_s = OPTARG(1, args);

    if( !arg_s.empty() ) {
        char*                   eocptr;
        string                  host_s, port_s;
        const vector<string>    parts = ::split(arg_s, '@');

        // Either two parts: "host@port" or just one, "port"
        if( parts.size()==1 ) {
            port_s = parts[0];
        } else if( parts.size()==2 ) {
            host_s = parts[0];
            port_s = parts[1];
        } else {
            THROW_EZEXCEPT(Error_Code_8_Exception, "Please specify host@port or just port, not something else")
        }
        // We now know for sure we have a port and possibly a host
        // Check port number for acceptability
        unsigned long int       port;
        const unsigned long int p_max = (unsigned long int)std::numeric_limits<unsigned short>::max();

        errno = 0;
        port  = ::strtoul(port_s.c_str(), &eocptr, 0);
        // Check if it's an acceptable "port" value 
        EZASSERT2(eocptr!=port_s.c_str() && *eocptr=='\0' && errno!=ERANGE && port<=p_max,
                  Error_Code_8_Exception,
                  EZINFO("port '" << port_s << "' not a number/out of range (range: 0-" << p_max << ")"));

        np.set_port( (unsigned short)port );
        // And blindly overwrite the host?
        np.host = host_s;
        oss << " 0 ;";
    } else {
        oss << " 8 : Missing argument to command ;";
    }
    return oss.str();
}

