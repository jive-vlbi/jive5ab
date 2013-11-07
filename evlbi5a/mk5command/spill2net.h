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
#include <mk5command/in2netsupport.h>
#include <threadfns.h>
#include <tthreadfns.h>
#include <carrayutil.h>
#include <interchainfns.h>
#include <inttypes.h>
#include <limits.h>
#include <iostream>
#include <utility>


// spill = split-fill [generate fillpattern and split/dechannelize it]
// spid  = split-disk [read data from StreamStor and split/dechannelize it]
// spif  = split-file [read data from file and split/dechannelize it]
// splet = split-net  [read data from network and split/dechannelize it]
struct splitsettings_type {
    bool             strict;
    uint16_t         station;
    unsigned int     vdifsize;
    unsigned int     bitsperchannel;
    unsigned int     bitspersample;
    unsigned int     qdepth;
    netparms_type    netparms;
    chain::stepid    framerstep;
    tagremapper_type tagremapper;

    splitsettings_type():
        strict( false ), station( 0 ),
        vdifsize( (unsigned int)-1 ),
        bitsperchannel(0), bitspersample(0), qdepth( 32 )
    {}
};

// Replaces the sequence "{tag}" with the representation of 
// the number. If no "{tag}" found returns the string unmodified
template <typename T>
std::string replace_tag(const std::string& in, const T& tag) {
    std::string::size_type  tagptr = in.find( "{tag}" );

    // no tag? 
    if( tagptr==std::string::npos )
        return in;

    // make a modifieable copy
    std::string        lcl(in);
    std::ostringstream repr;
    repr << tag;
    return lcl.replace(tagptr, 5, repr.str());
}

template <unsigned int Mark5>
std::string spill2net_fn(bool qry, const std::vector<std::string>& args, runtime& rte ) {
    // Keep some static info and the transfers that this function services
    static const transfer_type             transfers[] = {spill2net, spid2net, spin2net, spin2file, spif2net,
                                                          spill2file, spid2file, spif2file, splet2net, splet2file};
    static per_runtime<chain::stepid>      fifostep;
    static per_runtime<splitsettings_type> settings;
    // for split-fill pattern we can (attempt to) do realtime or
    // 'as-fast-as-you-can'. Default = as fast as the system will go
    static per_runtime<bool>               realtime;

    // atm == acceptable transfer mode
    // rtm == requested transfer mode
    // ctm == current transfer mode
    std::ostringstream  reply;
    const transfer_type rtm( ::string2transfermode(args[0]) );
    const transfer_type ctm( rte.transfermode );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( !find_element(rtm, transfers) ) {
        reply << " 2 : " << args[0] << " is not supported by this implementation ;";
        return reply.str();
    }

    // Query is always possible, command only if doing nothing or if the
    // requested transfer mode == current transfer mode
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==rtm))

    // Good. See what the usr wants
    if( qry ) {
        const std::string& what( OPTARG(1, args) );
        reply << " 0 : ";
        if( what=="station" ) {
            uint16_t  sid = settings[&rte].station;
            reply << sid;
            if( sid && (sid&0x8000)==0 )
               reply << " : " << (char)(sid&0xff) << (char)((sid>>8));
        } else if( what=="strict" ) {
            reply << settings[&rte].strict;
        } else if( what=="net_protocol" ) {
            const netparms_type& np = settings[&rte].netparms;

            reply << np.get_protocol() << " : " ;
            if( np.rcvbufsize==np.sndbufsize )
                reply << np.rcvbufsize;
            else
                reply << "Rx " << np.rcvbufsize << ", Tx " << np.sndbufsize;
            reply << " : " << np.get_blocksize()
                  << " : " << np.nblock;
        } else if( what=="mtu" ) {
            reply << settings[&rte].netparms.get_mtu();
        } else if( what=="ipd" ) {
            reply << settings[&rte].netparms.interpacketdelay;
        } else if( what=="vdifsize" ) {
            reply << settings[&rte].vdifsize;
        } else if( what=="bitsperchannel" ) {
            reply << settings[&rte].bitsperchannel;
        } else if( what=="bitspersample" ) {
            reply << settings[&rte].bitspersample;
        } else if( what=="qdepth" ) {
            reply << settings[&rte].qdepth;
        } else if( what=="tagmap" ) {
            tagremapper_type::const_iterator p; 
            tagremapper_type::const_iterator start = settings[&rte].tagremapper.begin();
            tagremapper_type::const_iterator end   = settings[&rte].tagremapper.end();

            for(p=start; p!=end; p++) {
                if( p!=start )
                    reply << " : ";
                reply << p->first << "=" << p->second;
            }
        } else if( what=="realtime" && args[0].find("spill2")!=std::string::npos ) {
            bool rt = false;
            if( realtime.find(&rte)!=realtime.end() )
                rt = realtime[&rte];
            reply << (rt?"1":"0");
        } else if( what.empty()==false ) {
            reply << " : unknown query parameter '" << what << "'";
        } else {
            if( ctm!=rtm ) {
                reply << "inactive";
            } else {
                reply << "active";
            }
        }
        reply << " ;";
        return reply.str();
    }

    if( args.size()<2 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;
    // <connect>
    //
    // [spill|spid|spin|splet]2[net|file] = connect : <splitmethod> : <tagN>=<destN> : <tagM>=<destM> ....
    // spif2[net|file]         = connect : <file> : <splitmethod> : <tagN> = <destN> : ...
    //    splitmethod = which splitter to use. check splitstuff.cc for
    //                  defined splitters [may be left blank - no
    //                  splitting is done but reframing to VDIF IS done
    //                  ;-)]
    //    file  = [spif2* only] - file to read data-to-split from
    //    destN = <host|ip>[@<port>]    (for *2net)
    //             default port is 2630
    //            <filename>,[wa]  (for *2file)
    //              w = (over)write; empty file before writing
    //              a = append-to-file 
    //              no default mode
    //    tagN  = <tag> | <tag>-<tag> [, tagN ]
    //             note: tag range "<tag>-<tag>" endpoint is *inclusive*
    //    tag   = unsigned int
    if( args[1]=="connect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            chain                    c;
            std::string              curcdm;
            const std::string        splitmethod( OPTARG((fromfile(rtm)?3:2), args) );
            const std::string        filename( OPTARG(2, args) );
            const unsigned int       qdepth = settings[&rte].qdepth;
            chunkdestmap_type        cdm;

#if 0
            ASSERT2_COND(splitmethod.empty()==false, SCINFO("You must specify how to split the data"));
#endif


            // If we need to send over UDP we reframe to MTU size,
            // otherwise we can send out the split frames basically
            // unmodified.
            // The output chunk size is determined by the MTU of
            // the output networksettings *if* the transfer is TO the network 
            // (*2net) and the transfer is over UDP. Otherwise the VDIF 
            // framesize is unconstrained (*2file and *2net over TCP).
            // Take netparms from global parameters if we're NOT doing
            // splet2net - in that case take the network parameters
            // from the settings[&rte].netparms
            const netparms_type&        dstnet( rtm==splet2net ? settings[&rte].netparms : rte.netparms );
            const headersearch_type     dataformat(rte.trackformat(), rte.ntrack(),
                                                   (unsigned int)rte.trackbitrate(),
                                                   rte.vdifframesize());
            const unsigned int ochunksz = ( (tonet(rtm) && dstnet.get_protocol().find("udp")!=std::string::npos) ?
                                            dstnet.get_max_payload() :
                                            settings[&rte].vdifsize /*-1*/ );

            DEBUG(3, args[0] << ": current data format = " << std::endl << "  " << dataformat << std::endl);

            // The chunk-dest-mapping entries only start at positional
            // argument 3:
            // 0 = 'spill2net', 1='connect', 2=<splitmethod>, 3+ = <tag>=<dest>
            // 0 = 'spif2net', 1='connect', 2=<file>, 3=<splitmethod>, 4+ = <tag>=<dest>
            for(size_t i=(fromfile(rtm)?4:3); (curcdm=OPTARG(i, args)).empty()==false; i++) {
                std::vector<std::string>  parts = ::split(curcdm, '=');
                std::vector<unsigned int> tags;

                EZASSERT2( parts.size()==2 && parts[0].empty()==false && parts[1].empty()==false,
                           cmdexception,
                           EZINFO(" chunk-dest-mapping #" << (i-3) << " invalid \"" << curcdm << "\"") );

                // Parse intrange
                tags = ::parseUIntRange(parts[0]);
                for(std::vector<unsigned int>::const_iterator curtag=tags.begin();
                    curtag!=tags.end(); curtag++) {
                    EZASSERT2( cdm.insert(std::make_pair(*curtag, replace_tag(parts[1], *curtag))).second,
                               cmdexception,
                               EZINFO(" possible double tag " << *curtag
                                      << " - failed to insert into map destination " << parts[1]) );
                }
            }

            rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
            DEBUG(2, args[0] << ": constrained sizes = " << rte.sizes << std::endl);

            // Look at requested transfermode
            // to see where the heck we should get the
            // data from.
            // Don't have to have a final 'else' clause since
            // IF we do not handle the requested transfer mode
            // the chain has no 'producer' and hence the
            // addition of the next step will trigger an exception ...
            if( fromfill(rtm) ) {
                fillpatargs  fpargs(&rte);
                if( realtime.find(&rte)!=realtime.end() )
                    fpargs.realtime = realtime[&rte];
                c.add( &framepatterngenerator, qdepth, fpargs );
            } else if( fromdisk(rtm) )
                c.add( &diskreader, qdepth, diskreaderargs(&rte) );
            else if( fromio(rtm) ) {
                // set up the i/o board and streamstor 
                XLRCODE(SSHANDLE   ss = rte.xlrdev.sshandle());

                in2net_transfer<Mark5>::setup(rte);
                // now program the streamstor to record from FPDP -> PCI
                XLRCALL( ::XLRSetMode(ss, (CHANNELTYPE)SS_MODE_PASSTHRU) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_PCI) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );
                // Check. Now program the FPDP channel
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );

                // Code courtesy of Cindy Gold of Conduant Corp.
                //   Have to distinguish between old boards and 
                //   new ones (most notably the Amazon based boards)
                //   (which are used in Mark5B+ and Mark5C)
                XLRCODE(UINT     u32recvMode);
                XLRCODE(UINT     u32recvOpt);

                if( rte.xlrdev.boardGeneration()<4 ) {
                    // This is either a XF2/V100/VXF2
                    XLRCODE(u32recvMode = SS_FPDP_RECVMASTER);
                    XLRCODE(u32recvOpt  = SS_OPT_FPDPNRASSERT);
                } else {
                    // Amazon or Amazon/Express
                    XLRCODE(u32recvMode = SS_FPDPMODE_RECVM);
                    XLRCODE(u32recvOpt  = SS_DBOPT_FPDPNRASSERT);
                }
                XLRCALL( ::XLRSetDBMode(ss, u32recvMode, u32recvOpt) );
                // and start the recording
                XLRCALL( ::XLRRecord(ss, XLR_WRAP_DISABLE, 1) );
                fifostep[&rte] = c.add( &fiforeader, qdepth, fiforeaderargs(&rte) );
            } else if( fromnet(rtm) ) 
                // net2* transfers always use the global network params
                // as input configuration. For net2net style use
                // splet2net = net_protocol : <proto> : <bufsize> &cet
                // to configure output network settings
                c.register_cancel( c.add( &netreader, qdepth, &net_server, networkargs(&rte) ),
                                   &close_filedescriptor);
            else if( fromfile(rtm) ) {
                EZASSERT( filename.empty() == false, cmdexception );
                c.add( &fdreader, qdepth, &open_file, filename, &rte );
            }

            // The rest of the processing chain is media independent
            settings[&rte].framerstep = c.add( &framer<tagged<frame> >, qdepth,
                                               framerargs(dataformat, &rte, settings[&rte].strict) );

            headersearch_type*             curhdr = new headersearch_type( rte.trackformat(),
                                                                           rte.ntrack(),
                                                                           (unsigned int)rte.trackbitrate(),
                                                                           rte.vdifframesize() );

            if( splitmethod.empty()==false ) {
                // Figure out which splitters we need to do
                std::vector<std::string>                 splitters = split(splitmethod,'+');

                // the rest accept tagged frames as input and produce
                // tagged frames as output
                for(std::vector<std::string>::const_iterator cursplit=splitters.begin();
                    cursplit!=splitters.end(); cursplit++) {
                    unsigned int             n2c = -1;
                    headersearch_type*       newhdr = 0;
                    splitproperties_type     splitprops;
                    std::vector<std::string> splittersetup = split(*cursplit,'*');

                    EZASSERT2( cursplit->empty()==false, cmdexception, EZINFO("empty splitter not allowed!") );
                    EZASSERT2( splittersetup.size()==1 || (splittersetup.size()==2 && splittersetup[1].empty()==false),
                               cmdexception,
                               EZINFO("Invalid splitter '" << *cursplit << "' - use <splitter>[*<int>]") );

                    // If the splittersetup looks like a dynamic channel extractor
                    // (ie "[..] [...]") and the user did not provide an input step size
                    // we'll insert it
                    if( splittersetup[0].find('[')!=std::string::npos && splittersetup[0].find('>')==std::string::npos ) {
                        std::ostringstream  pfx;
                        pfx << curhdr->ntrack << " > ";
                        splittersetup[0] = pfx.str() + splittersetup[0];
                    }

                    // Look up the splitter
                    EZASSERT2( (splitprops = find_splitfunction(splittersetup[0])).fnptr(),
                               cmdexception,
                               EZINFO("the splitfunction '" << splittersetup[0] << "' cannot be found") );

                    if( splittersetup.size()==2 ) {
                        char*         eocptr;
                        unsigned long ul;

                        errno = 0;
                        ul    = ::strtoul(splittersetup[1].c_str(), &eocptr, 0);
                        EZASSERT2( eocptr!=splittersetup[1].c_str() && *eocptr=='\0' && ul>0 && ul<=UINT_MAX,
                                   cmdexception,
                                   EZINFO("'" << splittersetup[1] << "' is not a numbah or it's too frikkin' large (or zero)!") );
                        n2c = (unsigned int)ul;
                    }
                    splitterargs  splitargs(&rte, splitprops, *curhdr, n2c);
                    newhdr = new headersearch_type( splitargs.outputhdr );
                    delete curhdr;
                    curhdr = newhdr;
                    c.add( &coalescing_splitter, qdepth, splitargs );
                }
            } else {
                // no splitter given, then we must strip the header
                c.add( &header_stripper, qdepth, *((const headersearch_type*)curhdr) );
            }

            // Whatever came out of the splitter we reframe it to VDIF
            // By now we know what kind of output the splitterchain is
            // producing so we can tell the reframer that
            reframe_args       ra(settings[&rte].station, curhdr->trackbitrate,
                                  curhdr->payloadsize, ochunksz, settings[&rte].bitsperchannel,
                                  settings[&rte].bitspersample);

            delete curhdr;

            // install the current tagremapper
            ra.tagremapper = settings[&rte].tagremapper;

            c.add( &reframe_to_vdif, qdepth, ra);

            // Based on where the output should go, add a final stage to
            // the processing
            if( tofile(rtm) )
                c.register_cancel( c.add( &multiwriter<miniblocklist_type, fdwriterfunctor>,
                                          &multifileopener,
                                          multidestparms(&rte, cdm)), 
                                   &multicloser );
            else if( tonet(rtm) ) {
                // if we need to write to upds we silently call upon the vtpwriter in
                // stead of the networkwriter
                if( dstnet.get_protocol().find("udps")!=std::string::npos ) {
                    c.register_cancel( c.add( &multiwriter<miniblocklist_type, vtpwriterfunctor>,
                                              &multiopener,
                                              multidestparms(&rte, cdm, dstnet) ),
                                       &multicloser );
                } else {
                    c.register_cancel( c.add( &multiwriter<miniblocklist_type, netwriterfunctor>,
                                              &multiopener,
                                              multidestparms(&rte, cdm, dstnet) ),
                                       &multicloser );
                }
            }

            // reset statistics counters
            rte.statistics.clear();

            // Now we can start the chain
            rte.processingchain = c;
            DEBUG(2, args[0] << ": starting to run" << std::endl);
            rte.processingchain.run();

            if ( fromfile(rtm) ) {
                rte.processingchain.communicate(0, &fdreaderargs::set_run, true);
            }

            DEBUG(2, args[0] << ": running" << std::endl);
            rte.transfermode    = rtm;
            rte.transfersubmode.clr_all().set(connected_flag).set(wait_flag);
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    } else if( args[1]=="station" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            uint16_t          sid = 0;
            const std::string station_id( OPTARG(2, args) );
            for(unsigned int i=0; i<2 && i<station_id.size(); i++)
                sid = (uint16_t)(sid | (((uint16_t)station_id[i])<<(i*8)));
            settings[&rte].station = sid;
            reply << " 0 ;";
        } else {
            reply << " 6 : cannot change during transfer ;";
        }
    } else if( args[1]=="strict" ) {
        const std::string strictarg( OPTARG(2, args) );

        recognized = true;
        EZASSERT2( strictarg=="1" || strictarg=="0",
                   cmdexception,
                   EZINFO("use '1' to turn strict mode on, '0' for off") );
        // Save the new value in the per runtime settings
        settings[&rte].strict = (strictarg=="1");

        // IF there is a transfer running, we communicate it also
        // to the framer
        if( rte.transfermode!=no_transfer )
            rte.processingchain.communicate( settings[&rte].framerstep,
                                             &framerargs::set_strict,
                                             settings[&rte].strict );

        reply << " 0 ;";
    } else if( args[1]=="net_protocol" ) {
        std::string         proto( OPTARG(2, args) );
        const std::string   sokbufsz( OPTARG(3, args) );
        netparms_type& np = settings[&rte].netparms;

        recognized = true;

        if( proto.empty()==false )
            np.set_protocol(proto);

        if( sokbufsz.empty()==false ) {
            char*          eptr;
            long int       bufsz = ::strtol(sokbufsz.c_str(), &eptr, 0);

            // was a unit given? [note: all whitespace has already been stripped
            // by the main commandloop]
            EZASSERT2( eptr!=sokbufsz.c_str() && ::strchr("kM\0", *eptr),
                       cmdexception,
                       EZINFO("invalid socketbuffer size '" << sokbufsz << "'") );

            // Now we can do this
            bufsz *= ((*eptr=='k')?KB:(*eptr=='M'?MB:1));

            // Check if it's a sensible "int" value for size, ie >=0 and <=INT_MAX
            EZASSERT2( bufsz>=0 && bufsz<=INT_MAX,
                       cmdexception,
                       EZINFO("<socbuf size> '" << sokbufsz << "' out of range") );
            np.rcvbufsize = np.sndbufsize = (int)bufsz;
        }
        reply << " 0 ;";
    } else if( args[1]=="mtu" ) {
        char*             eocptr;
        netparms_type&    np = settings[&rte].netparms;
        const std::string mtustr( OPTARG(2, args) );
        unsigned long int mtu;

        recognized = true;
        EZASSERT2(mtustr.empty()==false, cmdexception, EZINFO("mtu needs a parameter"));

        errno = 0;
        mtu   = ::strtoul(mtustr.c_str(), &eocptr, 0);

        // Check if it's a sensible "int" value for size, ie >0 and <=INT_MAX
        EZASSERT2(eocptr!=mtustr.c_str() && *eocptr=='\0' && errno!=ERANGE &&  mtu>0 && mtu<=UINT_MAX,
                  cmdexception,
                  EZINFO("mtu '" << mtustr << "' out of range") );
        np.set_mtu( (unsigned int)mtu );
        reply << " 0 ;";
    } else if( args[1]=="ipd" ) {
        char*             eocptr;
        long int          ipd;
        const std::string ipdstr( OPTARG(2, args) );
        netparms_type& np = settings[&rte].netparms;

        recognized = true;
        EZASSERT2(ipdstr.empty()==false, cmdexception, EZINFO("ipd needs a parameter"));

        ipd = ::strtol(ipdstr.c_str(), &eocptr, 0);

        // Check if it's an acceptable "ipd" value 
        EZASSERT2(eocptr!=ipdstr.c_str() && *eocptr=='\0' && errno!=ERANGE && ipd>=-1 && ipd<=INT_MAX,
                cmdexception,
                EZINFO("ipd '" << ipdstr << "' NaN/out of range (range: [-1," << INT_MAX << "])") );
        np.interpacketdelay = (int)ipd;
        reply << " 0 ;";
    } else if( args[1]=="vdifsize" ) {
        char*             eocptr;
        const std::string vdifsizestr( OPTARG(2, args) );
        unsigned long int vdifsize;

        recognized = true;
        EZASSERT2(vdifsizestr.empty()==false, cmdexception, EZINFO("vdifsize needs a parameter"));

        errno    = 0;
        vdifsize = ::strtoul(vdifsizestr.c_str(), &eocptr, 0);
        EZASSERT2(eocptr!=vdifsizestr.c_str() && *eocptr=='\0' && errno!=ERANGE && vdifsize<=UINT_MAX,
                cmdexception,
                EZINFO("vdifsize '" << vdifsizestr << "' NaN/out of range (range: [1," << UINT_MAX << "])") );
        settings[&rte].vdifsize = (unsigned int)vdifsize;
        reply << " 0 ;";
    } else if( args[1]=="bitsperchannel" ) {
        char*             eocptr;
        const std::string bpcstr( OPTARG(2, args) );
        unsigned long int bpc;

        recognized = true;
        EZASSERT2(bpcstr.empty()==false, cmdexception, EZINFO("bitsperchannel needs a parameter"));

        errno = 0;
        bpc   = ::strtoul(bpcstr.c_str(), &eocptr, 0);
        EZASSERT2(eocptr!=bpcstr.c_str() && *eocptr=='\0' && bpc>0 && bpc<=64, cmdexception,
                EZINFO("bits per channel must be >0 and less than 65"));
        settings[&rte].bitsperchannel = (unsigned int)bpc;
        reply << " 0 ;";
    } else if( args[1]=="bitspersample" ) {
        char*             eocptr;
        const std::string bpsstr( OPTARG(2, args) );
        unsigned long int bps;

        recognized = true;
        EZASSERT2(bpsstr.empty()==false, cmdexception, EZINFO("bitspersample needs a parameter"));

        errno = 0;
        bps   = ::strtoul(bpsstr.c_str(), &eocptr, 0);

        EZASSERT2(eocptr!=bpsstr.c_str() && *eocptr=='\0' && bps>0 && bps<=32, cmdexception,
                EZINFO("bits per sample must be >0 and less than 33"));
        settings[&rte].bitspersample = (unsigned int)bps;
        reply << " 0 ;";
    } else if( args[1]=="qdepth" ) {
        char*             eocptr;
        const std::string qdstr( OPTARG(2, args) );
        unsigned long int qd;

        recognized = true;
        EZASSERT2(qdstr.empty()==false, cmdexception, EZINFO("qdepth needs a parameter"));

        errno = 0;
        qd    = ::strtoul(qdstr.c_str(), &eocptr, 0);

        // Check if it's an acceptable qdepth
        EZASSERT2( eocptr!=qdstr.c_str() && *eocptr=='\0' && errno!=ERANGE && qd>0 && qd<=UINT_MAX,
                cmdexception,
                EZINFO("qdepth '" << qdstr << "' NaN/out of range (range: [1," << UINT_MAX << "])") );
        settings[&rte].qdepth = qd;
        reply << " 0 ;";
    } else if( args[1]=="realtime" && args[0].find("spill2")!=std::string::npos ) {
        char*             eocptr;
        long int          rt;
        const std::string rtstr( OPTARG(2, args) );

        recognized = true;
        EZASSERT2(rtstr.empty()==false, cmdexception, EZINFO("realtime needs a parameter"));

        rt = ::strtol(rtstr.c_str(), &eocptr, 10);

        // Check if it's an acceptable number
        EZASSERT2( eocptr!=rtstr.c_str() && *eocptr=='\0' && errno!=ERANGE,
                cmdexception,
                EZINFO("realtime parameter must be a decimal number") );
        realtime[&rte] = (rt!=0);
        reply << " 0 ;";
    } else if( args[1]=="tagmap" ) {

        recognized = true;
        if( rte.transfermode==no_transfer ) {
            tagremapper_type  newmap;
            std::string       curentry;

            // parse the tag->datathread mappings
            for(size_t i=2; (curentry=OPTARG(i, args)).empty()==false; i++) {
                unsigned int             tag, datathread;
                std::vector<std::string> parts = ::split(curentry, '=');

                EZASSERT2( parts.size()==2 && parts[0].empty()==false && parts[1].empty()==false,
                           cmdexception,
                           EZINFO(" tag-to-threadid #" << (i-2) << " invalid \"" << curentry << "\"") );

                // Parse numbers
                tag        = (unsigned int)::strtoul(parts[0].c_str(), 0, 0);
                datathread = (unsigned int)::strtoul(parts[1].c_str(), 0, 0);

                EZASSERT2( newmap.insert(std::make_pair(tag, datathread)).second,
                           cmdexception,
                           EZINFO(" possible double tag " << tag
                                  << " - failed to insert into map datathread " << parts[1]) );
            }
            settings[&rte].tagremapper = newmap;
            reply << " 0 ;";
        } else {
            reply << " 6 : Cannot change during transfers ;";
        }
    } else if( args[1]=="on" ) {
        recognized = true;
        // First: check if we're doing spill2[net|file]
        if( rte.transfermode==spill2net || rte.transfermode==spill2file ) {
            if( ((rte.transfersubmode&run_flag)==false) ) {
                uint64_t           nword = 100000;
                const std::string  nwstr( OPTARG(2, args) );
                const std::string  start_s( OPTARG(3, args) );
                const std::string  inc_s( OPTARG(4, args) );

                if( !nwstr.empty() ) {
                    ASSERT2_COND( ::sscanf(nwstr.c_str(), "%" SCNu64, &nword)==1,
                                  SCINFO("value for nwords is out of range") );
                }

                if( start_s.empty()==false ) {
                    char*     eocptr;
                    uint64_t  fill;

                    errno = 0;
                    fill  = ::strtoull(start_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fill==0 && eocptr==start_s.c_str()) && !(fill==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'start' value") );

                    rte.processingchain.communicate(0, &fillpatargs::set_fill, fill);
                }
                if( inc_s.empty()==false ) {
                    char*     eocptr;
                    uint64_t  inc;

                    errno = 0;
                    inc   = ::strtoull(inc_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(inc==0 && eocptr==inc_s.c_str()) && !(inc==~((uint64_t)0) && errno==ERANGE),
                                  SCINFO("Failed to parse 'inc' value") );
                    rte.processingchain.communicate(0, &fillpatargs::set_inc, inc);
                }

                // turn on the dataflow
                rte.processingchain.communicate(0, &fillpatargs::set_nword, nword);
                rte.processingchain.communicate(0, &fillpatargs::set_run,   true);
                recognized = true;
                rte.transfersubmode.clr(wait_flag).set(run_flag);
                reply << " 0 ;";
            } else {
                reply << " 6 : already running ;";
            }
            // Maybe we're doing spid (disk) to [net|file]?
        } else if( rte.transfermode==spid2net || rte.transfermode==spid2net ) {
            if( ((rte.transfersubmode&run_flag)==false) ) {
                bool               repeat = false;
                uint64_t           nbyte;
                playpointer        pp_s;
                playpointer        pp_e;
                const std::string  startbyte_s( OPTARG(2, args) );
                const std::string  endbyte_s( OPTARG(3, args) );
                const std::string  repeat_s( OPTARG(4, args) );

                // Pick up optional extra arguments:
                // note: we do not support "scan_set" yet so
                //       the part in the doc where it sais
                //       that, when omitted, they refer to
                //       current scan start/end.. that no werk

                // start-byte #
                if( !startbyte_s.empty() ) {
                    uint64_t v;

                    ASSERT2_COND( ::sscanf(startbyte_s.c_str(), "%" SCNu64, &v)==1,
                                  SCINFO("start-byte# is out-of-range") );
                    pp_s.Addr = v;
                }
                // end-byte #
                // if prefixed by "+" this means: "end = start + <this value>"
                // rather than "end = <this value>"
                if( !endbyte_s.empty() ) {
                    uint64_t v;

                    ASSERT2_COND( ::sscanf(endbyte_s.c_str(), "%" SCNu64, &v)==1,
                                  SCINFO("end-byte# is out-of-range") );
                    if( endbyte_s[0]=='+' )
                        pp_e.Addr = pp_s.Addr + v;
                    else
                        pp_e.Addr = v;
                    ASSERT2_COND(pp_e>pp_s, SCINFO("end-byte-number should be > start-byte-number"));
                }
                // repeat
                if( !repeat_s.empty() ) {
                    long int    v = ::strtol(repeat_s.c_str(), 0, 0);

                    if( (v==LONG_MIN || v==LONG_MAX) && errno==ERANGE )
                        throw xlrexception("value for repeat is out-of-range");
                    repeat = (v!=0);
                }
                // now compute "real" start and end, if any
                // so the threads, when kicked off, don't have to
                // think but can just *go*!
                if( pp_e.Addr<=pp_s.Addr ) {
                    S_DIR       currec;
                    playpointer curlength;

                    ::memset(&currec, 0, sizeof(S_DIR));
                    // end <= start => either end not specified or
                    // neither start,end specified. Find length of recording
                    // and play *that*, starting at startbyte#
                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &currec) );
                    curlength = currec.Length;

                    // check validity of start,end
                    if( pp_s>=curlength ||  pp_e>=curlength ) {
                        std::ostringstream  err;
                        err << "start and/or end byte# out-of-range, curlength=" << curlength;
                        throw xlrexception( err.str() );
                    }
                    // if no end given: set it to the end of the current recording
                    if( pp_e==playpointer(0) )
                        pp_e = curlength;
                }
                // make sure the amount to play is an integral multiple of
                // blocksize
                nbyte = pp_e.Addr - pp_s.Addr;
                DEBUG(1, "start/end [nbyte]=" <<
                      pp_s << "/" << pp_e << " [" << nbyte << "] " <<
                      "repeat:" << repeat << std::endl);
                nbyte = nbyte/rte.netparms.get_blocksize() * rte.netparms.get_blocksize();
                if( nbyte<rte.netparms.get_blocksize() )
                    throw xlrexception("less than <blocksize> bytes selected to play. no can do");
                pp_e = pp_s.Addr + nbyte;
                DEBUG(1, "Made it: start/end [nbyte]=" <<
                      pp_s << "/" << pp_e << " [" << nbyte << "] " <<
                      "repeat:" << repeat << std::endl);

                // Now communicate all to the appropriate step in the chain.
                // We know the diskreader step is always the first step ..
                // make sure we do the "run -> true" as last one, as that's the condition
                // that will make the diskreader go
                rte.processingchain.communicate(0, &diskreaderargs::set_start,  pp_s);
                rte.processingchain.communicate(0, &diskreaderargs::set_end,    pp_e);
                rte.processingchain.communicate(0, &diskreaderargs::set_repeat, repeat);
                rte.processingchain.communicate(0, &diskreaderargs::set_run,    true);
                reply << " 0 ;";
            } else {
                reply << " 6 : already running ;";
            }
        } else if( rte.transfermode==spin2net || rte.transfermode==spin2file ) {
            // only allow if we're in a connected state
            if( rte.transfermode&connected_flag ) {
                // initial state = connected, wait
                // other acceptable states are: running, pause
                // or: running
                if( rte.transfersubmode&wait_flag ) {
                    // first time here - kick the fiforeader into action
                    in2net_transfer<Mark5>::start(rte);
                    rte.processingchain.communicate(fifostep[&rte], &fiforeaderargs::set_run, true);
                    // change from WAIT->RUN,PAUSE (so below we can go
                    // from "RUN, PAUSE" -> "RUN"
                    rte.transfersubmode.clr( wait_flag ).set( run_flag ).set( pause_flag );
                }

                // ok, deal with pause / unpause
                if( rte.transfersubmode&run_flag && rte.transfersubmode&pause_flag ) {
                    // resume the hardware
                    in2net_transfer<Mark5>::resume(rte);
                    rte.transfersubmode.clr( pause_flag );
                    reply << " 0 ;";
                } else {
                    reply << " 6 : already on or not running;";
                }
            } else {
                reply << " 6 : not connected anymore ;";
            }
        } else {
            // "=on" doesn't apply yet!
            reply << " 6 : not doing " << args[0] << " ;";
        }
    } else if( args[1]=="off" ) {
        // Only valid for spin2[net|file]; pause the hardware
        if( rte.transfermode==spin2net || rte.transfermode==spin2file ) {
            recognized = true;
            // only acceptable if we're actually running and not yet
            // paused
            if( rte.transfersubmode&run_flag && !(rte.transfersubmode&pause_flag)) {
                in2net_transfer<Mark5>::pause(rte);
                rte.transfersubmode.set( pause_flag );
            } else {
                reply << " 6 : not running or already paused ;";
            }
        } else if( rte.transfermode==no_transfer ) {
            recognized = true;
            reply << " 6 : not doing " << args[0] << " ;";
        }
    } else if( args[1]=="disconnect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            reply << " 6 : not doing " << args[0] << " ;";
        } else {
            std::string error_message;
            DEBUG(2, "Stopping " << rte.transfermode << "..." << std::endl);

            if( rte.transfermode==spin2net || rte.transfermode==spin2file ) {
                try {
                    // tell hardware to stop sending
                    in2net_transfer<Mark5>::stop(rte);
                }
                catch ( std::exception& e ) {
                    error_message += std::string(" : Failed to stop I/O board: ") + e.what();
                }
                catch ( ... ) {
                    error_message += std::string(" : Failed to stop I/O board, unknown exception");
                }
                
                try {
                    // And stop the recording on the Streamstor. Must be
                    // done twice if we are running, according to the
                    // manual. I think.
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                    if( rte.transfersubmode&run_flag )
                        XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                }
                catch ( std::exception& e ) {
                    error_message += std::string(" : Failed to stop streamstor: ") + e.what();
                }
                catch ( ... ) {
                    error_message += std::string(" : Failed to stop streamstor, unknown exception");
                }
            }

            try {
                rte.processingchain.stop();
                DEBUG(2, rte.transfermode << " disconnected" << std::endl);
                rte.processingchain = chain();
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();

            if ( error_message.empty() ) {
                reply << " 0 ;";
            }
            else {
                reply << " 4" << error_message << " ;";
            }
            
            recognized = true;
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

