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
#include <iterator>

using namespace std;


// net_port function
// 28 Feb 2019: support "net_port = [<host>@]<port>" to set
//              local IP address to record data from
//
//              If no leading "<host>@" is found, reset to default,
//              i.e. no local host, i.e. all local interfaces
// 10 Aug 2023: Support for
//              "net_port = [addr1@]port1[=suffix1] [:[addr2@]port2[=suffix2] : ...] ;"
//              the idea being that jive5ab can be programmed to listen on 
//              multiple local addresses (host or IPv4) and/or port numbers
//              and optionally assign suffixes to the recording names for
//              streams received on those addresses.
//              Note: suffixes only allowed if we have > 1 netports defined!
string net_port_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));

    // Query available always, command only when doing nothing
    INPROGRESS(rte, oss, !(q || rte.transfermode==no_transfer))

    if( q ) {
        oss << " 0";
        for(hpslist_type::const_iterator p=np.get_hps().begin(); p!=np.get_hps().end(); p++) {
            oss << " : ";
            if( !p->host.empty() )
                oss << p->host << '@';
            oss << p->port;
            if( !p->suffix.empty() )
                oss << '=' << p->suffix;
        }
        oss << " ;";
#if 0
        if( !np.host.empty() )
           oss << np.host << "@";
        oss << np.get_port() << " ;";
#endif
        return oss.str();
    }

    // Collect parsed arguments. 
    hpslist_type                   new_hps;
    vector<string>::const_iterator argptr = args.begin();

    // args[0] exists and is the name of the command itself.
    std::advance( argptr, 1 );
    for(; argptr!=args.end(); argptr++) {
        const string    arg_s( *argptr );

        // skip/ignore empty arguments
        if( arg_s.empty() )
            continue;

        char*                   eocptr;
        string                  host_s, port_s, suffix_s;
        const vector<string>    parts1 = ::split(arg_s, '=');
        const vector<string>    parts2 = ::split(parts1[0], '@'); // parts1[0] will always exist

        if( parts1.size()>1 ) {
            // only one equal sign?
            if( parts1.size()>2 ) {
                THROW_EZEXCEPT(Error_Code_8_Exception, "Too many =-signs in " << arg_s);
            }
            // equal sign w/o actual suffix makes no sense
            if( parts1[1].empty() ) {
                THROW_EZEXCEPT(Error_Code_8_Exception, "Missing suffix after =-sign");
            }
            suffix_s = parts1[1];
        }

        // Either two parts: "host@port" or just one, "port"
        if( parts2.size()==1 ) {
            port_s = parts2[0];
        } else if( parts2.size()==2 ) {
            host_s = parts2[0];
            port_s = parts2[1];
        } else {
            THROW_EZEXCEPT(Error_Code_8_Exception, "Please specify host@port or just port, not something else")
        }
        // We now know for sure we have a port and possibly a host and possibly a suffix
        // Check port number for acceptability
        unsigned long int       port;
        const unsigned long int p_max = (unsigned long int)std::numeric_limits<unsigned short>::max();

        errno = 0;
        port  = ::strtoul(port_s.c_str(), &eocptr, 0);
        // Check if it's an acceptable "port" value 
        EZASSERT2(eocptr!=port_s.c_str() && *eocptr=='\0' && errno!=ERANGE && port<=p_max,
                  Error_Code_8_Exception,
                  EZINFO("port '" << port_s << "' not a number/out of range (range: 0-" << p_max << ")"));

        new_hps.push_back( hps_type(host_s, (unsigned short)port, suffix_s) );
    }

    // command better have an argument otherwise 
    // it don't mean nothing
    if( new_hps.empty() ) {
        oss << " 8 : Missing argument to command ;";
    } else {
        // Verify that if there's only one element it does not have a suffix
        // If there are >1 netports defined and not everyone has a non-empty suffix
        // it is the job of the chunkmaker(s) to handle that - i.e. it should
        // not add just "_ds" but only if it's "_ds<nonempty suffix>"
        if( new_hps.size()==1 ) {
            if( !new_hps[0].suffix.empty() )
                THROW_EZEXCEPT(Error_Code_8_Exception, "Please do not specify a suffix if recording only one stream");
            // if port number is zero, the old behaviour was to go back to default state!
            // in the new API this is triggered by setting an empty hps list
            if( new_hps[0].port==0 )
                new_hps.clear();
        } else {
            // verify that either they all have no suffix or a suffix
            // we don't have to worry about an all-whitespace suffix (which should count as empty)
            // because all whitespace is stripped for us - VSI/S allows embedded whitespace, technically,
            // but we not sure anyone likes that
            hpslist_type::size_type      nEmpty = 0, nNonEmpty = 0;

            for(hpslist_type::const_iterator p=new_hps.begin(); p!=new_hps.end(); p++)
                (void)(p->suffix.empty() ? nEmpty++ : nNonEmpty++);
            if( nEmpty && nNonEmpty )
                THROW_EZEXCEPT(Error_Code_8_Exception, "Make sure that eiter all netports have a suffix or none");
        }
        np.set_hps( new_hps );
        oss << " 0 ;";
    }
    return oss.str();
}

