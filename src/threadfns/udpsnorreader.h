// the udp-with-sequence-number-no-reordering reader
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
#ifndef JIVE5A_THREADFNS_UDPSNORREADER_H
#define JIVE5A_THREADFNS_UDPSNORREADER_H

#include <chain.h>
#include <block.h>
#include <threadfns.h>
#include <threadfns/per_sender.h>
#include <threadfns/do_push_block.h>

#include <sstream>
#include <iostream>


template <typename Item>
void udpsnorreader(outq_type<Item>* outq, sync_type<fdreaderargs>* args) {
    uint64_t                  seqnr;
    runtime*                  rteptr = 0;
    unsigned char*            location;
    unsigned char*            block_end;
    fdreaderargs*             network = args->userdata;
    struct sockaddr_in        sender;
    find_by_sender_type       find_by_sender(&sender);
    // Keep pakkit stats per sender. Keep at most 8 unique senders?
    per_sender_type           per_sender[8]; 
    per_sender_type*          curSender;
    unsigned int              nSender = 0;
    const unsigned int        maxSender( sizeof(per_sender)/sizeof(per_sender[0]) );
    per_sender_type*          endSender( &per_sender[0] );

    rteptr = network->rteptr; 

    // an (optionally compressed) block of <blocksize> is chopped up in
    // chunks of <read_size>, optionally compressed into <write_size> and
    // then put on the network.
    // We reverse this by reading <write_size> from the network into blocks
    // of size <read_size> and fill up a block of size <blocksize> before
    // handing it down the processing chain.
    // [note: no compression => write_size==read_size, ie this scheme will always work]

    // HV: 13-11-2013 Blocks larger than this size we call "insensible" and we 
    //                change the readahead + blockpool allocation scheme
    //                because we might be doing vlbi_streamer style
    const unsigned int           sensible_blocksize( 32*1024*1024 );
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];
    const unsigned int           n_dg_p_block = blocksize/wr_size;
    const unsigned int           n_zeroes  = (wr_size - rd_size);
    const unsigned char*         zeroes_p  = (n_zeroes ? new unsigned char[n_zeroes] : 0);
    
    // Create a blockpool. If we need blocks we take'm from there
    // HV: 13-11-2013 If blocksize seems too large, do not allocate
    //                more than two (2) chunks in one go
    const unsigned int           nb = (blocksize<sensible_blocksize?32:2);

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
             network->pool = new blockpool_type(blocksize, nb));

    if( zeroes_p )
        ::memset(const_cast<unsigned char*>(zeroes_p), 0x0, n_zeroes);

    // Set up the message - a lot of these fields have known & constant values
    struct iovec    iov[2];
    struct msghdr   msg;

    // We'd like to know who's sending to us
    msg.msg_name       = (void*)&sender;
    msg.msg_namelen    = sizeof(sender);

    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // The size of the parts of the message are known
    // and for the sequencenumber, we already know the destination address
    iov[0].iov_base    = &seqnr;
    iov[0].iov_len     = sizeof(seqnr);
    iov[1].iov_len     = rd_size;

    // message 'msg': two fragments. Sequence number and datapart
    msg.msg_iovlen     = 2;
    msg.msg_iov        = &iov[0];

    // reset statistics/chain and statistics/evlbi
    RTE3EXEC(*rteptr,
            rteptr->evlbi_stats[ network->tag ] = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdpsNorRead"),
            delete [] zeroes_p; delete network->threadid; network->threadid = 0;);

    // Great. We're done setting up. Now let's see if we weren't cancelled
    // by any chance
    bool   stop;
    SYNCEXEC(args, stop = args->cancelled);

    if( stop ) {
        delete [] zeroes_p;
        SYNCEXEC(args, delete network->threadid; network->threadid = 0);
        DEBUG(0, "udpsnorreader: cancelled before actual start" << std::endl);
        return;
    }

    // No, we weren't. Now go into our mainloop!
    DEBUG(0, "udpsnorreader: fd=" << network->fd << " data:" << iov[1].iov_len
            << " total:" << (iov[0].iov_len + iov[1].iov_len)
            << " pkts:" << n_dg_p_block 
            << " avbs: " << network->allow_variable_block_size
            << std::endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&   loscnt( rteptr->evlbi_stats[ network->tag ].pkt_lost );
    ucounter_type&   pktcnt( rteptr->evlbi_stats[ network->tag ].pkt_in );
//    ucounter_type&   ooocnt( rteptr->evlbi_stats.pkt_ooo );
//    ucounter_type&   ooosum( rteptr->evlbi_stats.ooosum );
//    ucounter_type    tmppkt, tmpooocnt, tmpooosum, tmplos;
    ucounter_type    tmplos;

    // inner loop variables
    block              b = network->pool->get();
    ssize_t            n;
    const ssize_t      waitallread = (ssize_t)(iov[0].iov_len + iov[1].iov_len);
    netparms_type&     np( network->rteptr->netparms );
    unsigned int const tag( network->tag );

    // Initialize the important counters & pointers for first use
    location    = (unsigned char*)b.iov_base;
    block_end   = location + b.iov_len - wr_size; // If location points beyond this we cannot write a packet any more

    // Drop into our tight inner loop
    while( true ) {
        // Wait here for packet
        iov[1].iov_base = location;

        if( (n=::recvmsg(network->fd, &msg, MSG_WAITALL))!=waitallread ) {
            lastsyserror_type  lse;
            std::ostringstream oss;

            // We don't have to check *if* this is a partial block; the
            // fact that we failed to read this packet implies the block is
            // a partial! [even if location pointed at the last packet in
            // the block: it failed to read so the block is not filled :D]

            // Fix 1. Check if we need to & are allowed to send a partial block downstream
            const unsigned int sz = (unsigned int)(location - (unsigned char*)b.iov_base);
            if( network->allow_variable_block_size && sz )
                do_push_block(outq, b.sub(0, sz), tag);
                //outq->push(b.sub(0, sz));

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
            delete [] zeroes_p;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - [" << lse << "] (ask:" << waitallread << " got:" << n << ")";
            throw syscallexception(oss.str());
        }

        // OK. Packet reading succeeded
        counter += waitallread;
        pktcnt++;

        // Write zeroes if necessary
        (void)(n_zeroes && ::memcpy(location+rd_size, zeroes_p, n_zeroes));

        // Compute location for next pakkit.
        // Release previous block if filled up [+get a new one to fill up]
        // Note: we read rd_size and advance by wr_size!
        location += wr_size;
        if( location>block_end ) {
            //if( outq->push(b)==false )
            if( !do_push_block(outq, b, tag) )
                break;
            // Reset to new block
            b         = network->pool->get();
            location  = (unsigned char*)b.iov_base;
            block_end = location + b.iov_len - wr_size;
        }

#ifdef FILA
        // FiLa10G/Mark5B only sends 32bits of sequence number
        seqnr = (uint64_t)(*((uint32_t*)(((unsigned char*)iov[0].iov_base)+4)));
#endif
        // Do sequence number + ACK processing - possibly
        curSender = std::find_if(&per_sender[0], endSender, find_by_sender);

        // Did we see this sender before?
        if( curSender==endSender ) {
            // Room for new sender?
            if( nSender>=maxSender )
                // no. just go on reading nxt pkt
                continue;
            // Ok, first time we see this sender. Initialize
            per_sender[nSender] = per_sender_type(sender, seqnr);
            curSender           = &per_sender[nSender];
            nSender++;
            endSender           = &per_sender[nSender];
        }

        // Let the per-sender handle the psn
        curSender->handle_seqnr(seqnr, network->fd, np.ackPeriod);

        // Aggregate the results
//        tmppkt = tmpooocnt = tmpooosum = tmplos = 0;
        tmplos = per_sender[0].loscnt;
//        for(unsigned int i=0; i<nSender; i++) {
        for(unsigned int i=1; i<nSender; i++) {
            per_sender_type const& ps = per_sender[i];
//            tmppkt    += ps.pktcnt;
//            tmpooocnt += ps.ooocnt;
//            tmpooosum += ps.ooosum;
            tmplos    += ps.loscnt;
        }
//        pktcnt = tmppkt;
//        ooocnt = tmpooocnt;
//        ooosum = tmpooosum;
        loscnt = tmplos;
    } 
    // We stopped blocking reads on the fd, so no more signals needed
    SYNCEXEC(args, delete network->threadid; network->threadid = 0);

    // Clean up
    delete [] zeroes_p;
    DEBUG(0, "udpsnorreader: stopping" << std::endl);
}

#endif
