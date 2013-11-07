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


// specialization for Mark5B/DIM
// We do *not* test for DIM; others should've
// checked for us
// mode={ext|tvg|ramp}:<bitstreammask>[:<decimation ratio>[:<fpdpmode>]]
// fpdpmode not supported by this code.
// We allow 'tvg+<num>' to set a specific tvg mode. See runtime.h for 
// details. Default will map to 'tvg+1' [internal tvg]
string mk5bdim_mode_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    mk5b_inputmode_type curipm;

    // Wether this is command || query, we need the current inputmode
    rte.get_input( curipm );

    // This part of the reply we can already form
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is always possible, command only if doing nothing
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer))

    if( qry ) {
        format_type fmt = rte.trackformat();

        if( is_vdif(fmt) )
            reply << "0 : " << fmt << " : " << rte.ntrack() << " : " << rte.vdifframesize() << " ;";
        else {
            // Decimation = 2^j
            const int decimation = (int)::round( ::exp(curipm.j * M_LN2) );
            reply << "0 : " << curipm.datasource << " : " << hex_t(curipm.bitstreammask)
                << " : " << decimation << " : "
                << (*rte.ioboard[mk5breg::DIM_II]) + 1
                << " ;";
        }
        return reply.str();
    }

    // We require at least two non-empty arguments
    // ('data source' and 'bitstreammask')
    // (unless the mode == "none", in which case no ekztra arguments
    //  are req'd)
    if( (args.size()<=1) || /* only the command or nothing at all? that is never any good */
        (args.size()==2 && args[1]!="none") || /* only "mode = none" is acceptable in this case */
        (args.size()==3 && (args[1].empty() || args[2].empty())) /* not two non-empty arguments */
      ) {
        reply << "8 : must have at least two non-empty arguments ;";
        return reply.str(); 
    }

    // Are we setting VDIF?
    if( args[1].find("vdif")!=string::npos ) {
        rte.set_vdif(args);
        reply << " 0 ;";
        return reply.str();
    }
    // Start off with an empty inputmode.
    int                     tvgmode;
    mk5b_inputmode_type     ipm( mk5b_inputmode_type::empty );

    // Get the current inputmode. _some_ parameters must be left the same.
    // For (most, but not all) non-boolean parameters we have 'majik' values
    // indicating 'do not change this setting' but for booleans (and some other
    // 'verbatim' values that impossible).
    // So we just copy the current value(s) of those we want to keep unmodified.

    // use 'clock_set' to modify these!
    ipm.selcgclk  = curipm.selcgclk; 
    ipm.seldim    = curipm.seldim;
    ipm.seldot    = curipm.seldot;

    ipm.userword  = curipm.userword;
    ipm.startstop = curipm.startstop;
    ipm.tvrmask   = curipm.tvrmask;
    ipm.gocom     = curipm.gocom;
    ipm.fpdp2     = curipm.fpdp2;

    // Other booleans (tvgsel a.o. are explicitly set below)
    // or are fine with their current default

    // Argument 1: the datasource
    // If the 'datasource' is "just" tvg, this is taken to mean "tvg+1"
    ipm.datasource     = ((args[1]=="tvg")?(string("tvg+1")):(args[1]));

    DEBUG(2, "Got datasource " << ipm.datasource << endl);

    // Now check what the usr wants
    if( ipm.datasource=="ext" ) {
        // aaaaah! Usr want REAL data!
        ipm.tvg        = 0;
        ipm.tvgsel     = false;
    } else if( ipm.datasource=="ramp" ) {
        // Usr want incrementing test pattern. Well, let's not deny it then!
        ipm.tvg        = 7;
        ipm.tvgsel     = true;
    } else if( ::sscanf(ipm.datasource.c_str(), "tvg+%d", &tvgmode)==1 ) {
        // Usr requested a specific tvgmode.
        ipm.tvg        = tvgmode;
        // Verify that we can do it

        // tvgmode==0 implies external data which contradicts 'tvg' mode.
        // Also, a negative number is out-of-the-question
        ASSERT2_COND( ipm.tvg>=1 && ipm.tvg<=8, SCINFO(" Invalid TVG mode number requested") );

        ipm.tvgsel     = true;

        // these modes request FPDP2, verify the H/W can do it
        if( ipm.tvg==3 || ipm.tvg==4 || ipm.tvg==5 || ipm.tvg==8 ) {
           ASSERT2_COND( rte.ioboard.hardware()&ioboard_type::fpdp_II_flag,
                         SCINFO(" requested TVG mode needs FPDP2 but h/w does not support it") );
           // do request FPDP2 - but may already been set
           ipm.fpdp2   = true;
        }
    } else if( ipm.datasource=="none" ) {
        // Set mode directly - do not try to parse bitstreammask etc
        rte.set_input( ipm );
        reply << "0 ; ";
        return reply.str();
    } else {
        reply << "8 : Unknown datasource " << args[1] << " ;";
        return reply.str();
    }

    // Argument 2: the bitstreammask in hex.
    // Be not _very_ restrictive here. "The user will know
    // what he/she is doing" ... HAHAHAAA (Famous Last Words ..)
    // The 'set_input()' will do the parameter verification so
    // that's why we don't bother here
    // 03 Jun 2013: HV - there's no error checking here at all.
    //              Let's fix that. Also relax the interpretation
    //              to allow decimal numbers to be given. DIMino
    //              seems to allow that.
    char*         eocptr;
    unsigned long ul;

    errno = 0;
    ul    = ::strtoul(args[2].c_str(), &eocptr, 0);
    ASSERT2_COND( eocptr!=args[2].c_str() && *eocptr=='\0' &&
                  !(ul==ULONG_MAX && errno==ERANGE) && !(ul==0 && errno==EINVAL),
                  SCINFO("Bitstream mask invalid") );
    ipm.bitstreammask    = (uint32_t)ul;

    // Optional argument 3: the decimation.
    // Again, the actual value will be verified before it is sent to the H/W
    // The decimation is 'j', not 'k'! Bah!
    // Also: the argument is/should be given as one of: 1,2,4,8,16
    // the 'j' value is the exponent we must write into the H/W.
    if( args.size()>=4 && !args[3].empty() ) {
        int     i_decm;
        double  decm_req( ::strtod(args[3].c_str(), 0) ), decm_closest;

        // from the double value, find the closest exponent
        // of 2 that yields the requested decimation.
        i_decm       = (int)::round( ::log(decm_req)/M_LN2 );
        decm_closest = ::exp(i_decm * M_LN2);

        // We only allow decimation up to 16 [0 < i_decm <= 4]
        ASSERT2_COND( (i_decm>=0 && i_decm<=4),
                      SCINFO(" Requested decimation is not >=1 and <=16") );
        // And it must be a power of two!
        ASSERT2_COND( ::fabs(decm_req - decm_closest)<=0.01,
                      SCINFO(" Requested decimation is not a power of 2") );

        // Great. Now transfer the integer value to the h/w
        ipm.j = i_decm;
    }

    // Optional argument 4: fpdp2 mode, "1" or "2"
    const string fpdp2( OPTARG(4, args) );
    EZASSERT(fpdp2.empty()==true || fpdp2=="1" || fpdp2=="2", Error_Code_6_Exception);

    if( fpdp2=="2" ) {
        // If user explicitly requests II but the hardware cannot do it ...
        EZASSERT2( rte.ioboard.hardware()&ioboard_type::fpdp_II_flag, Error_Code_6_Exception,
                  EZINFO("FPDP II requested but h/w does not support it") );
        ipm.fpdp2 = true;
    } else if( fpdp2=="1" ) {
        // force fpdp mode to '1'
        ipm.fpdp2 = false;
    }

    // Make sure other stuff is in correct setting
    ipm.gocom         = false;

    rte.set_input( ipm );

    reply << "0 ; ";
    // Return answer to caller
    return reply.str();
}
