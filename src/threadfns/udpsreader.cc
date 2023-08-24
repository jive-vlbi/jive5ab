// implementation of the udpsreader(s)
// Copyright (C) 2007-2023 Marjolein Verkouter
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
// Author:  Marjolein Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <threadfns/udpsreader.h>

#include <list>
#include <string>
#include <sstream>
/////////
///// Two-step UDPs reader. Makes sure that memory is touched only once
////  wether or not a packet is received or not
////
////  The bottom half grabs memory and puts packets in order and sets
////  a flag if it has written a position.
////  The top half goes over the array of flags and writes fill pattern +
////  zeroes in the packet positions that have no data
////
////  On that last subject: if we're doing compressed UDP traffic,
////  we must pre-expand each read packet from "wr_size" => "rd_size",
////  filling up the difference with zeroes.
////
////  This step needs to be done always. The fill pattern now, that is
////  different, that only needs to be done when the packet didn't arrive.
////  
////  Theoretically it would be nicer to do the zeroeing in the
////  "bottom half" but I _really_ want that routine to be as optimal
////  as can be, given the work it _already_ has to do. 
////  Rather, in the "top half". There's two top halves - one with
////  zeroeing and one without. There is some duplicated code but
////  the result is optimal for each case - within the high-performance
////  loop (looping over each packet) not a single decision needs to be
////  made. [in vlbi_streamer mode we have block sizes of 256/512 MByte,
////  i.e. ~30,000 to 60,000 packets and thus an equal amount
////  of decisions can be skipped.
//// 
////////


// The bottom half
void udpsreader_bh(outq_type<block>* outq, sync_type< sync_type<fdreaderargs> >* argsargs) {
    int                       lastack, oldack;
    bool                      stop;
    ssize_t                   r;
    uint64_t                  seqnr, seqoff, pktidx;
    uint64_t                  firstseqnr  = 0;
    uint64_t                  expectseqnr = 0;
    runtime*                  rteptr = 0;
    socklen_t                 slen( sizeof(struct sockaddr_in) );
    unsigned int              ack = 0;
    fdreaderargs*             network = 0;
    struct sockaddr_in        sender;
    sync_type<fdreaderargs>*  args = argsargs->userdata;
    static std::string        acks[] = {"xhg", "xybbgmnx",
                                        "xyreryvwre", "tbqireqbzzr",
                                        "obxxryhy", "rvxryovwgre",
                                        "qebrsgbrgre", "" /* leave empty string last!*/};
    circular_buffer<uint64_t> psn( 32 ); // keep the last 32 sequence numbers

    SYNCEXEC(args, network = args->userdata; rteptr = (network) ? network->rteptr : 0;);
    EZASSERT2(network && rteptr, netreaderexception, EZINFO("at least one of the pointer arguments was NULL"));

    // an (optionally compressed) block of <blocksize> is chopped up in
    // chunks of <read_size>, optionally compressed into <write_size> and
    // then put on the network.
    // We reverse this by reading <write_size> from the network into blocks
    // of size <read_size> and fill up a block of size <blocksize> before
    // handing it down the processing chain.
    // [note: no compression => write_size==read_size, ie this scheme will always work]
    unsigned char*               dummybuf = new unsigned char[ 65536 ]; // max size of a datagram 2^16 bytes

    // HV: 13-11-2013 Blocks larger than this size we call "insensible" and we 
    //                change the readahead + blockpool allocation scheme
    //                because we might be doing vlbi_streamer style
    const unsigned int           sensible_blocksize( 32*1024*1024 );
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];

    // HV: 13-11-2013 Support for vlbi streamer mode means that blocksize
    //                could be 256MB - 512MB / block. In such cases the
    //                readahead should be 1.
    //                Below we should take care of the blockpool allocation
    //                too - cannot ask for 16 blocks at a time :D
    const unsigned int           readahead = (blocksize>=sensible_blocksize)?2:network->netparms.nblock;

    // We tag the flags at the end of the block, one unsigned char/datagram
    unsigned char                dummyflag;
    unsigned char*               flagptr;
    const unsigned int           n_dg_p_block = blocksize/wr_size;
    
    // We need some temporary blocks:
    //   * an array of blocks, our workbuf. we keep writing packets
    //     in there until we receive a sequencenumber that would 
    //     force us to write outside the readahead buffer. then
    //     we start to release blocks until the packet CAN be
    //     written into the workbuf
    block*                       workbuf = new block[ readahead ];

    // Create a blockpool. If we need blocks we take'm from there
    // HV: 13-11-2013 If blocksize seems too large, do not allocate
    //                more than two (2) chunks in one go
    const unsigned int  nb = (blocksize<sensible_blocksize?32:2);

    // Before doing anything, make sure that *we* are the one being woken
    // if something happens on the file descriptor that we're supposed to
    // be sucking empty!
    //
    // 21-Jan-2014 HV: Note to self ... this whole thread signalling thing
    //                 only works if one actually installs a signal handler
    //                 for the thread one desires to be woken up should it be
    //                 signalled!
    install_zig_for_this_thread(SIGUSR1);
    SYNCEXEC(args,
             delete network->threadid;
             delete network->pool;
             network->threadid = new pthread_t( ::pthread_self() );
             network->pool = new blockpool_type(blocksize + n_dg_p_block*sizeof(unsigned char), nb));

    // If blocksize > sensible block size start to pre-allocate!
    if( blocksize>=sensible_blocksize ) {
        std::list<block>        bl;
        const unsigned int npre = network->netparms.nblock;
        DEBUG(4, "udpsreader_bh: start pre-allocating " << npre << " blocks" << std::endl);
        for(unsigned int i=0; i<npre; i++)
            bl.push_back( network->pool->get() );
        DEBUG(4, "udpsreader_bh: ok, done that!" << std::endl);
    }

    // Set up the message - a lot of these fields have known & constant values
    struct iovec    iov[2];
    struct msghdr   msg;

    msg.msg_name       = 0;
    msg.msg_namelen    = 0;

    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // message 'msg': two fragments. Sequence number and datapart
    msg.msg_iov        = &iov[0];

    // The iov_len's will be filled in differently between
    // the PEEK phase and the WAITALL phase
    msg.msg_iovlen     = 0;

    // The size of the parts of the message are known
    // and for the sequencenumber, we already know the destination address
    iov[0].iov_base    = &seqnr;
    iov[0].iov_len     = sizeof(seqnr);
    iov[1].iov_len     = rd_size;

    // Here we fix the amount of iov's and lengths of the
    // messages for the two phases: PEEK and WAITALL
    // We should be safe for datagrams up to 2G i hope
    //   (the casts to 'int' from iov[..].iov_len because
    //    the .iov_len is-an unsigned)
    const int               npeek       = 1;
    const int               peekread    = (int)(iov[0].iov_len);
    const int               nwaitall    = 2;
    const int               waitallread = (int)(iov[0].iov_len + iov[1].iov_len);

    // reset statistics/chain and statistics/evlbi
    RTE3EXEC(*rteptr,
            rteptr->evlbi_stats[ network->tag ] = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdpsReadBH"),
            delete [] dummybuf; delete [] workbuf; delete network->threadid; network->threadid = 0);

    // Great. We're done setting up. Now let's see if we weren't cancelled
    // by any chance
    SYNCEXEC(args, stop = args->cancelled);

    if( stop ) {
        delete [] dummybuf;
        delete [] workbuf;
        SYNCEXEC(args, delete network->threadid; network->threadid = 0);
        DEBUG(0, "udpsreader_bh: cancelled before actual start" << std::endl);
        return;
    }

    // No, we weren't. Now go into our mainloop!
    DEBUG(0, "udpsreader_bh: fd=" << network->fd << " data:" << iov[1].iov_len
            << " total:" << waitallread << " readahead:" << readahead
            << " pkts:" << n_dg_p_block * readahead
            << " avbs: " << network->allow_variable_block_size
            << std::endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&   loscnt( rteptr->evlbi_stats[network->tag].pkt_lost );
    ucounter_type&   pktcnt( rteptr->evlbi_stats[network->tag].pkt_in );
    ucounter_type&   ooocnt( rteptr->evlbi_stats[network->tag].pkt_ooo );
    ucounter_type&   disccnt( rteptr->evlbi_stats[network->tag].pkt_disc );
    ucounter_type&   ooosum( rteptr->evlbi_stats[network->tag].ooosum );

    // inner loop variables
    bool           done;
    bool           discard;
    bool           resync, OHNOES;
    void*          location;
    uint64_t       blockidx;
    uint64_t       maxseq, minseq;
    unsigned int   shiftcount;
    netparms_type& np( network->rteptr->netparms );

    // Our loop can be much cleaner if we wait here to receive the 
    // very first sequencenumber. We make that our current
    // first sequencenumber and then we can _finally_ drop
    // into our real readloop
    //
    // HV: 9 Jul 2012 - in order to prevent flooding we will
    //     have to send UDP backtraffic every now and then
    //     (such that network equipment between the scope and
    //     us does not forget our ARP entry). So we peek at the
    //     sequencenumber and at the same time record who's sending
    //     to us
#if 0
    ssize_t   n_read;
    while( (n_read=::recvfrom(network->fd, &seqnr, sizeof(seqnr), MSG_PEEK, (struct sockaddr*)&sender, &slen))!=sizeof(seqnr) ) {
DEBUG(-1,"udpsreader_bh: n_read=" << n_read << std::endl);
        ASSERT2_POS(n_read,
                   SCINFO("udpsreader_bh: cancelled before beginning");
                   delete[] dummybuf; delete[] workbuf; SYNCEXEC(args, delete network->threadid; network->threadid = 0));
    }
#endif
#if 1
    ssize_t nrec;
    if( (nrec=::recvfrom(network->fd, &seqnr, sizeof(seqnr), MSG_PEEK|MSG_WAITALL, (struct sockaddr*)&sender, &slen))!=sizeof(seqnr) ) {
        delete [] dummybuf;
        delete [] workbuf;
        SYNCEXEC(args, delete network->threadid; network->threadid = 0);
        DEBUG(-1, "udpsreader_bh: cancelled waiting for first frame " << "nrec:" << nrec << " (ask:" << sizeof(seqnr) << ")" << std::endl);
        return;
    }
#endif
    lastack = 0;                    // trigger immediate ack send
    oldack  = netparms_type::defACK;// will be updated if value changed from default

#ifdef FILA
// FiLa10G only sends 32bits of sequence number
seqnr = (uint64_t)(*((uint32_t*)(((unsigned char*)iov[0].iov_base)+4)));
#endif

    maxseq = minseq = expectseqnr = firstseqnr = seqnr;

    DEBUG(0, "udpsreader_bh: first sequencenr# " << firstseqnr << " from " <<
              inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << std::endl);

    // Drop into our tight inner loop
    done = false;
    do {
        // When receiving FiLa10G data across scan boundaries the 
        // sequence number will drop back to 0. So we're going to
        // build a heuristic that sais: if we receive a sequence number
        // that should be in the previous block (one before the current
        // read-ahead block array) we're going to say: that one came in too
        // late, sorry about that and discard it. 
        // If the sequence number is (way) before that, then we're going to
        // say: ok, the sender has started a new sequence of sequence
        // numbers and we're going to re-syncronize to that.
        OHNOES    = (seqnr<firstseqnr);
        discard   = (OHNOES && (firstseqnr-seqnr)<=n_dg_p_block);
        resync    = (OHNOES && !discard);
        location  = (discard?dummybuf:0);
        flagptr   = (discard?&dummyflag:0);

        // Ok, we have read another sequencenumber.
        // First up: some statistics?
        pktcnt++;

        // Statistics as per RFC4737. Not all of them,
        // and one or two slighty adapted.
        // In order to do the accounting as per the RFC
        // we should remember all sequence numbers.
        // We could keep, say, the last 100 but a lot of
        // linear searching is required to do the statistics
        // correctly. For now skip that.
        psn.push( seqnr );

        // Count sequence discontinuity (RFC/3.4) and
        // an approximation of the reordering extent (RFC/4.2.2).
        // The actual definition in 4.2.2 is more complex than
        // what we do but we save a linear search this way.
        // Also sum the gap between discontinuities (4.5.4).
        // The gap is the distance, in units of packets,
        // since the last seen discontinuity.
        if( seqnr>=expectseqnr ) {
            // update next expected seqnr
            expectseqnr = seqnr+1;
        } else {
            int       j = 0;
            const int npsn = (int)psn.size(); // do not buffer > 2.1G psn's ...

            // this is a reordering
            ooocnt++;

            // Compute the reordering extent as per RFC4737,
            // provided that we only look at the last N seq. nrs.
            // (see declaration of the circular buffer)
            // We must look at the first sequence number received
            // that has a sequence number larger than the reordered one
            while( j<npsn && psn[j]<seqnr )
                j++;
            ooosum += (uint64_t)( npsn - j );
        }

        // If we need to do a re-sync, we need to re-start some of our
        // statistics before we actually start to analyze them
        if( resync ) {
            const uint64_t  old_disccnt = disccnt;

            // The sequence number of the current packet will become 
            // the new 'firstseqnr'
            maxseq = minseq = expectseqnr = firstseqnr = seqnr;

            // In order to keep the bookkeeping sensible, we have to restart 
            // counting packets ... (We already have 1 packet - the one 
            // we're currently looking at!)
            pktcnt = 1;

            // Also clear the psn buffer - all reorderings &cet need to be
            // reset
            psn.clear();

            // We're going to throw away all data received so far
            // Do that by just resetting the flags and not 
            // touch the actual packet memory(*). We're going to count these
            // lost packets as discarded.
            // (*) the top-half of this mini chain will do the zeroeing
            //     for us, based on the flags
            for(blockidx=0; blockidx<readahead; blockidx++)
                if( workbuf[blockidx].empty()==false )
                    for(pktidx=0, flagptr=(((unsigned char*)workbuf[blockidx].iov_base) + blocksize);
                        pktidx<n_dg_p_block;
                        pktidx++, flagptr++)
                            if( *flagptr ) disccnt++, *flagptr=0;
            DEBUG(-1, "udpsreader_bh: resynced data stream! " << disccnt-old_disccnt << " packets discarded" << std::endl);
        }

        // More statistics ...
        if( discard )
            disccnt++;
        if( seqnr>maxseq )
            maxseq = seqnr;
        else if( seqnr<minseq )
            minseq = seqnr;
        // we can now be doing multiple streams in parallel
        loscnt = (maxseq - minseq + 1 - pktcnt);

        // Now we need to find out where to put the data for it!
        // that is, if the packet is not to be discarded
        // [if location is already set it is the dummybuf, ie discardage]
        // NOTE: blockidx can never become <0 since we've already
        //       checked that seqnr >= firstseqnr
        //       (a condition signalled by location==0 since
        //        location!=0 => seqnr < firstseqnr)
        shiftcount = 0;
        while( location==0 ) {
            seqoff   = seqnr - firstseqnr;
            blockidx = seqoff/n_dg_p_block;

            if( blockidx<readahead ) {
                pktidx = seqoff%n_dg_p_block;

                // ok we know in which block to put our datagram
                // make sure the block is non-empty
                if( workbuf[blockidx].empty() ) {
                    workbuf[blockidx] = network->pool->get();
                    // set all flags to 0 - no pkts in buffer yet
                    ::memset((unsigned char*)workbuf[blockidx].iov_base + blocksize, 0x0, n_dg_p_block);
                }
                // compute location inside block
                location = (unsigned char*)workbuf[blockidx].iov_base + pktidx*wr_size;
                flagptr  = (unsigned char*)workbuf[blockidx].iov_base + blocksize + pktidx;
                break;
            } 
            // Crap. sequence number would fall outside workbuf!

            // Release the first block in our workbuf.
            if( !workbuf[0].empty() )
                if( outq->push(workbuf[0])==false )
                    break;

            // Then shift all blocks down by one,
            for(unsigned int i=1; i<readahead; i++)
                workbuf[i-1] = workbuf[i];

            // do not forget to clear the last position
            workbuf[(readahead-1)] = block();

            // Update loopvariables
            firstseqnr += n_dg_p_block;
            if( ++shiftcount==readahead ) {
                DEBUG(0, "udpsreader_bh: detected jump > readahead, " << (seqnr - firstseqnr) << " datagrams" << std::endl);
                firstseqnr = seqnr;
            }
        }

        // If location STILL is 0 then there's no point
        // in going on
        if( location==0 )
            break;

        // Our primary computations have been done and, what's most
        // important, a location for the packet has been decided upon
        // Read the pakkit into our mem'ry space before we do anything else
        msg.msg_iovlen  = nwaitall;
        iov[1].iov_base = location;
        if( (r=::recvmsg(network->fd, &msg, MSG_WAITALL))!=(ssize_t)waitallread ) {
            lastsyserror_type  lse;
            std::ostringstream oss;

            // Fix 1. Allow partial blocks to be sent downstream. In fact,
            //        push all blocks from our workbuff that contain data
            //        Full blocks [we check 'maxseqnr' for that] are pushed
            //        always, partial blocks only if the
            //        'allow_variable_block_size' is set
            for(uint64_t i=0, blockseqnstart=firstseqnr; i<readahead && blockseqnstart<=maxseq; i++, blockseqnstart+=n_dg_p_block) {
                const unsigned int sz = wr_size * (unsigned int)std::min(maxseq + 1 - blockseqnstart, (uint64_t)n_dg_p_block);

                if( sz==blocksize || network->allow_variable_block_size )
                    if( (done=(outq->push(workbuf[i].sub(0, sz))==false))==true )
                        break;
            }
            // 1a.) Remove ourselves from the environment - our thread is going
            // to be dead!
            //
            //     28Aug2015: I did some follow up. Apparently the Linux
            //                b*stards interpret the POSIX standards
            //                somewhat differently - to the point where they
            //                feel that glibc's pthread_kill() on a thread-id that
            //                referred to a valid thread but which is not
            //                alive any more yields a SEGFAULT is
            //                acceptable. Cf:
            //                https://sourceware.org/bugzilla/show_bug.cgi?id=4509
            //                Un-fucking-believable.
            SYNCEXEC(args, delete network->threadid; network->threadid = 0);
            // Fix 2. Do not throw on EINTR or EBADF; that is the normal way to
            //        terminate the reader and should not warrant an
            //        exception
            if( lse.sys_errno==EINTR || lse.sys_errno==EBADF )
                break;
            // Wasn't EINTR/EBADF so now we must throw. Prepare stuff before
            // actually doing that
            // 2.) delete local buffers. In c++11 using unique_ptr this
            // wouldnae be necessary
            delete [] dummybuf;
            delete [] workbuf;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - [" << lse << "] (ask:" << waitallread << " got:" << r << ")";
            throw syscallexception(oss.str());
        }

        // Now that we've *really* read the pakkit we may update our
        // read statistics 
        *flagptr       = 1;
        counter       += waitallread;

        // Acknowledgement processing:
        // Send out an ack before we go into infinite wait
        if( np.ackPeriod!=oldack ) {
            // someone set a different acknowledgement period
            // let's trigger immediate send + restart with current ACK period
            lastack = 0;
            oldack  = np.ackPeriod;
            DEBUG(2, "udpsreader_bh: switch to ACK every " << oldack << "th packet" << std::endl);
        }
        if( lastack<=0 ) {
            if( acks[ack].empty() )
                ack = 0;
            // Only warn if we fail to send. Try again in two minutes
            if( ::sendto(network->fd, acks[ack].c_str(), acks[ack].size(), 0,
                         (const struct sockaddr*)&sender, sizeof(struct sockaddr_in))==-1 )
                DEBUG(-1, "udpsreader_bh: WARN failed to send ACK back to sender" << std::endl);
            lastack = oldack;
            ack++;
        } else {
            // lower ACK counter 
            lastack--;
        }

        // Wait for another pakkit to come in. 
        // When it does, take a peak at the sequencenr
        msg.msg_iovlen = npeek;
        if( (r=::recvmsg(network->fd, &msg, MSG_PEEK))!=peekread ) {
            lastsyserror_type  lse;
            std::ostringstream oss;

            // Fix 1. Allow partial blocks to be sent downstream. In fact,
            //        push all blocks from our workbuff that contain data
            //        Full blocks [we check 'maxseqnr' for that] are pushed
            //        always, partial blocks only if the
            //        'allow_variable_block_size' is set
            for(uint64_t i=0, blockseqnstart=firstseqnr; i<readahead && blockseqnstart<=maxseq; i++, blockseqnstart+=n_dg_p_block) {
                const unsigned int sz = wr_size * (unsigned int)std::min(maxseq + 1 - blockseqnstart, (uint64_t)n_dg_p_block);

                if( sz==blocksize || network->allow_variable_block_size )
                    if( (done=(outq->push(workbuf[i].sub(0, sz))==false))==true )
                        break;
            }
            // 1a.) Remove ourselves from the environment - our thread is going
            // to be dead!
            //     28Aug2015: I did some follow up. Apparently the Linux
            //                b*stards interpret the POSIX standards
            //                somewhat differently - to the point where they
            //                feel that glibc's pthread_kill() on a thread-id that
            //                referred to a valid thread but which is not
            //                alive any more yields a SEGFAULT is
            //                acceptable. Cf:
            //                https://sourceware.org/bugzilla/show_bug.cgi?id=4509
            //                Un-fucking-believable.
            SYNCEXEC(args, delete network->threadid; network->threadid = 0);
            // Fix 2. Do not throw on EINTR or EBADF; that is the normal way to
            //        terminate the reader and should not warrant an
            //        exception
            if( lse.sys_errno==EINTR || lse.sys_errno==EBADF )
                break;
            // Wasn't EINTR/EBADF so now we must throw. Prepare stuff before
            // actually doing that
            // 2.) delete local buffers. In c++11 using unique_ptr this
            // wouldnae be necessary
            delete [] dummybuf;
            delete [] workbuf;
            oss << "::recvmsg(network->fd, &msg, MSG_PEEK) fails - [" << lse << "] (ask:" << peekread << " got:" << r << ")";;
            throw syscallexception(oss.str());
        }
#ifdef FILA
// FiLa10G only sends 32bits of sequence number
seqnr = (uint64_t)(*((uint32_t*)(((unsigned char*)iov[0].iov_base)+4)));
#endif
    } while( !done );

    // Clean up
    delete [] dummybuf;
    delete [] workbuf;
    SYNCEXEC(args, delete network->threadid; network->threadid = 0);
    DEBUG(0, "udpsreader_bh: stopping" << std::endl);
}


// In this top half there will be no zeroes; read_size == write_size
void udpsreader_th_nonzeroeing(inq_type<block>* inq, outq_type<block>* outq, sync_type<runtime*>* args) {
    runtime*           rteptr    = *(args->userdata);
    const unsigned int wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int blocksize = rteptr->sizes[constraints::blocksize];

    // Cache ANYTHING that is known & constant.
    // If a value MUST be constant, then MAKE IT SO.
    const unsigned int n_dg_p_block   = blocksize/wr_size;

    //   * one prototype block with fillpattern + zeroes etc
    //     in the right places to initialize a freshly allocated
    //     block with
    block                   b;
    unsigned char*          fpblock = new unsigned char[ wr_size ];
    unsigned char*          flagptr;
    unsigned char*          dataptr;
    
    // Fill the fillpattern block with fillpattern
    {
        uint64_t*      dgptr   = (uint64_t*)fpblock;
        const uint64_t fillpat = ((uint64_t)0x11223344 << 32) + 0x11223344;

        for(unsigned int i=wr_size, j=0; i>=sizeof(uint64_t); i-=sizeof(uint64_t), j++)
            dgptr[j] = fillpat;

        // If we're recording VDIF, we overwrite the first 16 bytes
        // with a valid 'invalid' VDIF header. Only the frame size
        // and valid flag will contain useful information
        if( is_vdif(rteptr->trackformat()) ) {
            vdif_header*  vdifh = (vdif_header*)fpblock;

            ::memset(fpblock, 0x0, sizeof(vdif_header));
            vdifh->invalid         = 1;
            vdifh->data_frame_len8 = (wr_size / 8);

            DEBUG(-1, "udpsreader_th_nonzeroeing: detected VTP, 'invalid' marked VDIF frame replaces missing data" << std::endl);
        }
    }

    // Ok, fall into our main loop
    DEBUG(0, "udpsreader_th_nonzeroeing/starting " << std::endl);
    while( inq->pop(b) ) {
        dataptr = (unsigned char*)b.iov_base;
        flagptr = dataptr + blocksize;

        for(unsigned int i=0; i<n_dg_p_block; i++, dataptr+=wr_size)
            if( flagptr[i]==0 )
                ::memcpy(dataptr, fpblock, wr_size);
        if( outq->push(b.sub(0, std::min(blocksize, (unsigned int)b.iov_len)))==false )
            break;

        b = block();
    }
    DEBUG(0, "udpsreader_th_nonzeroeing/done " << std::endl);
    delete [] fpblock;
}

// Deal with the case where rd_size != wrsize.
//
// This is the top half which deals with correctly writing fill pattern in
// the part of the packet that was read from the network and appends zeroes
// (there are zeroes to be added or else you wouldn't be in THIS top half
// d'oh) in the remainder.
// The reason that there should be zeroes there, is because a following
// decompression step(*), which expands the compressed packet back to
// full "write_size" size, can only move bits around based on the 
// assumption they are initialized with zero; the bit movement is done
// by bitwise OR'ing "bit" with the destination bit: bit OR dest.
// If "dest" == 0 this reads: "bit" OR 0, which => result = "bit"
// (i.e. the value of "bit" has been moved to "dest").
// If "dest" == 1 (which could be the case if there's fill pattern there)
// the OR would read: "bit" OR 1 which would _always_ evaluate to "1",
// i.e. if "bit" happened to be zero, it would not be "moved" succesfully.
//
// (*) the fact that wr_size != rd_size is _because_ compression was in
//     effect on the sending side!
void udpsreader_th_zeroeing(inq_type<block>* inq, outq_type<block>* outq, sync_type<runtime*>* args) {
    runtime*           rteptr  = *(args->userdata);
    const unsigned int rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int blocksize = rteptr->sizes[constraints::blocksize];

    // Cache ANYTHING that is known & constant.
    // If a value MUST be constant, then MAKE IT SO.
    const unsigned int n_dg_p_block   = blocksize/wr_size;

    //   * one prototype block with fillpattern + zeroes etc
    //     in the right places to initialize a freshly allocated
    //     block with
    block                   b;
    const size_t            nzeroes = (wr_size - rd_size);
    unsigned char*          fpblock = new unsigned char[ rd_size ];
    unsigned char*          zeroes  = (unsigned char*)::calloc(nzeroes, 1);
    unsigned char*          flagptr;
    unsigned char*          dataptr;

    // Fill the fillpattern block with fillpattern
    // HV: 19May2011 - this is not quite correct at ALL!
    //     We must initialize the positions of the datagrams
    //     with fillpattern. If there is excess space,
    //     eg when reading compressed data: then we allocate
    //     space for the decompressed data but we overwrite only 
    //     a portion of that memory by the size of compressed
    //     data.
    //     As a result there would be fillpattern in the excess 
    //     space - the decompression could, potentially, leave
    //     parts of the data intact by only modifying the
    //     affected bits ...
    {
        uint64_t*      dgptr   = (uint64_t*)fpblock;
        const uint64_t fillpat = ((uint64_t)0x11223344 << 32) + 0x11223344;

        for(unsigned int i=rd_size, j=0; i>=sizeof(uint64_t); i-=sizeof(uint64_t), j++)
            dgptr[j] = fillpat;

        // If we're recording VDIF, we overwrite the first 16 bytes
        // with a valid 'invalid' VDIF header. Only the frame size
        // and valid flag will contain useful information
        if( is_vdif(rteptr->trackformat()) ) {
            vdif_header*  vdifh = (vdif_header*)fpblock;

            ::memset(fpblock, 0x0, sizeof(vdif_header));
            vdifh->invalid         = 1;
            vdifh->data_frame_len8 = (wr_size / 8);

            DEBUG(-1, "udpsreader_th_zeroeing: detected VTP, 'invalid' marked VDIF frame replaces missing data" << std::endl);
        }
    }

    // Ok, fall into our main loop
    DEBUG(0, "udpsreader_th_zeroeing/starting " << std::endl);
    while( inq->pop(b) ) {
        dataptr = (unsigned char*)b.iov_base;
        flagptr = dataptr + blocksize;

        for(unsigned int i=0; i<n_dg_p_block; i++, dataptr+=wr_size) {
            // do we need to insert fill pattern?
            if( flagptr[i]==0 )
                ::memcpy(dataptr, fpblock, rd_size);
            // append the zeroes
            ::memcpy(dataptr+rd_size, zeroes, nzeroes);
        }
        if( outq->push(b.sub(0, std::min(blocksize, (unsigned int)b.iov_len)))==false )
            break;
        b = block();
    }
    delete [] fpblock;
    ::free( zeroes );
    DEBUG(0, "udpsreader_th_zeroeing/done " << std::endl);
}
