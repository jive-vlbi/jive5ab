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


// Specialization for Mark5B/DOM. Currently it can only
// globally set the mode properties; no hardware
// settings are done. 
// This is here such that a Mark5B/DOM can do net2file
// correctly. (the sender and receiver of data have to
// have their modes set equally, for the constraint-solving
// to come up with the same values at either end of the
// transmission).
//
// Jun 2013: Add support for Mark5C.
//           It has "mode=unk" (for 'unknown').
//           What we'll do is silently convert "unk" to "none"
//           when setting + mapping it back when querying
string mk5bdom_mode_fn(bool qry, const vector<string>& args, runtime& rte) {
    const bool             is5c( rte.ioboard.hardware() & ioboard_type::mk5c_flag );
    ostringstream          reply;
    mk5bdom_inputmode_type ipm( mk5bdom_inputmode_type::empty );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // query can always be done
    if( qry ) {
        const format_type  fmt = rte.trackformat();
        if( is_vdif(fmt) )
            reply << "0 : " << fmt << " : " << rte.ntrack() << " : " << rte.vdifframesize() << " ;";
        else {
            rte.get_input( ipm );
            if( is5c && ipm.mode=="none" )
                ipm.mode = "unk";
            reply << "0 : " << ipm.mode << " : " << rte.ntrack() << " : " << rte.trackformat() << " ;";
        }
        return reply.str();
    }

    // Command only allowed if doing nothing
    if( rte.transfermode!=no_transfer ) {
        reply << "6 : Cannot change during transfers ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "8 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    ipm.mode   = OPTARG(1, args);
    ipm.ntrack = OPTARG(2, args);

    // set mode to h/w
    if( ipm.mode.find("vdif")!=string::npos ) {
        rte.set_vdif(args);
    } else {
        if( is5c && ipm.mode=="unk" )
            ipm.mode = "none";
        if( ipm.mode=="mark5b" ) {
            EZASSERT2(is5c, cmdexception, EZINFO("mode=mark5b only supported on Mark5C"));
        }
        rte.set_input( ipm );
    }
    reply << "0 ;";

    return reply.str();
}
