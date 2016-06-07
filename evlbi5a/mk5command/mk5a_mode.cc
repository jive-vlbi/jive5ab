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
#include <streamutil.h>
#include <iostream>

using namespace std;


// specialization for Mark5A(+)
string mk5a_mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;

    // This part of the reply we can already form
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // query can always be done
    // Command only allowed if doing nothing
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer))

    if( qry ) {
        const samplerate_type   rate = rte.trackbitrate()/1000000;
        mk5bdom_inputmode_type  magicmode;
       
        // If magicmode.mode.empty()==false, this means
        // that the magic mode has been set.
        // Provide different output in that case
        rte.get_input( magicmode );

        if( magicmode.mode.empty()==false ) {
            reply << "0 : " << magicmode.mode << " : "
                  << rte.trackformat() << " : " << rte.ntrack() << " : " << format("%.3lf", boost::rational_cast<double>(rate));
            if( is_vdif(rte.trackformat()) )
                reply << " : " << rte.vdifframesize();
            reply << " ;";
            return reply.str();
        }

        // Not magic mode - normal hardware response
        inputmode_type  ipm;
        outputmode_type opm;

        rte.get_input( ipm );
        rte.get_output( opm );

        reply << "0 : "
            << ipm.mode << " : " << ipm.submode << " : "
            << opm.mode << " : " << opm.submode << " : "
            << (opm.synced?('s'):('-')) << " : " << opm.numresyncs
            << " ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "8 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    inputmode_type  ipm( inputmode_type::empty );
    outputmode_type opm( outputmode_type::empty );

    // If we are called as "mode=<argument>" (one argument only)
    // this MUST be the 'magic mode' command.
    if( args.size()==2 ) {
        mk5bdom_inputmode_type  magicmode( mk5bdom_inputmode_type::empty );

        magicmode.mode = args[1];
        rte.set_input( magicmode );
        reply << "0 ;";
        return reply.str();
    }

    // first argument. the actual 'mode'
    if( args.size()>=2 && args[1].size() ) {
        opm.mode = ipm.mode = args[1];
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
    if ( !ipm.mode.empty() )
        rte.set_input( ipm );
    if ( !opm.mode.empty() )
        rte.set_output( opm );

    // no reply yet indicates "ok"
    reply << "0 ;";
    return reply.str();
}
