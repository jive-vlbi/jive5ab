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
#include <carrayutil.h>
#include <iostream>

using namespace std;


// Support for the Mark5C "packet" function
//  packet = DPOFST : DFOST : length : PSNMode : PSNOfst [ : <raw mode> ]
//
//  where DPOFST = data payload offset (wrt UDP header)
//        DFOFST = data frame offset (wrt start of payload)
//        length = #-of-bytes to record from the packet
//        PSNMode = packet sequence number monitoring mode. Currently
//                  (May 2013) only PSN Mode "0" seems to work
//        PSNOfst = offset of PSN from beginning of payload
//
//        <raw mode> = "0" or "1". 
//                     added by JIVE. An override to disable length
//                     filtering alltogether. Default is "0" (enable filter).
// Write parameters to the hardware, if given
enum param_type { 
    // these values indicate the positional parameter of the corresponding
    // argument in the "packet" command
    DPOFST  = 1, DFOFST  = 2, LENGTH = 3,
    PSNMODE = 4, PSNOFST = 5, RAW = 6 
    // pseudo - not for the commandline parsing but for register writing
    , PSN_MODE1 = 7, PSN_MODE2 = 8, MACDISABLE = 9, LENENABLE = 10,
    PKTLENENABLE = 11, CRCDISABLE = 12, PROMISCUOUS = 13, ETHDISABLE = 14
};
struct value_type {
    UINT32                value;
    xlrreg::teng_register reg;

    value_type(UINT32 v, xlrreg::teng_register r):
        value( v ), reg( r )
    {}
};
typedef std::map<param_type, value_type> pv_map_type;

string mk5c_packet_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Query is possible always, command only when the i/o board
    // (==10GigE in this case) is not in use
    INPROGRESS(rte, reply, !qry && (toio(ctm) || fromio(ctm)))

    if( qry ) {
        static const xlrreg::teng_register toread[] = { 
              xlrreg::TENG_DPOFST
            , xlrreg::TENG_DFOFST
            , xlrreg::TENG_BYTE_LENGTH
            , xlrreg::TENG_PSN_MODES
            , xlrreg::TENG_PSNOFST
        };
        static const size_t  num2read = array_size(toread);
        UINT32*              regvals = new UINT32[ num2read ];

        for(size_t i=0; i<num2read; i++)
            regvals[ i ] = *rte.xlrdev[ toread[i] ];
        // do some processing on the PSN mode *sigh*
        // PSN mode 0 => psn1 = 0,  psn2 = 0
        // PSN mode 1 => psn1 = 1,  psn2 = 0
        // PSN mode 2 => psn1 = 0,  psn2 = 1
        // By reading both PSN mode bits we can divide by 2
        // to get the actual psn mode
        regvals[ 3 ] /= 2;

        reply << " 0 ";
        for(size_t i=0; i<num2read; i++)
            reply << " : " << regvals[ i ];
        reply << " ; ";

        delete [] regvals;
        return reply.str();
    }

    // if command, we require 'n argument
    // for now, we discard the first argument but just look at the frequency
    if( args.size()<2 ) {
        reply << "8 : not enough arguments to command ;";
        return reply.str();
    }

    string      tmp;
    pv_map_type pvMap;


    // DPOFST
    if( !(tmp=OPTARG(DPOFST, args)).empty() ) {
        // parse into a UINT32
        char*         eocptr;
        unsigned long ul;
        ul = ::strtoul(tmp.c_str(), &eocptr, 0);
        EZASSERT2( eocptr!=tmp.c_str() && *eocptr=='\0',
                   cmdexception,
                   EZINFO("DPOFST '" << tmp << "' is not a number") );
        pvMap.insert( make_pair(DPOFST, value_type((UINT32)ul, xlrreg::TENG_DPOFST)) );
    }

    // DFOFST
    if( !(tmp=OPTARG(DFOFST, args)).empty() ) {
        // parse into a UINT32
        char*         eocptr;
        unsigned long ul;
        ul = ::strtoul(tmp.c_str(), &eocptr, 0);
        EZASSERT2( eocptr!=tmp.c_str() && *eocptr=='\0',
                   cmdexception,
                   EZINFO("DFOFST '" << tmp << "' is not a number") );
        pvMap.insert( make_pair(DFOFST, value_type((UINT32)ul, xlrreg::TENG_DFOFST)) );
    }

    // Length of the recording
    if( !(tmp=OPTARG(LENGTH, args)).empty() ) {
        // parse into a UINT32
        char*         eocptr;
        unsigned long length;
        length = ::strtoul(tmp.c_str(), &eocptr, 0);
        EZASSERT2( eocptr!=tmp.c_str() && *eocptr=='\0',
                   cmdexception,
                   EZINFO("LENGTH '" << tmp << "' is not a number") );
        EZASSERT2( length>=64 && length<=9000 && (length%8)==0, cmdexception,
                   EZINFO("LENGTH is not a valid packet length") );
        pvMap.insert( make_pair(LENGTH, value_type((UINT32)length, xlrreg::TENG_BYTE_LENGTH)) );
    }

    // The PSN mode
    if( !(tmp=OPTARG(PSNMODE, args)).empty() ) {
        // parse into a UINT32
        char*         eocptr;
        unsigned long ul;
        ul = ::strtoul(tmp.c_str(), &eocptr, 0);
        EZASSERT2( eocptr!=tmp.c_str() && *eocptr=='\0' && ul<3,
                   cmdexception,
                   EZINFO("Invalid value " << tmp << " for PSN mode (0,1,2 allowed)") );
        // Depending on the PSN mode, we have to write registers for PSN
        // mode 1 and 2
        UINT32  m1 = 0, m2 = 0;
        switch( ul ) {
            case 0:
                // already covered by initial values of m1, m2
                break;
            case 1:
                    m1 = 1;
                    break;
            case 2:
                    m2 = 1;
                    break;
        }
        pvMap.insert( make_pair(PSN_MODE1, value_type(m1, xlrreg::TENG_PSN_MODE1)) );
        pvMap.insert( make_pair(PSN_MODE2, value_type(m2, xlrreg::TENG_PSN_MODE2)) );
    }

    // PSN OFST
    if( !(tmp=OPTARG(PSNOFST, args)).empty() ) {
        // parse into a UINT32
        char*         eocptr;
        unsigned long ul;
        ul = ::strtoul(tmp.c_str(), &eocptr, 0);
        EZASSERT2( eocptr!=tmp.c_str() && *eocptr=='\0',
                   cmdexception,
                   EZINFO("PSNOFST '" << tmp << "' is not a number") );
        pvMap.insert( make_pair(PSNOFST, value_type((UINT32)ul, xlrreg::TENG_PSNOFST)) );
    }

    // Raw mode? Not specified => OFF
    tmp = OPTARG(RAW, args);
    if( tmp.empty() )
        tmp = "0";
    // only allow "0" or "1"
    EZASSERT2(tmp=="0" || tmp=="1", cmdexception,
              EZINFO("raw mode " << tmp << " not a valid value, only 0 or 1 allowed"));

    // All filtery bits
    UINT32       macdisable, lengthenable, pktlenenable, crcdisable, promisc, ethdisable;

    if( tmp=="0" ) {
        // no raw mode - enable byte length check.
        // mac filter disable
        // enable pkt length
        // enable crc check
        // not promiscuous
        lengthenable = 0;
        macdisable   = 1;
        pktlenenable = 1;
        crcdisable   = 0;
        promisc      = 1;
        ethdisable   = 0;
    } else {
        // raw mode: make sure filters are disabled
        lengthenable = 0;
        macdisable   = 1;
        pktlenenable = 0;
        crcdisable   = 1;
        promisc      = 1;
        ethdisable   = 1;
    }
    pvMap.insert( make_pair(MACDISABLE,   value_type(macdisable, xlrreg::TENG_DISABLE_MAC_FILTER)) );
    pvMap.insert( make_pair(LENENABLE,    value_type(lengthenable,  xlrreg::TENG_BYTE_LENGTH_CHECK_ENABLE)) );
    pvMap.insert( make_pair(PKTLENENABLE, value_type(pktlenenable, xlrreg::TENG_PACKET_LENGTH_CHECK_ENABLE)) );
    pvMap.insert( make_pair(CRCDISABLE,   value_type(crcdisable,  xlrreg::TENG_CRC_CHECK_DISABLE)) );
    pvMap.insert( make_pair(PROMISCUOUS,  value_type(promisc,  xlrreg::TENG_PROMISCUOUS)) );
    pvMap.insert( make_pair(ETHDISABLE,   value_type(ethdisable,  xlrreg::TENG_DISABLE_ETH_FILTER)) );

    // *phew* we've parsed the command line and collected all the registers
    // and values that must be written to them. Let's do it!
    for( pv_map_type::const_iterator curpv=pvMap.begin();
         curpv!=pvMap.end(); curpv++ ) 
            rte.xlrdev[ curpv->second.reg ] = curpv->second.value;

    // Good, after having written potentially new values, we compute the
    // actual packet length. To this effect we read the
    // BYTE_LENGTH (length of the recording), the DATA_PAYLOAD_OFFSET
    // and DATA_FRAME_OFFSET and add them together
    UINT32   pl = 0;
    pl += *rte.xlrdev[ xlrreg::TENG_BYTE_LENGTH ];
    pl += *rte.xlrdev[ xlrreg::TENG_DPOFST ];
    pl += *rte.xlrdev[ xlrreg::TENG_DFOFST ];

    // And write this into the ETH_PKT_LEN
    rte.xlrdev[ xlrreg::TENG_PACKET_LENGTH ] = pl;

    reply << " 0 ;";
    return reply.str();
}
