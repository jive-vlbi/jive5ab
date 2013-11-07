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


// specialization for Mark5A(+)
string mk5a_mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;

    // query can always be done
    // Command only allowed if doing nothing
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer))

    if( qry ) {
        format_type  fmt = rte.trackformat();

        if( is_vdif(fmt) )
            reply << "!" << args[0] << "? 0 : " << fmt << " : " << rte.ntrack() << " : " << rte.vdifframesize() << " ;";
        else {
            inputmode_type  ipm;
            outputmode_type opm;

            rte.get_input( ipm );
            rte.get_output( opm );

            reply << "!" << args[0] << "? 0 : "
                << ipm.mode << " : " << ipm.submode << " : "
                << opm.mode << " : " << opm.submode << " : "
                << (opm.synced?('s'):('-')) << " : " << opm.numresyncs
                << " ;";
        }
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "!" << args[0] << "= 8 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    inputmode_type  ipm( inputmode_type::empty );
    outputmode_type opm( outputmode_type::empty );

    reply.str( string() );

    // first argument. the actual 'mode'
    if( args.size()>=2 && args[1].size() ) {
        opm.mode = ipm.mode = args[1];
    }

    // when setting vdif do not try to send it to the hardware
    if( ipm.mode.find("vdif")!=string::npos ) {
        rte.set_vdif(args);
        reply << "!" << args[0] << "= 0 ;";
        return reply.str();
    } 
    
    if( ipm.mode!="none" && !ipm.mode.empty() ) {
        // Looks like we're not setting the bypassmode for transfers

        // 2nd arg: submode
        if( args.size()>=3 ) {
            ipm.submode = opm.submode = args[2];
        }
    }

    // if output mode is set explicitly, override them
    if ( args.size() >= 5 ) {
        opm.mode = args[3];
        opm.submode = args[4];
    }

    // set mode to h/w
    if ( !ipm.mode.empty() ) {
        rte.set_input( ipm );
    }
    if ( !opm.mode.empty() ) {
        rte.set_output( opm );
    }

    // no reply yet indicates "ok"
    if( reply.str().empty() )
        reply << "!" << args[0] << "= 0 ;";
    return reply.str();
}
