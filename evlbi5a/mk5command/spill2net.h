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
#include <sys/stat.h>


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

// We have disk reader, file reader and fifo reader
struct reader_info_type {
    chain::stepid   readstep;
    diskreaderargs  disk;
    uint64_t        nbyte;

    reader_info_type(runtime* rteptr):
        disk(rteptr)
    {}
};

// Change settings in object returned by open_file(), according
// to desired props for _this_ function, mainly, we want to hold
// up reading until user really starts the transfer; thus we
// want '.run == false'
fdreaderargs* open_file_wrap(std::string filename, runtime* r);

typedef per_runtime<reader_info_type> reader_info_store_type;


#define NOTWHILSTTRANSFER   \
    EZASSERT2(rte.transfermode==no_transfer, cmdexception, EZINFO("Cannot change during transfers"))



// Finalizer function - takes care of stopping hardware & clearing 
// the transfer mode. Triggered on chain exceptional shutdown as well
// as on normal shutdown.
template <unsigned int Mark5>
void finalize_split(runtime* rteptr) {
    DEBUG(2, "finalize_split/" << rteptr->transfermode << std::endl);

    if( fromio(rteptr->transfermode) ) {
        try {
            // tell hardware to stop sending
            in2net_transfer<Mark5>::stop(*rteptr);
        }
        catch ( std::exception& e ) {
            DEBUG(-1, "finalize_split: failed to stop I/O board: " << e.what() << std::endl);
        }
        catch ( ... ) {
            DEBUG(-1, "finalize_split: failed to stop I/O board: unknown exception" << std::endl);
        }

        try {
            // And stop the recording on the Streamstor. Must be
            // done twice if we are running, according to the
            // manual. I think.
            XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
            if( rteptr->transfersubmode&run_flag )
                XLRCALL( ::XLRStop(rteptr->xlrdev.sshandle()) );
        }
        catch ( std::exception& e ) {
            DEBUG(-1, "finalize_split: failed to stop streamstor: " << e.what() << std::endl);
        }
        catch ( ... ) {
            DEBUG(-1, "finalize_split: failed to stop streamstor: unknown exception" << std::endl);
        }
    }

    rteptr->transfermode = no_transfer;
    rteptr->transfersubmode.clr_all();
    DEBUG(2, "finalize_split/" << rteptr->transfermode << "/done" << std::endl);
}




template <unsigned int Mark5>
std::string spill2net_fn(bool qry, const std::vector<std::string>& args, runtime& rte ) {
    // Keep some static info and the transfers that this function services
    static const transfer_type               transfers[] = {spill2net, spid2net, spin2net, spin2file, spif2net,
                                                            spill2file, spid2file, spif2file, splet2net, splet2file};
    static reader_info_store_type            reader_info_store;
    static per_runtime<splitsettings_type>   settings;
    // for split-fill pattern we can (attempt to) do realtime or
    // 'as-fast-as-you-can'. Default = as fast as the system will go
    static per_runtime<bool>                 realtime;
    static per_runtime<struct timespec>      timedelta;
    static per_runtime<framefilterargs_type> ffargs;

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

    // Make absolutely sure that an entry for the current runtime
    // exists in reader_info
    reader_info_store_type::iterator  riptr = reader_info_store.find(&rte);
    if( riptr==reader_info_store.end() )
        riptr = reader_info_store.insert( std::make_pair(&rte, reader_info_type(&rte)) ).first;
    reader_info_type&   reader_info( riptr->second );

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
            int  ns = settings[&rte].netparms.interpacketdelay_ns;
            if( ns % 1000 )
                reply << float(ns)/1000.0;
            else
                reply << ns/1000;
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
        } else if( what=="timeoffset" ) {
            per_runtime<struct timespec>::iterator ptr = timedelta.find( &rte );

            if( ptr==timedelta.end() )
                reply << "none";
            else
                reply << ptr->second.tv_sec << " : " << ptr->second.tv_nsec << " ;";
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

                const uint64_t current = rte.statistics.counter(reader_info.readstep);
                if( fromfile(ctm) ) {
                    const uint64_t  start = rte.processingchain.communicate(reader_info.readstep, &fdreaderargs::get_start);
                    const uint64_t  end   = rte.processingchain.communicate(reader_info.readstep, &fdreaderargs::get_end);
                    reply << " : " << start
                          << " : " << current + start
                          << " : " << end;
                } else if( fromdisk(ctm) ) {
                    reply << " : " << reader_info.disk.pp_start.Addr
                          << " : " << current + reader_info.disk.pp_start.Addr
                          << " : " << reader_info.disk.pp_end.Addr;
                } else if( fromio(ctm) || fromfill(ctm) ) {
                    reply << " : 0 " 
                          << " : " << current
                          << " : " << reader_info.nbyte;
                }
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
    //            <filename>,[nwa]  (for *2file)
    //              w = (over)write; empty file before writing
    //              a = append-to-file 
    //              n = new, may not exist yet
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
                                                   rte.trackbitrate(),
                                                   rte.vdifframesize());

            // Depending on settings, choose vdif output size computing method
            // Start with default setup
            const bool         udp_out  = (tonet(rtm) && dstnet.get_protocol().find("udp")!=std::string::npos);
            vdif_computer      computer = &size_is_request;
            unsigned int       requested_vdif_size = settings[&rte].vdifsize;

            if( udp_out && (requested_vdif_size==(unsigned int)-1) ) {
                computer            = &size_is_hint;
                requested_vdif_size = dstnet.get_max_payload();
            }

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

            // Check for sane net_protocol
            throw_on_insane_netprotocol(rte);

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
                reader_info.readstep = c.add( &framepatterngenerator, qdepth, fpargs );
            } else if( fromdisk(rtm) ) {
                reader_info.readstep = c.add( &diskreader, qdepth, diskreaderargs(&rte) );
            } else if( fromio(rtm) ) {
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
                reader_info.readstep = c.add( &fiforeader, qdepth, fiforeaderargs(&rte) );
            } else if( fromnet(rtm) ) {
                // net2* transfers always use the global network params
                // as input configuration. For net2net style use
                // splet2net = net_protocol : <proto> : <bufsize> &cet
                // to configure output network settings
                reader_info.readstep = c.add( &netreader, qdepth, &net_server, networkargs(&rte) );
                c.register_cancel(reader_info.readstep, &close_filedescriptor);
            } else if( fromfile(rtm) ) {
                EZASSERT( filename.empty() == false, cmdexception );

                reader_info.readstep = c.add( &fdreader, qdepth, &open_file_wrap, filename, &rte );
                c.register_cancel(reader_info.readstep, &close_filedescriptor);
            }

            // The rest of the processing chain is media independent
            settings[&rte].framerstep = c.add( &framer<tagged<frame> >, qdepth,
                                               framerargs(dataformat, &rte, settings[&rte].strict) );

            // Do we need to twiddle with the time stamp?
            per_runtime<struct timespec>::iterator timeoffptr = timedelta.find( &rte );
            if( timeoffptr!=timedelta.end() )
                c.add(&timemanipulator, 3, timemanipulator_type(timeoffptr->second));

            // Run all frame integer time stamps through a median filter
            c.add( &medianfilter, 4 );

            // This will always describe the _output_ header i.e. AFTER
            // (potentially) splitting.
            headersearch_type*             curhdr = new headersearch_type( rte.trackformat(),
                                                                           rte.ntrack(),
                                                                           rte.trackbitrate(),
                                                                           rte.vdifframesize() );
            // (Potentially) Add frame filter which filters n frames for
            // the first splitter step, subject to the condition that
            // the time stamp of the first frame out of these n has a
            // time stamp which is an integer multiple of the output
            // VDIF frame length.
            // default framefilterargs => can detect this for no filtering step to be added
            framefilterargs_type&          framefilterargs( ffargs[&rte] );

            // reset framefilterargs to default [they're now kept static]
            framefilterargs = framefilterargs_type();

            if( splitmethod.empty()==false ) {
                // Figure out which splitters we need to do
                std::vector<std::string>                 splitters = split(splitmethod,'+');

                // We are likely to have a framefilter necessary: if the
                // number of accumulated frames > 1 but not if the input
                // frame duration is an integer amount of output vdif frame
                // lengths!
                framefilterargs.naccumulate = 1;

                // Add the framefilterstep. Its userdata will point at the
                // framefilterargs that *we* keep statically in here. This
                // means that the thread can read the values we will
                // prepare later on in this method. Otherwise we would have
                // to communicate() with the thread after the chain has been
                // started; at _this_ point in time, we don't have all the
                // information the framefilterthread needs yet ...
                c.add( &framefilter, 4, &framefilterargs );

                // The following steps accept tagged frames as input and produce
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

                    framefilterargs.naccumulate *= splitargs.naccumulate;
                }
            } else {
                // no splitter given, then we must strip the header
                c.add( &header_stripper, qdepth, *((const headersearch_type*)curhdr) );
            }

            // Whatever came out of the splitter we reframe it to VDIF
            // By now we know what kind of output the splitterchain is
            // producing so we can compute the output vdif frame size and
            // tell the reframer that
            const unsigned int     output_vdif_size = computer(requested_vdif_size, curhdr->payloadsize);

            // In case of UDP output, verify that the ochunksz is not > max payload - 16 
            // 1. We output legacy VDIF, so 16 bytes of the payload goes to VDIF header
            // 2. Note that if protcol == udps (i.e. with 64-bit sequence number prepended)
            //    that has already been accounted for in the .get_max_payload()
            EZASSERT2( !udp_out || (udp_out && output_vdif_size<=dstnet.get_max_payload()-16), cmdexception,
                       EZINFO("User set output VDIF frame size " << output_vdif_size << " too big for MTU = " << dstnet.get_mtu() <<
                              " (max payload = " << dstnet.get_max_payload() << ")") );

            reframe_args           ra(settings[&rte].station, curhdr->trackbitrate,
                                      curhdr->payloadsize, output_vdif_size, settings[&rte].bitsperchannel,
                                      settings[&rte].bitspersample);

            // vdif frame rate (and thus length computation):
            // take #-of-tracks + bitrate from the last header. Note that
            // we're not at all interested in the frame rate here, just the
            // frame length. This is such that the filter can start
            // accumulating input frames at multiples of the output frame length)
            framefilterargs.framelength = (8 * output_vdif_size) / (curhdr->ntrack * curhdr->trackbitrate);

            DEBUG(3, rtm << ": output vdif frame length = " << framefilterargs.framelength << std::endl);

            // Now we do not need curhdr anymore
            delete curhdr; curhdr = 0;

            // install the current tagremapper and add the reframe-to-vdif
            // step
            ra.tagremapper = settings[&rte].tagremapper;

            c.add( &reframe_to_vdif, qdepth, ra);

            // Based on where the output should go, add a final stage to
            // the processing
            if( tofile(rtm) ) {
                c.register_cancel( c.add( &multiwriter<miniblocklist_type, fdwriterfunctor>,
                                          &multifileopener,
                                          multidestparms(&rte, cdm)), 
                                   &multicloser );
            } else if( tonet(rtm) ) {
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
            } else {
                EZASSERT2(false, cmdexception, EZINFO(rtm << ": is not 'tonet()' nor 'tofile()'?!!"));
            }

            // Register a finalization function which stops hardware and
            // resets the transfer mode
            c.register_final(&finalize_split<Mark5>, &rte);

            // reset statistics counters
            rte.statistics.clear();

            // Now we can start the chain
            rte.processingchain = c;
            DEBUG(2, args[0] << ": starting to run" << std::endl);
            rte.processingchain.run();

            DEBUG(2, args[0] << ": running" << std::endl);
            rte.transfermode    = rtm;
            rte.transfersubmode.clr_all().set(connected_flag).set(wait_flag);
            reply << " 0 ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    //
    //   Set VDIF station name
    //
    } else if( args[1]=="station" ) {
        uint16_t          sid = 0;
        const std::string station_id( OPTARG(2, args) );

        NOTWHILSTTRANSFER;

        recognized = true;
        for(unsigned int i=0; i<2 && i<station_id.size(); i++)
            sid = (uint16_t)(sid | (((uint16_t)station_id[i])<<(i*8)));
        settings[&rte].station = sid;
        reply << " 0 ;";
    //
    //   Set strictness level for the frame searcher
    //
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
    //
    //   Set output net protocol for splet-to-net
    //
    } else if( args[1]=="net_protocol" ) {
        std::string         proto( OPTARG(2, args) );
        const std::string   sokbufsz( OPTARG(3, args) );
        netparms_type& np = settings[&rte].netparms;

        NOTWHILSTTRANSFER;

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
    //
    //  Set the MTU for the output network parameters for splet2net
    //
    } else if( args[1]=="mtu" ) {
        char*             eocptr;
        netparms_type&    np = settings[&rte].netparms;
        const std::string mtustr( OPTARG(2, args) );
        unsigned long int mtu;

        NOTWHILSTTRANSFER;

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
    //
    //  Set IPD value for output network parameters for splet2net
    //
    } else if( args[1]=="ipd" ) {
        char*             eocptr;
        long int          ipd;
        const std::string ipdstr( OPTARG(2, args) );
        netparms_type& np = settings[&rte].netparms;

        NOTWHILSTTRANSFER;

        recognized = true;

        EZASSERT2(ipdstr.empty()==false, cmdexception, EZINFO("ipd needs a parameter"));

        ipd = ::strtol(ipdstr.c_str(), &eocptr, 0);

        // Check if it's an acceptable "ipd" value 
        // the end-of-string character may be '\0' or 'n' (for nano-seconds)
        // or 'u' for micro seconds (==default)
        EZASSERT2(eocptr!=ipdstr.c_str() && ::strchr("nu\0", *eocptr) && errno!=ERANGE && ipd>=-1 && ipd<=INT_MAX,
                cmdexception,
                EZINFO("ipd '" << ipdstr << "' NaN/out of range (range: [-1," << INT_MAX << "])") );
        np.interpacketdelay_ns = (int)ipd;
        // not specified in ns? then assume us (or it was explicit us)
        if( *eocptr!='n' )
            np.interpacketdelay_ns *= 1000;
        reply << " 0 ;";
    //
    // Set the output VDIF frame size. 
    //
    } else if( args[1]=="vdifsize" ) {
        char*             eocptr;
        long int          vdifsize;
        const std::string vdifsizestr( OPTARG(2, args) );

        NOTWHILSTTRANSFER;

        recognized = true;

        EZASSERT2(vdifsizestr.empty()==false, cmdexception, EZINFO("vdifsize needs a parameter"));

        errno    = 0;
        vdifsize = ::strtoll(vdifsizestr.c_str(), &eocptr, 0);
        EZASSERT2(eocptr!=vdifsizestr.c_str() && *eocptr=='\0' && errno!=ERANGE && vdifsize>=-1 && vdifsize!=0 && vdifsize<=(long int)UINT_MAX,
                cmdexception,
                EZINFO("vdifsize '" << vdifsizestr << "' NaN/out of range (range: [-1," << UINT_MAX << "] with 0 excluded)") );
        settings[&rte].vdifsize = (unsigned int)vdifsize;
        reply << " 0 ;";
    //
    //  bitsperchannel parameter for the cornerturning
    //
    } else if( args[1]=="bitsperchannel" ) {
        char*             eocptr;
        const std::string bpcstr( OPTARG(2, args) );
        unsigned long int bpc;

        NOTWHILSTTRANSFER;

        recognized = true;
        EZASSERT2(bpcstr.empty()==false, cmdexception, EZINFO("bitsperchannel needs a parameter"));

        errno = 0;
        bpc   = ::strtoul(bpcstr.c_str(), &eocptr, 0);
        EZASSERT2(eocptr!=bpcstr.c_str() && *eocptr=='\0' && bpc>0 && bpc<=64, cmdexception,
                EZINFO("bits per channel must be >0 and less than 65"));
        settings[&rte].bitsperchannel = (unsigned int)bpc;
        reply << " 0 ;";
    //
    //  bitspersample parameter for the cornerturning
    //
    } else if( args[1]=="bitspersample" ) {
        char*             eocptr;
        const std::string bpsstr( OPTARG(2, args) );
        unsigned long int bps;

        NOTWHILSTTRANSFER;

        recognized = true;

        EZASSERT2(bpsstr.empty()==false, cmdexception, EZINFO("bitspersample needs a parameter"));

        errno = 0;
        bps   = ::strtoul(bpsstr.c_str(), &eocptr, 0);

        EZASSERT2(eocptr!=bpsstr.c_str() && *eocptr=='\0' && bps>0 && bps<=32, cmdexception,
                EZINFO("bits per sample must be >0 and less than 33"));
        settings[&rte].bitspersample = (unsigned int)bps;
        reply << " 0 ;";
    //
    // Experimental: play with the depth of the queue 
    //
    } else if( args[1]=="qdepth" ) {
        char*             eocptr;
        const std::string qdstr( OPTARG(2, args) );
        unsigned long int qd;

        NOTWHILSTTRANSFER;

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
    //
    // "spill2*" can be made to go as fast as it can or
    // sort of realtime
    //
    } else if( args[1]=="realtime" && fromfill(rtm) ) {
        char*             eocptr;
        long int          rt;
        const std::string rtstr( OPTARG(2, args) );

        NOTWHILSTTRANSFER;

        recognized = true;
        EZASSERT2(rtstr.empty()==false, cmdexception, EZINFO("realtime needs a parameter"));

        rt = ::strtol(rtstr.c_str(), &eocptr, 10);

        // Check if it's an acceptable number
        EZASSERT2( eocptr!=rtstr.c_str() && *eocptr=='\0' && errno!=ERANGE,
                cmdexception,
                EZINFO("realtime parameter must be a decimal number") );
        realtime[&rte] = (rt!=0);
        reply << " 0 ;";
    //
    // It is possible to modify the VDIF time stamps by
    // a constant amount 
    //
    } else if( args[1]=="timeoffset" ) {
        char*             eocptr;
        long int          tv_sec = 0, tv_nsec = 0;
        const std::string secstr( OPTARG(2, args) );
        const std::string nsecstr( OPTARG(3, args) );

        NOTWHILSTTRANSFER;

        recognized = true;

        if( !secstr.empty() ) {
            errno   = 0;
            tv_sec  = ::strtol(secstr.c_str(), &eocptr, 0);
            EZASSERT2(eocptr!=secstr.c_str() && *eocptr=='\0', cmdexception,
                      EZINFO("integer second offset '" << secstr << "' is not a valid number"));
        }
        if( !nsecstr.empty() ) {
            errno   = 0;
            tv_nsec = ::strtol(nsecstr.c_str(), &eocptr, 0);
            EZASSERT2(eocptr!=nsecstr.c_str() && *eocptr=='\0', cmdexception,
                      EZINFO("nanosecond offset '" << nsecstr << "' is not a valid number"));
        }

        if( tv_sec==0 && tv_nsec==0 ) {
            // remove the current offset
            per_runtime<struct timespec>::iterator  ptr = timedelta.find( &rte );
            if( ptr!=timedelta.end() )
                timedelta.erase( ptr );
        } else {
            timedelta[&rte].tv_sec  = tv_sec;
            timedelta[&rte].tv_nsec = tv_nsec;
        }
        reply << " 0 ;";
    //
    // The produced VDIF threads can be remapped to 
    // different thread IDs, if that's deemed useful
    //
    } else if( args[1]=="tagmap" ) {
        tagremapper_type  newmap;
        std::string       curentry;

        NOTWHILSTTRANSFER;

        recognized = true;


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
    //
    // spill2* = on [ : nword [ : [start] [ : [inc]]]]  (defaults 100000 0x11223344 0)
    // spid2*  = on [ : start [ : [+]end] ]   (defaults taken from scan_set params)
    // spif2*  = on [ : start [ : [+]end] ]   (defaults to whole file)
    // spin2*  = on [ : nbyte ]               (default 2**63 - 1)
    //
    } else if( args[1]=="on" ) {
        recognized = true;
        //
        // Are we generating fill pattern? [spill2*]
        // 
        if( fromfill(ctm) ) {
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

                // Store the number of bytes in the reader_info
                reader_info.nbyte = nword * sizeof(uint64_t);

                // turn on the dataflow
                rte.processingchain.communicate(0, &fillpatargs::set_nword, nword);
                rte.processingchain.communicate(0, &fillpatargs::set_run,   true);
                recognized = true;
                rte.transfersubmode.clr(wait_flag).set(run_flag);
                reply << " 0 ;";
            } else {
                reply << " 6 : already running ;";
            }
        // 
        // Maybe we're reading from disk?  [spid2*]
        //
        } else if( fromdisk(ctm) ) {
            if( ((rte.transfersubmode&run_flag)==false) ) {
                bool               repeat = false;
                uint64_t           nbyte;
                playpointer        pp_s = rte.pp_current.Addr;
                playpointer        pp_e = rte.pp_end.Addr;
                const std::string  startbyte_s( OPTARG(2, args) );
                const std::string  endbyte_s( OPTARG(3, args) );
                const std::string  repeat_s( OPTARG(4, args) );

                // start-byte #
                // Update: make sure it's just a number
                if( !startbyte_s.empty() ) {
                    char*      eocptr;
                    int64_t    v;
                    const bool plus( startbyte_s[0]=='+' );

                    errno = 0;
                    v     = ::strtoll(startbyte_s.c_str(), &eocptr, 0);
                    ASSERT2_COND( *eocptr=='\0' && errno==0 && eocptr!=startbyte_s.c_str() && errno==0 && v>=0, 
                                  SCINFO(" invalid start byte number " << startbyte_s));

                    if( plus )
                        pp_s.Addr += v;
                    else
                        pp_s.Addr = v;
                }
                // end-byte #
                // if prefixed by "+" this means: "end = start + <this value>"
                // rather than "end = <this value>"
                // 13-Mar-2017 allow for explicitly setting "end = 0" as a sentinel
                //             for "end-of-diskpack"
                if( !endbyte_s.empty() ) {
                    uint64_t v;

                    ASSERT2_COND( ::sscanf(endbyte_s.c_str(), "%" SCNu64, &v)==1,
                                  SCINFO("end-byte# is out-of-range") );
                    if( endbyte_s[0]=='+' )
                        pp_e.Addr = pp_s.Addr + v;
                    else
                        pp_e.Addr = v;
                    // end byte < start byte?!
                    EZASSERT2(pp_e==playpointer(0) || pp_e>pp_s, cmdexception,
                              EZINFO("end-byte-number should be either zero or > start-byte-number"));
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

                // Store the actual byte numbers in the reader_info
                reader_info.disk.pp_start = pp_s;
                reader_info.disk.pp_end   = pp_e;

                // Now communicate all to the appropriate step in the chain.
                // We know the diskreader step is always the first step ..
                // make sure we do the "run -> true" as last one, as that's the condition
                // that will make the diskreader go
                rte.processingchain.communicate(reader_info.readstep, &diskreaderargs::set_start,  pp_s);
                rte.processingchain.communicate(reader_info.readstep, &diskreaderargs::set_end,    pp_e);
                rte.processingchain.communicate(reader_info.readstep, &diskreaderargs::set_repeat, repeat);
                rte.processingchain.communicate(reader_info.readstep, &diskreaderargs::set_run,    true);
                reply << " 0 ;";
            } else {
                reply << " 6 : already running ;";
            }
        //
        //  Maybe reading from file then?  [spif2*]
        //
        } else if( fromfile(ctm) ) {
            if( ((rte.transfersubmode&run_flag)==false) ) {
                off_t              start = 0, end = 0;
                const std::string  startbyte_s( OPTARG(2, args) );
                const std::string  endbyte_s( OPTARG(3, args) );

                // start-byte #
                if( !startbyte_s.empty() ) {
                    uint64_t v;

                    ASSERT2_COND( ::sscanf(startbyte_s.c_str(), "%" SCNu64, &v)==1,
                                  SCINFO("start-byte# is out-of-range") );
                    start = v;
                    rte.processingchain.communicate(reader_info.readstep, &fdreaderargs::set_start,  start);
                }
                // end-byte #
                // if prefixed by "+" this means: "end = start + <this value>"
                // rather than "end = <this value>"
                if( !endbyte_s.empty() ) {
                    uint64_t v;

                    ASSERT2_COND( ::sscanf(endbyte_s.c_str(), "%" SCNu64, &v)==1,
                                  SCINFO("end-byte# is out-of-range") );
                    end = v;
                    if( endbyte_s[0]=='+' )
                        end += start;
                    // end byte < start byte?!
                    EZASSERT2(end>start, cmdexception, EZINFO("end-byte-number should be > start-byte-number"));
                    rte.processingchain.communicate(reader_info.readstep, &fdreaderargs::set_end,    end);
                }

                // Now communicate all to the appropriate step in the chain.
                // We know the diskreader step is always the first step ..
                // make sure we do the "run -> true" as last one, as that's the condition
                // that will make the diskreader go

                // *NOW* we can go on running!
                rte.processingchain.communicate(reader_info.readstep, &fdreaderargs::set_run,    true);
                reply << " 0 ;";
            } else {
                reply << " 6 : already running ;";
            }
        //
        // Are we reading from the I/O board?  [spin2*]
        //
        } else if( rte.transfermode==spin2net || rte.transfermode==spin2file ) {
            // only allow if we're in a connected state
            if( rte.transfermode&connected_flag ) {
                // initial state = connected, wait
                // other acceptable states are: running, pause
                // or: running
                if( rte.transfersubmode&wait_flag ) {
                    // first time here - kick the fiforeader into action
                    //                   this is the only time we accept
                    //                   an optional argument
                    uint64_t           nbyte = std::numeric_limits<uint64_t>::max();
                    const std::string  nbstr( OPTARG(2, args) );

                    if( !nbstr.empty() ) {
                        EZASSERT2( ::sscanf(nbstr.c_str(), "%" SCNu64, &nbyte)==1, cmdexception,
                                   EZINFO("value for nbytes is out of range") );
                    }
                    // Store the falue in the reader_info
                    reader_info.nbyte = nbyte;

                    in2net_transfer<Mark5>::start(rte);
                    rte.processingchain.communicate(reader_info.readstep, &fiforeaderargs::set_run, true);
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
    //
    // Pause current transfer, only valid for [spin2*]
    //
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
    //
    // Disconnect the whole lot
    //
    } else if( args[1]=="disconnect" ) {
        recognized = true;
        if( rte.transfermode==no_transfer ) {
            reply << " 6 : not doing " << args[0] << " ;";
        } else {
            try {
                rte.processingchain.stop();
                DEBUG(2, rte.transfermode << " disconnected" << std::endl);
                rte.processingchain = chain();
                reply << " 0 ;";
            }
            catch ( std::exception& e ) {
                reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
            }
            catch ( ... ) {
                reply << " 4 : Failed to stop processing chain, unknown exception ;";
            }
            
            rte.transfermode = no_transfer;
            rte.transfersubmode.clr_all();
            recognized = true;
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}

