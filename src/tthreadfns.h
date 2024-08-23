// available templated thread-functions
// Copyright (C) 2007-2008 Harro Verkouter
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
#ifndef JIVE5A_TTHREADFNS_H
#define JIVE5A_TTHREADFNS_H

#include <map>
#include <utility>
#include <sys/uio.h>
#include <sys/socket.h>
#include <signal.h>
#include <sstream>

#include <sciprint.h>
#include <threadutil.h>
#include <getsok.h>
#include <getsok_udt.h>
#include <boyer_moore.h>
#include <udt.h>



// The framer. Gobbles in blocks of data and outputs
// compelete tape/diskframes as per Mark5 Memo #230 (Mark4/VLBA) and #... (Mark5B).
// The strictness of headerchecking is taken from the framerargs.
// If framerargs.strict == false then, basically, all that this checks is
// the syncword. (which is also all we can do for Mark5B anyway).
// For Mk4 and VLBA formats, strict==true implies CRC checking of one of the
// tracks [reasonably expensive check, but certainly a good one].
//  * mark4/vlba format as taken from Mark4 memo 230, revision 1.21, 10 June 2005
//      "Mark IIIA/IV/VLBA Tape Formats, Recording Modes and Compatibility"
//  * mark5b format as described in "Mark 5B User's manual", 8 August 2006 
//      (http://www.haystack.mit.edu/tech/vlbi/mark5/docs/Mark%205B%20users%20manual.pdf)
// Pushing tagged VDIF pushes with tag == thread_id!
inline bool do_push(const frame& f, outq_type<tagged<frame> >* qptr) {
    struct vdif_header const*  vhdr = reinterpret_cast<struct vdif_header const*>(f.framedata.iov_base);
    return qptr->push( tagged<frame>( is_vdif(f.frametype) ? vhdr->thread_id : 0, f) );
}
inline bool do_push(const frame& f, outq_type<frame>* qptr) {
    return qptr->push( f );
}

// counts how many syncwords of 0xffffff are following
inline unsigned int fpcount(void const * p, unsigned int bytes) {
    uint32_t const*       ptr = (uint32_t const*)p;
    uint32_t const* const e_ptr = (uint32_t const*)((unsigned char const*)p + bytes);

    for( ; (ptr+4)<e_ptr && *ptr==0xffffffff; ptr++ ) { }
    return (ptr - (uint32_t const*)p);
}


// The framer is templated on the actual output element type, it can be
// either 'frame' or 'tagged<frame>'. The fun part is that the code can now
// push either untagged or tagged frames.
// It does this by delegating the actual "push()" on the outputqueue to a
// freestanding function which looks at the type of the output queue and
// does the right thing. It is a compiletime known function which is inlined
// so it basically comes for free
template <typename OutElement>
void framer(inq_type<block>* inq, outq_type<OutElement>* outq, sync_type<framerargs>* args) {
    bool                stop;
    block               accublock;
    runtime*            rteptr;
    framerargs*         framer = args->userdata;
    headersearch_type   header         = framer->hdr;
    const unsigned int  syncword_area  = header.syncwordoffset + header.syncwordsize;
    // Searchvariables must be kept out of mainloop as we may need to
    // aggregate multiple blocks from our inputqueue before we find a 
    // complete frame
    uint64_t            nFrame        = 0;
    uint64_t            nBytes        = 0;
    boyer_moore         syncwordsearch(header.syncword, header.syncwordsize);
    unsigned int        bytes_to_next = header.framesize;
    const bool          no_syncword   = (header.syncwordsize==0 || header.syncword==0);
    const bool          strict        = framer->strict;
    // In non-strict mode we relax the conditions to be consistent whilst
    // allowing DBE Mark5B data.
    headersearch::strict_type   tm_decode_flg;
    
    if( strict )
        tm_decode_flg = headersearch::strict_type(headersearch::chk_default);
    else
        tm_decode_flg = headersearch::strict_type() | headersearch::chk_consistent |
                        headersearch::chk_allow_dbe | headersearch::chk_verbose |
                        headersearch::chk_nothrow ;

    rteptr = framer->rteptr;

    // Basic assertions: we require some elements downstream (qdepth)
    // AND we require that the supplied sizes make sense:
    //     framesize  >= headersize [a header fits in a frame]
    ASSERT2_COND( args->qdepth>0, SCINFO("there is no downstream Queue?") );
    ASSERT_COND( header.framesize  >= header.headersize );

    // allocate a buffer for <nframe> frames
    SYNCEXEC(args,
             framer->pool = new blockpool_type(header.framesize, 8);
             stop           = args->cancelled;);

    RTEEXEC(*rteptr, rteptr->statistics.init(args->stepid, "Framerv2"));
    counter_type&  counter( rteptr->statistics.counter(args->stepid) );

    DEBUG(0, "framer: start looking for " << header << " dataframes" << std::endl);

    // Before we enter our main loop we initialize:
    accublock     = framer->pool->get();
    bytes_to_next = header.framesize;

    // off we go! 
    while( !stop ) {
        block b;
        if ( !inq->pop(b) ) {
            break;
        }
        unsigned char*              ptr      = (unsigned char*)b.iov_base;
        unsigned char*              accubase = (unsigned char*)accublock.iov_base;
        unsigned int                ncached  = header.framesize - bytes_to_next;
        unsigned char const * const base_ptr = (unsigned char const * const)b.iov_base;
        unsigned char const * const e_ptr    = ptr + b.iov_len;

        // update accounting - we must do that now since we may jump back to
        // the next iteration of this loop w/o executing part(s) of this
        // loop 
        nBytes += b.iov_len;

        // Ah. New bytes came in.
        // first deal with leftover bytes from previous block, if any.
        // We copy at most "syncword_area-1" bytes out of the new block and
        // append them to the amount that's cached: if no syncword was found
        // in that amount of bytes then we start searching the new block
        // instead, discarding all bytes that we kept.
        while( ncached && ptr<e_ptr ) {
            // can we look for syncword yet? If we're doing a format that
            // doesn't have a syncword we don't have to search either
            const bool           search = (ncached<syncword_area && !no_syncword);
            const unsigned int   navail = (unsigned int)(e_ptr-ptr);
            const unsigned int   ncpy   = (search)?
                                            std::min((2*syncword_area)-1-ncached, navail):
                                            std::min(bytes_to_next, navail);

            ::memcpy(accubase+ncached, ptr, ncpy);
            bytes_to_next   -= ncpy;
            ptr             += ncpy;
            ncached         += ncpy;

            // If we have sufficient bytes to determine if the header is
            // indeed a valid header do that now, use track 5 for now
            if( strict && ncached>=header.headersize && header.check(accubase, headersearch::chk_default, 5)==false ) {
                // ok, not-a-frame then.
                // stop copying data into the local cache and restart search
                // in big block
                if( (ptrdiff_t)(ptr-base_ptr)>(ptrdiff_t)syncword_area )
                    ptr          -= syncword_area;

                ncached       = 0;
                bytes_to_next = header.framesize;
                continue;
            }

            if( bytes_to_next==0 ) {
                // ok, we has a frames!
                frame  f(header.frameformat, header.ntrack, accublock);


                f.frametime   = header.decode_timestamp(accubase, tm_decode_flg/*headersearch::chk_default*/, 0);
                // If valid frame, push & count it
                if( f.frametime.tv_sec ) {
                    stop          = (::do_push(f, outq)==false);

                    // update statistics!
                    counter      += header.framesize;
                    nFrame++;
                }

                // ok! get ready to accept any leftover bytes
                // from the next main loop.
                accublock     = framer->pool->get();
                accubase      = (unsigned char*)accublock.iov_base;
                bytes_to_next = header.framesize;
                ncached       = 0;
                continue;
            }

            // If we do not have to search for the syncword anymore, we were
            // just copying bytes - carry on doing that
            if( !search )
                continue;

            const unsigned char*     sw = syncwordsearch(accubase, ncached);

            if( sw==0 ) {
                // not found. depending on how many bytes we copied
                // we take the appropriate action
                if( ncpy>=(syncword_area-1) ) {
                    // no syncword found in concatenation of
                    // previous bytes with a full syncwordarea less 1 of
                    // the new block: *if* there is a syncword area in the
                    // new block it's at least going to be at the start!
                    // Return to the state that indicates an empty
                    // accumulator block and start the syncwordsearch
                    // in the whole block in the main loop below.
                    // We discard all local cached bytes.
                    bytes_to_next = header.framesize;
                    ptr           = (unsigned char*)b.iov_base;
                    ncached       = 0;
                } else {
                    // It may be that we just didn't have enough
                    // bytes just yet - 'ncpy' could be 0 at this 
                    // point. 
                    // If we have syncword_area or more bytes
                    // w/o syncword we keep only the last
                    // (syncwordarea-1) bytes - this will trigger
                    // a new 'search' on the next entry of this loop
                    const unsigned int  nkeep( std::min(syncword_area-1, ncached) );

                    ::memmove(accubase, accubase + ncached - nkeep, nkeep);
                    bytes_to_next = header.framesize - nkeep;
                    ncached       = nkeep;
                }
                // Ok, the state after a failed syncword search
                // has been set - now continue with the outer loop;
                // it should fall out of it anyway.
                continue;
            }
            // If the syncword is not at least at the offset where we
            // expect it, we has an ishoos!
            const unsigned int swpos = (unsigned int)(sw - accubase);
            if( swpos<header.syncwordoffset ) {
                // we're d00med. we're missing pre-syncword bytes!
                // best to discard all locally cached bytes and
                // restart
                bytes_to_next = header.framesize;
                ptr           = (unsigned char*)b.iov_base;
                ncached       = 0;
                continue;
            } else if( swpos>header.syncwordoffset ) {
                // OTOH - if it's a bit too far off, we
                // move it to where it should be:

                const unsigned int diff = swpos - header.syncwordoffset;
                ::memmove(accubase, accubase + diff, ncached-diff);
                ncached       -= diff;
                bytes_to_next += diff;
            }
            // Ok - we now either have found the syncword and put
            // the data where it should be or we discarded everything.
            // Let's try again!
        } // end of dealing with cached bytes

        // If we already exhausted the block we can skip the code below
        if( stop || ptr>=e_ptr )
            continue;

        // Main loop over the remainder of the incoming block: we look for
        // syncwords until we run out of block and keep the remainder for
        // the next incoming block
        while( ptr<e_ptr ) {
            const unsigned int          navail = (unsigned int)(e_ptr-ptr);
            unsigned char const * const sw     = (no_syncword?ptr:syncwordsearch(ptr, navail));

            if( sw==0 ) {
                // no more syncwords. Keep at most 'syncarea-1' bytes for the future
                const unsigned int nkeep = std::min(syncword_area-1, navail);

                ::memcpy(accubase, e_ptr - nkeep, nkeep);
                bytes_to_next = header.framesize - nkeep;
                // trigger end-of-loop condtion
                ptr = const_cast<unsigned char*>(e_ptr);
                continue;
            }
            // At least we HAVE a syncword
            const unsigned int swpos = (unsigned int)(sw - ptr);

            // If syncword found too close to start of search (which we
            // assume the start-of-frame to be, generally) we must discard
            // this one: we're missing pre-syncword bytes so we jump over
            // the syncword and start searching from there
            if( swpos<header.syncwordoffset ) {
                ptr = const_cast<unsigned char*>(sw) + header.syncwordsize;
                continue;
            }

            // Now the syncword is *at least* at the offset from 'ptr'  where it
            // should be so we can safely do 'syncwordptr - header.syncwordoffset'
            // Also compute how many bytes we have - it may be a truncated
            // frame.
            //
            // keep a pointer to where the current frame should start in memory
            // (ie pointing at the first 'p' or 'S')
            //    * sof  == start-of-frame
            //
            // Most important assumption: any frame looks like:
            //
            //  [pppp]SSSSShhhhhdddddddd
            //  |<------- frame ------>|
            //  |<-- header -->|
            //
            // Where:
            //  pppp : optional pre-syncword header bytes
            //  SSSS : syncword bytes
            //  hhhh : header bytes
            //  dddd : frame data bytes
            unsigned char const * const sof = sw - header.syncwordoffset;
            const unsigned int          num = (unsigned int)(e_ptr - sof);

            if( num<header.framesize ) {
                // yup, incomplete frame. Means that the current block
                // was exhausted before we could find a complete frame.
                // Copy to the local accumulator buffer so's the next
                // incoming bytes can be appended.
                ::memcpy(accubase, sof, num);
                bytes_to_next = header.framesize - num;

                // trigger loop end condition
                ptr = const_cast<unsigned char*>(e_ptr);
                continue;
            }

            // Possibly extracts a track [Mk4, VLBA] and does a CRC check on the header
            // use track 5 for now
            if( strict && header.check(sof, headersearch::chk_default, 5)==false ) {
                // invalid frame! restart after the current syncword
                ptr = const_cast<unsigned char*>(sof)+syncword_area;
                continue;
            }

            // Sweet! We has a frames!
            block   fblock = b.sub((sof - base_ptr), header.framesize);
            frame   f(header.frameformat, header.ntrack, fblock);

            f.frametime   = header.decode_timestamp(sof, tm_decode_flg/*headersearch::chk_default*/, 0);

            // Only attempt to pass on valid frames
            if( f.frametime.tv_sec ) {
                // Fail to push downstream means: quit!
                //if( (stop=(outq->push(f)==false))==true )
                if( (stop=(::do_push(f, outq)==false))==true )
                    break;
                // update statistics!
                counter += header.framesize;
                // chalk up one more frame.
                nFrame++;
            }
            // Advance ptr to what hopefully is the
            // next frame
            ptr = const_cast<unsigned char*>(sof) + header.framesize;
        } // done processing block
    }
    // we take it that if nBytes==0ULL => nFrames==0ULL (...)
    // so the fraction would come out to be 0/1.0 = 0 rather than
    // a divide-by-zero exception.
    double bytes    = ((nBytes==0)?(1.0):((double)nBytes));
    double fraction = ((double)(nFrame * header.framesize)/bytes) * 100.0;
    DEBUG(0, "framer: stopping. Found " << nFrame << " frames, " << 
             "fraction=" << fraction << "% of " << nBytes << " bytes" << std::endl);
    return;
}



// The name says it all, really
template <typename T>
void bitbucket(inq_type<T>* inq) {
    T  b;
    DEBUG(2, "bitbucket: starting" << std::endl);
    while( inq->pop(b) ) { };
    DEBUG(2, "bitbucket: stopping" << std::endl);
}


// Generic filedescriptor writer.
// whatever gets popped from the inq gets written to the filedescriptor.
// leave intelligence up to other steps.
template <typename T>
void fdwriter(inq_type<T>* inq, sync_type<fdreaderargs>* args) {
    bool              stop = false;
    size_t            nchunk;
    runtime*          rteptr;
    uint64_t          nbyte = 0;
    struct iovec*     chunks = new struct iovec[16];
    fdreaderargs*     network = args->userdata;

    rteptr = network->rteptr;
    ASSERT_COND(rteptr!=0);
    // first things first: register our threadid so we can be cancelled
    // if the network is to be closed and we don't know about it
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).
    install_zig_for_this_thread(SIGUSR1);
    SYNCEXEC(args,
             stop              = args->cancelled;
             delete network->threadid;
             network->threadid = new pthread_t(::pthread_self()));

    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "FdWrite"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    if( stop ) {
        DEBUG(0, "fdwriter: got stopsignal before actually starting" << std::endl);
        SYNCEXEC(args, delete network->threadid; network->threadid=0);
        return;
    }

    DEBUG(0, "fdwriter: writing to fd=" << network->fd << std::endl);

    uint64_t bytes_in_cache = 0;
    // blind copy of incoming data to outgoing filedescriptor
    while( true ) {
        T b;
        if ( !inq->pop(b) ) {
            break;
        }
        ssize_t                    bcnt;
        ssize_t                    rv;
        struct iovec*              cptr;
        typename T::const_iterator bptr;

        // FIXME: FIXME if there are more than 16 entries in the block, this
        //              will fail!
        for( bptr=b.begin(), cptr=&chunks[0], nchunk=0, bcnt=0;
             bptr!=b.end() && nchunk<16;
             cptr++, bptr++, nchunk++) {
            cptr->iov_base  = bptr->iov_base;
            cptr->iov_len   = bptr->iov_len;
            bcnt           += (ssize_t)bptr->iov_len;
        }
        // DO NOT ENTER A BLOCKING SYSTEMCALL WITH A LOCK HELD!
        if( (rv=::writev(network->fd, chunks, nchunk))!=(ssize_t)bcnt ) {
            lastsyserror_type lse;
            DEBUG(0, "fdwriter: fail to write " << bcnt << " bytes "
                     << lse << " (only " << rv << " written, nchunk=" << nchunk << ")" << std::endl);
            break;
        }
        
        nbyte   += (uint64_t)bcnt;
        counter += (counter_type)bcnt;

        bytes_in_cache += (uint64_t)bcnt;

        if ( bytes_in_cache > network->max_bytes_to_cache ) {
            // flush every now and then, to prevent the kernel to built up a huge cache
            if ( (rv = ::fsync( network->fd )) != 0 ) {
                DEBUG(-1, "fdwriter: sync failed: " << evlbi5a::strerror(errno) << std::endl);
            }
            bytes_in_cache = 0;
        }
    }
    // Issue a shutdown for writing - we know we're not going to write data
    // anymore. Also issue a read - wait until the other end has closed as
    // well. In case the sokkit was already geclosed, this finishes
    // immediately (I hope :D)
    if( ::shutdown(network->fd, SHUT_WR)==0 ) {
        char    c;
        if( ::read(network->fd, &c, 1) ) {}
    }
    // We're not going to block on the fd anymore so we should unregister
    // ourselves from receiving signals
    SYNCEXEC(args, delete network->threadid; network->threadid = 0);

    delete [] chunks;
    DEBUG(0, "fdwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte,"byte") << ")"
             << std::endl);
    network->finished = true;
}

// Write to UDT socket
template <typename T>
void udtwriter(inq_type<T>* inq, sync_type<fdreaderargs>* args) {
    bool              stop = false;
    runtime*          rteptr;
    uint64_t          nbyte = 0;
    fdreaderargs*     network = args->userdata;

    rteptr = network->rteptr;
    ASSERT_COND(rteptr!=0);

    // first things first: register our threadid so we can be cancelled
    // if the network is to be closed and we don't know about it
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).
    install_zig_for_this_thread(SIGUSR1);
    SYNCEXEC(args,
             stop              = args->cancelled;
             delete network->threadid;
             network->threadid = new pthread_t(::pthread_self()));

    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    RTEEXEC(*rteptr,
            rteptr->evlbi_stats[ network->tag ] = evlbi_stats_type();
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "UdtWrite"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);
    ucounter_type& loscnt( rteptr->evlbi_stats[ network->tag ].pkt_lost );
    ucounter_type& pktcnt( rteptr->evlbi_stats[ network->tag ].pkt_in );

    if( stop ) {
        DEBUG(0, "udtwriter: got stopsignal before actually starting" << std::endl);
        SYNCEXEC(args, delete network->threadid; network->threadid=0);
        return;
    }

    // Grab a hold of the congestion control instance
    int                  old_ipd = -314, dummy;
    /*socklen_t            dummy;*/ // UDTv2 API is POSIX compliant, but v1 is default
    IPDBasedCC*          ccptr = 0;
    const netparms_type& np( network->rteptr->netparms );

    ASSERT_ZERO( UDT::getsockopt(network->fd, SOL_SOCKET, UDT_CC, &ccptr, &dummy) );

    if( ccptr==0 ) {
        DEBUG(-1, "udtwriter: WARNING - no congestion control instance found, rate limiting disabled" << std::endl);
    }

    DEBUG(0, "udtwriter: writing to fd=" << network->fd << std::endl);

    // blind copy of incoming data to outgoing filedescriptor
    while( !stop ) {
        T b;
        if ( !inq->pop(b) ) {
            break;
        }
        ssize_t                    rv;
        UDT::TRACEINFO             ti;
        typename T::const_iterator bptr;

        for(bptr=b.begin(); !stop && bptr!=b.end(); bptr++) {
            size_t    nsent = 0;
            const int ipd = ipd_ns( np );

            if( old_ipd!=ipd ) {
                if( ccptr ) {
                    ccptr->set_ipd( ipd );
                    DEBUG(0, "udtwriter: switch to ipd=" << float(ipd)/1000.0f
                             << " [set=" << float(ipd_set_ns(np))/1000.0f << ", "
                             << ", theoretical=" << float(theoretical_ipd_ns(np))/1000.0f << "]"
                             << std::endl);
                }
                old_ipd = ipd;
            }

            while( nsent!=bptr->iov_len ) {
                rv = UDT::send(network->fd, ((char const*)bptr->iov_base)+nsent, bptr->iov_len - nsent, 0);
                if( rv==UDT::ERROR ) {
                    DEBUG(0, "udtwriter: fail to write " << bptr->iov_len << " bytes (only " << nsent << " sent) "
                            << UDT::getlasterror().getErrorMessage() << std::endl);
                    stop = true;
                    break;
                }
                nsent   += (size_t)rv;
                nbyte   += (uint64_t)rv;
                counter += (counter_type)rv;
            }
        }
        if( UDT::perfmon(network->fd, &ti, true)==0 ) {
            pktcnt = ti.pktSentTotal;
            loscnt += ti.pktSndLoss;
        }
    }
    // We're not going to block on the fd anymore, do unregister ourselves
    // from being signalled
    SYNCEXEC(args, delete network->threadid; network->threadid = 0);
    // If we terminated normally - i.e. not because the socket was closed
    // behind our backs - enable lingering again such that all queued data may
    // arrive at the other end
    if( !stop ) {
        struct linger l;

        l.l_onoff  = 1;
        l.l_linger = 180;
        DEBUG(3, "udtwriter: re-enabling lingering on UDT socket for " << l.l_linger << "s" << std::endl);
        // Do not make an assertion out of this, it is not lethal (well, it
        // is, but not something that we can fix!)
        if( UDT::setsockopt(network->fd, SOL_SOCKET, UDT_LINGER, &l, sizeof(struct linger))==UDT::ERROR ) {
            DEBUG(-1, "udtwriter: FAILED to re-enable lingering on UDT socket. Queued data may be lost." << std::endl);
        }
    }
    DEBUG(0, "udtwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte,"byte") << ")"
             << std::endl);
}

// Write incoming blocks of data in chunks of 'constraints::write_size'
// to the network, prepending 64 bits of strict
// monotonically increasing sequencenumber in front of it
template <typename T>
void udpswriter(inq_type<T>* inq, sync_type<fdreaderargs>* args) {
    int                    oldipd = -300;
    bool                   stop = false;
    ssize_t                ntosend;
    runtime*               rteptr;
    uint64_t               seqnr;
    uint64_t               nbyte = 0;
    struct iovec           iovect[2];
    fdreaderargs*          network = args->userdata;
    struct msghdr          msg;
    struct ::timeval       sop;
    struct ::timeval       now;
    const netparms_type&   np( network->rteptr->netparms );

    rteptr = network->rteptr;

    // assert that the sizes in there make sense
    RTEEXEC(*rteptr, rteptr->sizes.validate()); 
    const unsigned int     wr_size = rteptr->sizes[constraints::write_size];
    args->lock();
    // the d'tor of "fdreaderargs" will delete the storage for us!
    stop              = args->cancelled;
    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    args->unlock();

    if( stop ) {
        DEBUG(-1, "udpswriter: cancelled before actual start" << std::endl);
        return;
    }
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "NetWrite/UDPs"));

    counter_type& counter( rteptr->statistics.counter(args->stepid) );

    // Initialize the sequence number with a random 32bit value
    // - just to make sure that the receiver does not make any
    // implicit assumptions on the sequencenr other than that it
    // is strictly monotonically increasing.
    seqnr = (uint64_t)evlbi5a::random();

    // Initialize stuff that will not change (sizes, some adresses etc)
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;
    msg.msg_iov        = &iovect[0];
    msg.msg_iovlen     = sizeof(iovect)/sizeof(struct iovec);
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // part of the iovec can also be filled in [ie: the header]
    iovect[0].iov_base = &seqnr;
    iovect[0].iov_len  = sizeof(seqnr);
    // we always send datagrams of size datagramsize
    iovect[1].iov_len  = wr_size;

    // Can precompute how many bytes should be sent in a sendmsg call
    ntosend = iovect[0].iov_len + iovect[1].iov_len;

    DEBUG(0, "udpswriter: first sequencenr=" << seqnr
             << " fd=" << network->fd
             << " n2write=" << ntosend << std::endl);
    // send out any incoming blocks out over the network
    // initialize "start-of-packet" to "now()". the sendloop
    // waits with sending as long as "now()" is not later than
    // "start-of-packet". by forcing that condition to true here the
    // first packet will be sent immediately.
    // by doing it like this - having a time-of-send outside the
    // major loop and pre-computing when to send the next packet
    // (if any) - we get, hopefully, an even nicer behaved sending
    // function: it is more of an absolute timing now than a 
    // relative one ("wait ipd microseconds after you sent the
    // previous one"), which was the previous implementation.
#if 0
    typedef std::map<int,unsigned int> histogram_type;
    int              delta;
    histogram_type   hist;
    struct ::timeval last;
#endif
    ::gettimeofday(&sop, 0);
#if 0
    last = sop;
#endif
    while( !stop ) {
        T b;
        if ( !inq->pop(b) ) {
            break;
        }
        const int                  ipd( ipd_us(np) );
        typename T::const_iterator bptr;

        // Loop over all blocks in the popped item
        for(bptr=b.begin(); bptr!=b.end(); bptr++) {
            unsigned char*       ptr = (unsigned char*)bptr->iov_base;
            const unsigned char* eptr = (ptr + bptr->iov_len);
            if( ipd!=oldipd ) {
                DEBUG(0, "udpswriter: switch to ipd=" << ipd << " [set=" << ipd_set_us(np) << ", " <<
                        "theoretical=" << theoretical_ipd_us(np) << "]" << std::endl);
                oldipd = ipd;
            }
            while( (ptr+wr_size)<=eptr ) {
                // iovect[].iov_base is of the "void*" persuasion.
                // we would've liked to use thattaone directly but
                // because of its void-pointerishness we cannot
                // (can't do pointer arith on "void*").
                iovect[1].iov_base = ptr;

                // at this point, wait until the current time
                // is at least "start-of-packet" [and ipd >0 that is].
                // in fact we delay sending of the packet until it is time to send it.
                while( ipd>0 ) {
                    ::gettimeofday(&now, 0);
                    // Because of a problem with gettimeofday going back
                    // under certain circumstances (https://lkml.org/lkml/2007/8/23/96)
                    // we have to be suspicious of timejumps > 3600 seconds.
                    // Technically we have a problem here since it can take
                    // a non-zero amount of time between staring this
                    // function (initializing 'start-of-packet', "sop")
                    // immediately before the loop and the arrival of actual
                    // data (the ".pop()" method is blocking - it could wait
                    // a LOOOONG time before someone actually switches on
                    // the dataflow!).
                    // According to the lkml.org bug report the jump is
                    // typically > 4000 seconds into the future so if we
                    // give the system an hour .. that should be good
                    // enough.
                    // The fix: if the time diff > 3600 seconds, do a second
                    // gettimeofday. If after that the time is *still* in
                    // the future, it most likely actually *IS*, ie the
                    // ".pop()" took a looooong time ;-)

                    // Do the test in two steps since we can't really be
                    // sure of what type the .tv_sec member is other than
                    // being integral
                    if( now.tv_sec>sop.tv_sec &&
                        now.tv_sec - sop.tv_sec > 3600 )
                            ::gettimeofday(&now, 0);
                    if( now.tv_sec>sop.tv_sec )
                        break;
                    if( now.tv_sec<sop.tv_sec )
                        continue;
                    if( now.tv_usec>=sop.tv_usec )
                        break;
                }
                if( ::sendmsg(network->fd, &msg, MSG_EOR)!=ntosend ) {
                    DEBUG(-1, "udpswriter: failed to send " << ntosend << " bytes - " <<
                            evlbi5a::strerror(errno) << " (" << errno << ")" << std::endl);
                    stop = true;
                    break;
                }
#if 0
                delta = ((int)now.tv_sec - (int)last.tv_sec)*1000000 + (int)now.tv_usec - (int)last.tv_usec;
                hist[delta]++;
                last = now;
#endif

                // update loopvariables.
                // only update send-time of next packet if ipd>0
                if( ipd>0 ) {
                    sop          = now;
                    sop.tv_usec += ipd;
                    if( sop.tv_usec>=1000000 ) {
                        sop.tv_sec  += 1;
                        sop.tv_usec -= 1000000;
                    }
                }
                ptr     += wr_size;
                nbyte   += wr_size;
                counter += ntosend;
                seqnr++;
            }
        }
    }
    SYNCEXEC(args, delete network->threadid; network->threadid=0);
    DEBUG(0, "udpswriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte, "byte") << ")"
             << std::endl);
#if 0
    histogram_type::const_iterator  cur;
    for(cur=hist.begin(); cur!=hist.end(); cur++)
        ::printf("%4d ", cur->first);
    ::printf("\n");
    for(size_t i=0; i<hist.size(); i++)
        ::printf("----");
    ::printf("\n");
    for(cur=hist.begin(); cur!=hist.end(); cur++)
        ::printf("%4d ", cur->second);
    ::printf("\n");
#endif
    network->finished = true;
}

// Write each incoming block *as a whole* to the destination,
// prepending each block with a 64bit strict monotonically
// incrementing sequencenumber
template <typename T>
void vtpwriter(inq_type<T>* inq, sync_type<fdreaderargs>* args) {
    T                      b;
    int                    oldipd = -300;
    bool                   stop = false;
    ssize_t                ntosend;
    runtime*               rteptr;
    uint64_t               seqnr;
    uint64_t               nbyte = 0;
    struct iovec           iovect[17];
    fdreaderargs*          network = args->userdata;
    struct msghdr          msg;
    struct ::timeval       sop;
    struct ::timeval       now;
    const netparms_type&   np( network->rteptr->netparms );

    rteptr = network->rteptr;

    // assert that the sizes in there make sense
    RTEEXEC(*rteptr, rteptr->sizes.validate()); 

    args->lock();
    // the d'tor of "fdreaderargs" will delete the storage for us!
    stop              = args->cancelled;
    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    args->unlock();

    if( stop ) {
        DEBUG(-1, "vtpwriter: cancelled before actual start" << std::endl);
        return;
    }
    install_zig_for_this_thread(SIGUSR1);
    SYNCEXEC(args,
             delete network->threadid;
             network->threadid = new pthread_t(::pthread_self()));
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "NetWrite/VTP"));

    counter_type& counter( rteptr->statistics.counter(args->stepid) );

    // Initialize the sequence number with a random 32bit value
    // - just to make sure that the receiver does not make any
    // implicit assumptions on the sequencenr other than that it
    // is strictly monotonically increasing.
    seqnr = (uint64_t)evlbi5a::random();

    // Initialize stuff that will not change (sizes, some adresses etc)
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;
    msg.msg_iov        = &iovect[0];
    msg.msg_iovlen     = sizeof(iovect)/sizeof(struct iovec);
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // part of the iovec can also be filled in [ie: the header]
    iovect[0].iov_base = &seqnr;
    iovect[0].iov_len  = sizeof(seqnr);
    iovect[1].iov_len  = 0;


    DEBUG(0, "vtpwriter: first sequencenr=" << seqnr
             << " fd=" << network->fd << std::endl);
    // send out any incoming blocks out over the network
    // initialize "start-of-packet" to "now()". the sendloop
    // waits with sending as long as "now()" is not later than
    // "start-of-packet". by forcing that condition to true here the
    // first packet will be sent immediately.
    // by doing it like this - having a time-of-send outside the
    // major loop and pre-computing when to send the next packet
    // (if any) - we get, hopefully, an even nicer behaved sending
    // function: it is more of an absolute timing now than a 
    // relative one ("wait ipd microseconds after you sent the
    // previous one"), which was the previous implementation.
    ::gettimeofday(&sop, 0);
    while( !stop && inq->pop(b) ) {
        const int                  ipd( ipd_us(np) );
        struct iovec*              cptr = &iovect[1];
        typename T::const_iterator bptr;

        if( ipd!=oldipd ) {
            DEBUG(0, "vtpwriter: switch to ipd=" << ipd << " [set=" << ipd_set_us(np) << ", " <<
                     "theoretical=" << theoretical_ipd_us(np) << "]" << std::endl);
            oldipd = ipd;
        }
        msg.msg_iovlen = 1;
        // transfer the pointers to the array
        for(bptr=b.begin(), ntosend=iovect[0].iov_len; bptr!=b.end() && msg.msg_iovlen<17; bptr++, cptr++, msg.msg_iovlen++) {
            cptr->iov_base  = bptr->iov_base;
            cptr->iov_len   = bptr->iov_len;
            ntosend        += bptr->iov_len;
        }

        // at this point, wait until the current time
        // is at least "start-of-packet" [and ipd >0 that is].
        // in fact we delay sending of the packet until it is time to send it.
        while( ipd>0 ) {
            ::gettimeofday(&now, 0);
            if( now.tv_sec>sop.tv_sec )
                break;
            if( now.tv_sec<sop.tv_sec )
                continue;
            if( now.tv_usec>=sop.tv_usec )
                break;
        }

        if( ::sendmsg(network->fd, &msg, MSG_EOR)!=ntosend ) {
            DEBUG(-1, "vtpwriter: failed to send " << ntosend << " bytes - " <<
                    evlbi5a::strerror(errno) << " (" << errno << ")" << std::endl);
            stop = true;
            break;
        }
        if( ipd>0 ) {
            sop          = now;
            sop.tv_usec += ipd;
            if( sop.tv_usec>=1000000 ) {
                sop.tv_sec  += 1;
                sop.tv_usec -= 1000000;
            }
        }

        // update loopvariables.
        // only update send-time of next packet if ipd>0
        nbyte   += ntosend;
        counter += ntosend;
        seqnr++;
    }
    SYNCEXEC(args, delete network->threadid; network->threadid=0);
    DEBUG(0, "vtpwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte, "byte") << ")"
             << std::endl);
    network->finished = true;
}

// Break up incoming blocks into chunks of 'constraints::write_size'
// and write them to the network
template <typename T>
void udpwriter(inq_type<T>* inq, sync_type<fdreaderargs>* args) {
    int                    oldipd = -300;
    bool                   stop = false;
    runtime*               rteptr;
    uint64_t               nbyte = 0;
#if 0
    struct iovec           iovect[17];
#endif
    fdreaderargs*          network = args->userdata;
    struct ::timeval       sop;
    struct ::timeval       now;
#if 0
    struct msghdr          msg;
#endif
    const unsigned int     pktsize = network->rteptr->sizes[constraints::write_size];
    const netparms_type&   np( network->rteptr->netparms );

    rteptr = network->rteptr;
    ASSERT2_COND(pktsize>0, SCINFO("internal error with constraints"));

    args->lock();
    stop              = args->cancelled;
    args->unlock();

    if( stop ) {
        DEBUG(0, "udpwriter: got stopsignal before actually starting" << std::endl);
        return;
    }
    install_zig_for_this_thread(SIGUSR1);
    SYNCEXEC(args,
             delete network->threadid;
             network->threadid = new pthread_t(::pthread_self()));
#if 0
    // Initialize stuff that will not change (sizes, some adresses etc)
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;
    msg.msg_iov        = &iovect[0];
    msg.msg_iovlen     = 0/*sizeof(iovect)/sizeof(struct iovec)*/;
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;
#endif

    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "NetWrite/UDP"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    DEBUG(0, "udpwriter: writing to fd=" << network->fd << " wr:" << pktsize << std::endl);
    // any block we pop we put out in chunks of pktsize, honouring the ipd
    ::gettimeofday(&sop, 0);
    while( !stop ) {
        T b;
        if ( !inq->pop(b) ) {
            break;
        }
        const int                  ipd( ipd_us(np) );
#if 0
        unsigned int               io_bytes = 0, io = 0;
#endif
        typename T::const_iterator bptr;

        if( ipd!=oldipd ) {
            DEBUG(0, "udpwriter: switch to ipd=" << ipd << " [set=" << ipd_set_us(np) << ", " <<
                     "theoretical=" << theoretical_ipd_us(np) << "]" << std::endl);
            oldipd = ipd;
        }
#if 0
        // This is crappy loop: we have to loop over blocks and fill
        // the struct iovec at the same time. We send 'pktsize' bytes
        // at maximum per send action.
        // We must be able to handle both cases where:
        //   * one block is > pktsize (ie mulitple sends/block)
        // as well as the one where
        //   * multiple blocks < pktsize (ie multiple blocks/send)
        for( bptr=b.begin(); bptr!=b.end(); bptr++ ) {
            unsigned char*       ptr = (unsigned char*)bptr->iov_base;
            const unsigned char* eptr = (const unsigned char*)(ptr + bptr->iov_len);
            while( ptr<eptr ) {
                const unsigned int avail_data( eptr - ptr );
                const unsigned int avail_send( pktsize-io_bytes );
                iovect[io].iov_base = ptr;
                iovect[io].iov_len  = min(avail_data, avail_send);
                io_bytes += iovect[io].iov_len;
                ptr      += iovect[io].iov_len;
                ASSERT_COND( ++io < sizeof(iovect)/sizeof(iovect[0]),
                             SCINFO("blocks too small for packetsize; only room for " << sizeof(iovect)/sizeof(iovect[0]) << " blocks") );
            }
        }
#endif
        for( bptr=b.begin(); bptr!=b.end(); bptr++ ) {
            unsigned char*       ptr = (unsigned char*)bptr->iov_base;
            const unsigned char* eptr = (const unsigned char*)(ptr + bptr->iov_len);
            
            while( (ptr+pktsize)<=eptr ) {
                // at this point, wait until the current time
                // is at least "start-of-packet" [and ipd >0 that is].
                // in fact we delay sending of the packet until it is time to send it.
                while( ipd>0 ) {
                    ::gettimeofday(&now, 0);
                    // Need to take care of kernel bug - see below
                    // comment in "udpswriter()" function
                    if( now.tv_sec>sop.tv_sec &&
                        now.tv_sec - sop.tv_sec > 3600 )
                            ::gettimeofday(&now, 0);
                    if( now.tv_sec>sop.tv_sec )
                        break;
                    if( now.tv_sec<sop.tv_sec )
                        continue;
                    if( now.tv_usec>=sop.tv_usec )
                        break;
                }

                if( ::write(network->fd, ptr, pktsize)!=(int)pktsize ) {
                    lastsyserror_type lse;
                    DEBUG(0, "udpwriter: fail to write " << pktsize << " bytes " << lse << std::endl);
                    stop = true;
                    break;
                }
                if( ipd>0 ) {
                    sop          = now;
                    sop.tv_usec += ipd;
                    if( sop.tv_usec>=1000000 ) {
                        sop.tv_sec  += 1;
                        sop.tv_usec -= 1000000;
                    }
                }
                nbyte   += pktsize;
                ptr     += pktsize;
                counter += pktsize;
            }
            if( !stop && ptr!=eptr )
                DEBUG(-1, "udpwriter: internal constraint problem - block is not multiple of pkt\n"
                        <<"           block:" << bptr->iov_len << " pkt:" << pktsize << std::endl);
        }
    }
    SYNCEXEC(args, delete network->threadid; network->threadid=0);
    DEBUG(0, "udpwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte,"byte") << ")"
             << std::endl);
    network->finished = true;
}

// Highlevel networkwriter interface. Does the accepting if necessary
// and delegates to either the generic filedescriptorwriter or the udp-smart
// writer, depending on the actual protocol
template <typename T>
void netwriter(inq_type<T>* inq, sync_type<fdreaderargs>* args) {
    bool                   stop;
    fdreaderargs*          network = args->userdata;
    const std::string      proto   = network->netparms.get_protocol();

    SYNCEXEC(args,  stop = args->cancelled);

    if( stop ) {
        DEBUG(0, "netwriter: stop signalled before we actually started" << std::endl);
        return;
    }
    // we may have to accept first [eg "rtcp"]
    if( network->doaccept ) {
        pthread_t                 my_tid( ::pthread_self() );
        pthread_t*                old_tid  = 0;
        fdprops_type::value_type* incoming = 0;
        
        // Before entering a potentially blocking accept() set up
        // infrastructure to allow other thread(s) to interrupt us and get
        // us out of catatonic sleep:
        // if the network (if 'fd' refers to network that is) is to be closed
        // and we don't know about it because we're in a blocking syscall.
        // (under linux, closing a filedescriptor in one thread does not
        // make another thread, blocking on the same fd, wake up with
        // an error. b*tards. so we have to manually send a signal to wake a
        // thread up!).
        install_zig_for_this_thread(SIGUSR1);
        SYNCEXEC(args, old_tid = network->threadid; network->threadid = &my_tid);

        RTEEXEC(*network->rteptr, network->rteptr->transfersubmode.set(wait_flag));
        DEBUG(0, "netwriter: waiting for incoming connection" << std::endl);

        // Attempt to accept. "do_accept_incoming" throws on wonky!
        // make sure we catch any errors and prevent any d'tor from being
        // called at this point (some code may expect the
        // 'network->threadid' to point at something coming from 'new' and
        // might call 'delete' on it. Our version is stack based and as such
        // would SIGSEGV if delete was called on that address)
        try {
            // dispatch based on protocol
            if( proto=="unix" )
                incoming = new fdprops_type::value_type( do_accept_incoming_ux(network->fd) );
            else if( proto=="udt" )
                incoming = new fdprops_type::value_type( do_accept_incoming_udt(network->fd) );
            else
                incoming = new fdprops_type::value_type( do_accept_incoming(network->fd) );
        }
        catch( ... ) {
            // no need to delete memory - our pthread_t was allocated on the stack
            uninstall_zig_for_this_thread(SIGUSR1);
            SYNCEXEC(args, network->threadid = old_tid);
            throw;
        }
        uninstall_zig_for_this_thread(SIGUSR1);

        // great! we have accepted an incoming connection!
        // check if someone signalled us to stop (cancelled==true).
        // someone may have "pressed cancel" between the actual accept
        // and us getting time to actually process this.
        // if that wasn't the case: close the lissnin' sokkit
        // and install the newly accepted fd as network->fd.
        // Whilst we have the lock we can also put back the old threadid.
        args->lock();
        stop              = args->cancelled;
        network->threadid = old_tid;
        if( !stop ) {
            if( proto=="udt" )
                UDT::close(network->fd);
            else
                ::close(network->fd);
            network->fd = incoming->first;
        }
        args->unlock();

        if( stop ) {
            DEBUG(0, "netwriter: stopsignal before actual start " << std::endl);
            return;
        }
        // as we are not stopping yet, inform user whom we've accepted from
        DEBUG(0, "netwriter: incoming dataconnection from " << incoming->second << std::endl);

        // clean up
        delete incoming;
    }
    // update submode flags. we can safely say that we're connected
    // but if we're running? leave that up to someone else to decide
    RTEEXEC(*network->rteptr,
            network->rteptr->transfersubmode.clr(wait_flag).set(connected_flag));

    // now drop into either the generic fdwriter or the udpswriter
    if( proto=="udps" )
        ::udpswriter<T>(inq, args);
    else if( proto=="udp" )
        ::udpwriter<T>(inq, args);
    else if( proto=="udt" )
        ::udtwriter<T>(inq, args);
    else if( proto=="itcp" ) {
        // write the itcp id into the stream before falling to the normal
        // tcp writer
        std::string   itcp_id_buffer( "id: " + network->rteptr->itcp_id );
        pthread_t     my_tid( ::pthread_self() );
        pthread_t*    old_tid  = 0;

        SYNCEXEC(args, old_tid = network->threadid; network->threadid = &my_tid);
        install_zig_for_this_thread(SIGUSR1);

        itcp_id_buffer.push_back('\0');
        itcp_id_buffer.push_back('\0');
        ASSERT_COND( ::write(network->fd, itcp_id_buffer.c_str(), itcp_id_buffer.size()) == (ssize_t)itcp_id_buffer.size() );

        uninstall_zig_for_this_thread(SIGUSR1);
        SYNCEXEC(args, network->threadid = old_tid);

        ::fdwriter<T>(inq, args);
    }
    else
        ::fdwriter<T>(inq, args);
    // We're definitely not going to block on any fd anymore so make rly
    // sure we're not receiving signals no more
    SYNCEXEC(args, delete network->threadid; network->threadid = 0);

    network->finished = true;

    // update submode flags
    RTEEXEC(*network->rteptr, 
            network->rteptr->transfersubmode.clr( connected_flag ) );
}


// For each distinctive tag we keep a queue and a synctype
template <typename T>
struct dst_state_type {
    ::pthread_mutex_t        mtx;
    ::pthread_cond_t         cond;
    bqueue<T>*               actual_q_ptr;
    inq_type<T>*             iq_ptr;
    outq_type<T>*            oq_ptr;
    sync_type<fdreaderargs>* st_ptr;

    dst_state_type( unsigned int qd ) :
        actual_q_ptr( new bqueue<T>(qd) ),
        iq_ptr( new inq_type<T>(actual_q_ptr) ),
        oq_ptr( new outq_type<T>(actual_q_ptr) ),
        st_ptr( new sync_type<fdreaderargs>(&cond, &mtx) ) {
            PTHREAD_CALL( ::pthread_mutex_init(&mtx, 0) );
            PTHREAD_CALL( ::pthread_cond_init(&cond, 0) );
        }
    ~dst_state_type() THROWS(pthreadexception) {
        delete st_ptr;
        delete iq_ptr;
        delete oq_ptr;
        delete actual_q_ptr;
        PTHREAD_CALL( ::pthread_cond_destroy(&cond) );
        PTHREAD_CALL( ::pthread_mutex_destroy(&mtx) );
    }
};

// Change the free standing function templates into templated 
// functors - this allows us to use them as template argument
//
// At the moment we wrap them - we might move the implementations
// into the functors too
template <typename T>
struct netwriterfunctor {
    static void* f(void* dst_state_ptr) {
        dst_state_type<T>*  dst_state = (dst_state_type<T>*)dst_state_ptr;
        DEBUG(0, "netwriterfunctor[fd=" << dst_state->st_ptr->userdata->fd << "]" << std::endl);
        try {
            ::netwriter<T>(dst_state->iq_ptr, dst_state->st_ptr);
        }
        catch( const std::exception& e ) {
            DEBUG(0, "netwriterfunctor: netwriter threw up - " << e.what() << std::endl);
        }
        catch( ... ) {
            DEBUG(0, "netwriterfunctor: netwriter threw unknown exception" << std::endl);
        }
        // If we're done, disable our queue such that upchain
        // get's informed that WE aren't lissning anymore
        dst_state->actual_q_ptr->disable();
        return (void*)0;
    }
};

template <typename T>
struct fdwriterfunctor {
    static void* f(void* dst_state_ptr) {
        dst_state_type<T>*  dst_state = (dst_state_type<T>*)dst_state_ptr;
        DEBUG(0, "fdwriterfunctor[fd=" << dst_state->st_ptr->userdata->fd << "]" << std::endl);

        try {
            ::fdwriter<T>(dst_state->iq_ptr, dst_state->st_ptr);
        }
        catch( const std::exception& e ) {
            DEBUG(0, "fdwriterfunctor: fdwriter threw up - " << e.what() << std::endl);
        }
        catch( ... ) {
            DEBUG(0, "fdwriterfunctor: fdwriter threw unknown exception" << std::endl);
        }
        // If we're done, disable our queue such that upchain
        // get's informed that WE aren't lissning anymore
        dst_state->actual_q_ptr->disable();
        return (void*)0;
    }
};

template <typename T>
struct vtpwriterfunctor {
    static void* f(void* dst_state_ptr) {
        dst_state_type<T>*  dst_state = (dst_state_type<T>*)dst_state_ptr;
        DEBUG(0, "vtpwriterfunctor[fd=" << dst_state->st_ptr->userdata->fd << "]" << std::endl);
        try {
            ::vtpwriter<T>(dst_state->iq_ptr, dst_state->st_ptr);
        }
        catch( const std::exception& e ) {
            DEBUG(0, "vtpwriterfunctor: vtpwriter threw up - " << e.what() << std::endl);
        }
        catch( ... ) {
            DEBUG(0, "vtpwriterfunctor: vtpwriter threw unknown exception" << std::endl);
        }
        // If we're done, disable our queue such that upchain
        // get's informed that WE aren't lissning anymore
        dst_state->actual_q_ptr->disable();
        return (void*)0;
    }
};
/*
template <typename T>
struct udtwriterfunctor {
    static void* f(void* dst_state_ptr) {
        dst_state_type<T>*  dst_state = (dst_state_type<T>*)dst_state_ptr;
        DEBUG(0, "udtwriterfunctor[fd=" << dst_state->st_ptr->userdata->fd << "]" << std::endl);
        try {
            ::udtwriter<T>(dst_state->iq_ptr, dst_state->st_ptr);
        }
        catch( const std::exception& e ) {
            DEBUG(0, "udtwriterfunctor: udtwriter threw up - " << e.what() << std::endl);
        }
        catch( ... ) {
            DEBUG(0, "udtwriterfunctor: udtwriter threw unknown exception" << std::endl);
        }
        // If we're done, disable our queue such that upchain
        // get's informed that WE aren't lissning anymore
        dst_state->actual_q_ptr->disable();
        return (void*)0;
    }
};
*/
// use any of the above functors as 2nd template argument
template <typename T, template <typename U> class functor>
void multiwriter( inq_type<tagged<T> >* inq, sync_type<multifdargs>* args) {
    typedef std::map<int, pthread_t>                   fd_thread_map_type;
    typedef std::map<int, dst_state_type<T>*>          fd_state_map_type;
    typedef std::map<unsigned int, dst_state_type<T>*> tag_state_map_type;

    tagged<T>               tb;
    const std::string       proto( args->userdata->netparms.get_protocol() );
    fd_state_map_type       fd_state_map;
    fd_thread_map_type      fd_thread_map;
    tag_state_map_type      tag_state_map;
    const dest_fd_map_type& dst_fd_map( args->userdata->dstfdmap );

    // Make sure there are filedescriptors to write to
    ASSERT2_COND(dst_fd_map.size()>0, SCINFO("There are no destinations to send to"));

    DEBUG(2, "[" << ::pthread_self() << "] " << "multiwriter starting" << std::endl);

    // So, for each destination in the destination-to-filedescriptor mapping
    // we check if we already have a netwriterthread for that
    // filedescriptor. If not, add one.
    for( dest_fd_map_type::const_iterator cd=dst_fd_map.begin();
         cd!=dst_fd_map.end();
         cd++ ) {
            // Check if we have the fd for the current destination
            // (cd->second) already in our map
            //
            // In the end we must add an entry of current destination's tag
            // (cd->first) to the actual netwriter-state ('dst_state_type*')
            // so the code can put the data-to-send in the appropriate
            // queuek
            //
            // we have "tag -> fd"  (dstfdmap)
            // we must have "tag -> dst_stat_type*"   (tag_state_map)
            // with the constraint
            // "fd -> dst_stat_type*" == unique       (fd_state_map)

            // Look for "fd -> dst_stat_type*" for the current fd
            typename fd_state_map_type::iterator   fdstate = fd_state_map.find( cd->second );

            if( fdstate==fd_state_map.end() ) {
                // Bollox. Wasn't there yet! We must create a state + thread
                // for the current destination
                fdreaderargs*                                         userdata = 0;
                std::pair<typename fd_state_map_type::iterator, bool> insres;

                insres = fd_state_map.insert( std::make_pair(cd->second, new dst_state_type<T>(10)) );
                ASSERT2_COND(insres.second, SCINFO("Failed to insert fd->dst_state_type* entry into map"));

                // insres->first is 'pointer to fd_state_map iterator'
                // ie a pair<filedescriptor, dst_state_type*>. We need to
                // 'clobber' ("fill in") the details of the freshly inserted
                // dst_state_type
                dst_state_type<T>*  stateptr = insres.first->second;

                // fill in the synctype (which is a "fdreaderargs")
                // We have all the necessary info here (eg the 'fd')
                userdata = new fdreaderargs();

                userdata->fd        = cd->second;
                userdata->rteptr    = args->userdata->rteptr;
                userdata->doaccept  = (proto=="rtcp") /* - would require support in multiopener as well! 
                                                           HV: 03-Dec-2013 multiopener has it now */;

                // copy pointer to the state for this thread
                stateptr->st_ptr->userdata = userdata;
                stateptr->st_ptr->setqdepth(10);
                stateptr->st_ptr->setstepid(args->stepid);

                // allocate a threadid
                pthread_t   tmp_threadid;

                // enable the queue
                stateptr->actual_q_ptr->enable();

                // Add the freshly constructed 'fdreaderargs' entry to the list-of-fdreaders so
                // 'multicloser()' can do its thing, when needed.
                // The 'fdreaderargs' is the "user_data" for the
                // fdreader/writer threadfunctions
                args->userdata->fdreaders.push_back( userdata );

                // and after starting the thread insert the entry in our fd -> thread mapping
                PTHREAD_CALL( ::pthread_create(&tmp_threadid, 0, &functor<T>::f, (void*)stateptr) );
                fd_thread_map.insert( std::make_pair(cd->second, tmp_threadid) );

                // Now the pointer to the entry is filled in and it can
                // double as if it was the searchresult in the first place,
                // as if the entry had existed
                fdstate = insres.first;
            }

            // We know that fdstat points at a pair <fd, dst_state_type*>
            // All we have to do is create an entry <tag, dst_state_type*>
            ASSERT_COND( tag_state_map.insert(std::make_pair(cd->first, fdstate->second)).second );
    }

    // We have now spawned a number of threads: one per destination
    // We pop everything from our inputqueue
    while( inq->pop(tb) ) {
        // for each tagged block find out where it has to go
        typename tag_state_map_type::const_iterator curdest = tag_state_map.find(tb.tag);

        // Unconfigured destination gets ignored silently
        if( curdest==tag_state_map.end() )
            continue;

        // Configured destination that we fail to send to = AAARGH!
        if( curdest->second->oq_ptr->push(tb.item)==false ) {
            DEBUG(-1, "multiwriter: tag " << tb.tag << " failed to push block to it!" << std::endl);
            break;
        }
    }
    // Start signalling spawned threads that it's time to stop
    for( typename fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            // set cancel to true & condition signal
            cd->second->st_ptr->lock();
            cd->second->st_ptr->setcancel(true);
            PTHREAD_CALL( ::pthread_cond_broadcast(&cd->second->cond) );
            cd->second->st_ptr->unlock();

            // disable queue
            cd->second->actual_q_ptr->delayed_disable();
    }
    for( typename fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            // For each fd we started a thread - look up the threadid
            fd_thread_map_type::iterator  tidptr = fd_thread_map.find( cd->first );

            if( tidptr==fd_thread_map.end() ) {
                DEBUG(-1, "[" << ::pthread_self() << "] multiwriter ZOMG! no pthread_t for fd[" << cd->first << "] !?" << std::endl);
                continue;
            }
            // Now we can join
            PTHREAD_CALL( ::pthread_join(tidptr->second, 0) );

            // only now it's safe to delete resources
            delete cd->second;
    }
    DEBUG(2, "[" << ::pthread_self() << "] " << "multiwriter: done" << std::endl);
}

#endif
