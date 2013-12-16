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


// Expect:
// net_protcol=<protocol>[:<socbufsize>[:<blocksize>[:<nblock>]]
// 
// Note: existing uses of eVLBI protocolvalues mean that when "they" say
//       'netprotcol=udp' they *actually* mean 'netprotocol=udps'
//       (see netparms.h for details). We will transform this silently and
//       add another value, "pudp" which will get translated into plain udp.
// Note: socbufsize will set BOTH send and RECV bufsize
string net_protocol_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream  reply;
    netparms_type& np( rte.netparms );

    // Can already form this part of the reply
    reply << "!" << args[0] << (qry?('?'):('='));

    // Query available always, command only when doing nothing
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer))

    if( qry ) {
        reply << " 0 : " << np.get_protocol() << " : " ;
        if( np.rcvbufsize==np.sndbufsize )
            reply << np.rcvbufsize;
        else
            reply << "Rx " << np.rcvbufsize << ", Tx " << np.sndbufsize;
        reply << " : " << np.get_blocksize()
              << " : " << np.nblock 
              << " ;";
        return reply.str();
    }

    // Not query. Pick up all the values that are given
    // If len(args)<=1 *and* it's not a query, that's a syntax error!
    if( args.size()<=1 )
        return string("!net_protocol = 8 : Empty command (no arguments given, really) ;");

    // Make sure the reply is RLY empty [see before "return" below why]
    reply.str( string() );

    // Extract potential arguments
    const string proto( OPTARG(1, args) );
    const string sokbufsz( OPTARG(2, args) );
    const string workbufsz( OPTARG(3, args) );
    const string nbuf( OPTARG(4, args) );

    // See which arguments we got
    // #1 : <protocol>
    if( proto.empty()==false )
        np.set_protocol( proto );

    // #2 : <socbuf size> [we set both send and receivebufsizes to this value]
    if( sokbufsz.empty()==false ) {
        char*      eptr;
        long int   v = ::strtol(sokbufsz.c_str(), &eptr, 0);

        // was a unit given? [note: all whitespace has already been stripped
        // by the main commandloop]
        EZASSERT2( eptr!=sokbufsz.c_str() && ::strchr("kM\0", *eptr),
                   cmdexception,
                   EZINFO("invalid socketbuffer size '" << sokbufsz << "'") );

        // Now we can do this
        v *= ((*eptr=='k')?KB:(*eptr=='M'?MB:1));

        // Check if it's a sensible "int" value for size, ie >=0 and <=INT_MAX
        if( v<0 || v>INT_MAX ) {
            reply << "!" << args[0] << " = 8 : <socbuf size> out of range <0 or > INT_MAX ; ";
        } else {
            np.rcvbufsize = np.sndbufsize = (int)v;
        }
    }

    // #3 : <workbuf> [the size of the blocks used by the threads]
    //      Value will be adjusted to accomodate an integral number of
    //      datagrams.
    if( workbufsz.empty()==false ) {
        char*               eptr;
        unsigned long int   v;
       
        errno = 0;
        v     = ::strtoul(workbufsz.c_str(), &eptr, 0);

        // was a unit given? [note: all whitespace has already been stripped
        // by the main commandloop]
        EZASSERT2( eptr!=workbufsz.c_str() && ::strchr("kM\0", *eptr) && errno!=ERANGE && errno!=EINVAL,
                   cmdexception,
                   EZINFO("invalid workbuf size '" << workbufsz << "'") );

        // Now we can do this
        v *= ((*eptr=='k')?KB:(*eptr=='M'?MB:1));

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v<=UINT_MAX ) {
            np.set_blocksize( (unsigned int)v );
        } else {
            reply << "!" << args[0] << " = 8 : <workbufsize> out of range (too large) ;";
        }
    }

    // #4 : <nbuf>
    if( nbuf.empty()==false ) {
        unsigned long int   v = ::strtoul(nbuf.c_str(), 0, 0);

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v>0 && v<=UINT_MAX )
            np.nblock = (unsigned int)v;
        else
            reply << "!" << args[0] << " = 8 : <nbuf> out of range - 0 or too large ;";
    }
    if( args.size()>5 )
        DEBUG(1,"Extra arguments (>5) ignored" << endl);

    // If reply is still empty, the command was executed succesfully - indicate so
    if( reply.str().empty() )
        reply << "!" << args[0] << " = 0 ;";
    return reply.str();
}
