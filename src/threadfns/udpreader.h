// Straight through UDP reader - no sequence number but with backtraffic every minute
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
//
#ifndef JIVE5A_THREADFNS_UDPREADER_H
#define JIVE5A_THREADFNS_UDPREADER_H

#include <chain.h>
#include <block.h>
#include <threadfns.h>
//#include <threadfns/per_sender.h>
#include <threadfns/do_push_block.h>

#include <list>
#include <string>
#include <sstream>
#include <iostream>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>


// Seamlessly push onto tagged or untagged queues
template <typename Item>
void udpreader(outq_type<Item>* outq, sync_type<fdreaderargs>* args) {
    int                       lastack, oldack;
    bool                      stop;
    ssize_t                   r;
    runtime*                  rteptr = 0;
    socklen_t                 slen( sizeof(struct sockaddr_in) );
    unsigned int              ack = 0;
    fdreaderargs*             network = args->userdata;
    struct sockaddr_in        sender;
    static std::string        acks[] = {"xhg", "xybbgmnx",
                                        "xyreryvwre", "tbqireqbzzr",
                                        "obxxryhy", "rvxryovwgre",
                                        "qebrsgbrgre", "" /* leave empty string last!*/};

    if( network )
        rteptr = network->rteptr; 
    ASSERT_COND( rteptr && network );

    // Before diving in too deep  ...
    // this asserts that all sizes make sense and meet certain constraints
    RTEEXEC(*rteptr, rteptr->sizes.validate());

    // an (optionally compressed) block of <blocksize> is chopped up in
    // chunks of <read_size>, optionally compressed into <write_size> and
    // then put on the network.
    // We reverse this by reading <write_size> from the network into blocks
    // of size <read_size> and fill up a block of size <blocksize> before
    // handing it down the processing chain.
    // [note: no compression => write_size==read_size, ie this scheme will always work]
    //
    // In order for decompression to work we have to fill up the difference
    // between read_size and write_size with zeroes
    block                        b;
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];
    const unsigned int           tag       = network->tag;

    // HV: 13-11-2013 Blocks larger than this size we call "insensible" and we 
    //                change the readahead + blockpool allocation scheme
    //                because we might be doing vlbi_streamer style
    const unsigned int           sensible_blocksize( 32*1024*1024 );

    // Note: cannot use std::auto_ptr here because we need 
    //       "operator delete []" and not "operator delete" -
    //       std::auto_ptr manages only a single object, not an array
    // Note: must remember how many zeroes we allocated, for
    //       the memcpy ...
    const size_t                 nzeroes        = wr_size - rd_size;
    const unsigned char*         zeroes         = (unsigned char*)(nzeroes?(::calloc(nzeroes, 1)):0);

    // Create a blockpool. If we need blocks we take'm from there
    // HV: 13-11-2013 If blocksize seems too large, do not allocate
    //                more than ~2GB/turn [which would be ~4 chunks
    //                for vlbi_streamer mode in 512MB/chunk] or 32 blocks.
    //                Only go for 2GB per allocation if we 
    //                *really* have to!
    const unsigned int  nb = (blocksize<sensible_blocksize?32:2);
    install_zig_for_this_thread(SIGUSR1);
    SYNC3EXEC(args,
              delete network->threadid;
              delete network->pool;
              network->threadid = new pthread_t( ::pthread_self() );
              network->pool = new blockpool_type(blocksize, nb),
              delete [] zeroes);

    // If blocksize > sensible block size start to pre-allocate!
    if( blocksize>=sensible_blocksize ) {
        std::list<block>        bl;
        const unsigned int npre = network->netparms.nblock;
        DEBUG(2, "udpreader: start pre-allocating " << npre << " blocks" << std::endl);
        for(unsigned int i=0; i<npre; i++)
            bl.push_back( network->pool->get() );
        DEBUG(2, "udpreader: ok, done that!" << std::endl);
    }

    // Set up the message - a lot of these fields have known & constant values
    struct iovec                 iov[1];
    struct msghdr                msg;
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;

    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // message 'msg': one fragment, the datagram
    msg.msg_iov        = &iov[0];
    msg.msg_iovlen     = 1;

    // The size of the datapart of the message are known
    iov[0].iov_len     = rd_size;

    // Here we fix the amount of iov's and lengths of the
    // messages for the two phases: PEEK and WAITALL
    // We should be safe for datagrams up to 2G i hope
    //   (the casts to 'int' from iov[..].iov_len because
    //    the .iov_len is-an unsigned)
    const int               waitallread = (int)(iov[0].iov_len);

    // reset statistics/chain and statistics/evlbi
    RTE3EXEC(*rteptr,
            rteptr->evlbi_stats[ network->tag ] = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdpRead") ,
            delete [] zeroes; delete network->threadid; network->threadid = 0 );

    // Great. We're done setting up. Now let's see if we weren't cancelled
    // by any chance
    SYNCEXEC(args, stop = args->cancelled);

    if( stop ) {
        delete [] zeroes;
        SYNCEXEC(args, delete network->threadid; network->threadid = 0);
        DEBUG(0, "udpreader: cancelled before actual start" << std::endl);
        return;
    }

    // No, we weren't. Now go into our mainloop!
    DEBUG(0, "udpreader: fd=" << network->fd << " datagramlength=" << iov[0].iov_len << std::endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&   pktcnt( rteptr->evlbi_stats[ network->tag ].pkt_in );

    // inner loop variables
    unsigned char* location;
    unsigned char* endptr;
    netparms_type& np( network->rteptr->netparms );

    // Before actually starting to receive get a block and initialize
    b = network->pool->get();

    pktcnt++;
    location = (unsigned char*)b.iov_base;
    endptr   = (unsigned char*)b.iov_base + blocksize;

    // HV: 9 Jul 2012 - in order to prevent flooding we will
    //     have to send UDP backtraffic every now and then
    //     (such that network equipment between the scope and
    //     us does not forget our ARP entry). 
    //     We read the first packet and record who sent it to us.
    if( ::recvfrom(network->fd, location, rd_size, MSG_WAITALL, (struct sockaddr*)&sender, &slen)!=(ssize_t)rd_size ) {
        delete [] zeroes;
        SYNCEXEC(args, delete network->threadid; network->threadid = 0);
        DEBUG(-1, "udpreader: cancelled before beginning" << std::endl);
        return;
    }
    location += wr_size;               // next packet will be put at write size offset
    lastack   = 0;                     // trigger immediate ack send
    oldack    = netparms_type::defACK; // will be updated if value is not set to default

    DEBUG(0, "udpreader: incoming data from " <<
              inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << std::endl);

    // Drop into our tight inner loop
    do {
        // First check if we filled up a block
        if( location>=endptr ) {
            //if( outq->push(b)==false )
            if( do_push_block(outq, b, tag)==false )
                break;
            // get a new block to write data in
            b = network->pool->get();
            location = (unsigned char*)b.iov_base;
            endptr   = (unsigned char*)b.iov_base + blocksize;
        }

        // Our primary computations have been done and, what's most
        // important, a location for the packet has been decided upon
        // Read the pakkit into our mem'ry space before we do anything else
        iov[0].iov_base = location;
        if( (r=::recvmsg(network->fd, &msg, MSG_WAITALL))!=(ssize_t)waitallread ) {
            lastsyserror_type    lse;
            std::ostringstream   oss;
            const unsigned char* beginptr = (const unsigned char*)b.iov_base;

            // If the current block is partially filled, do push it on 
            // downwards, but only the part that was filled
            if( location>beginptr && network->allow_variable_block_size )
                do_push_block(outq, b.sub(0, (location-beginptr)), tag);
                //outq->push(b.sub(0, (location-beginptr)));

            // Deregister interest in getting signals
            SYNCEXEC(args, delete network->threadid; network->threadid = 0);

            // whilst we're at it: fix a long-standing issue:
            //  do NOT throw on EINTR; it is the normal
            //  way in which a connection is terminated
            //  Note: in fact, it is EBADF that is returned
            //        under normal circumstances; some
            //        other thread has closed the fd
            //        and kicked us. Hence no throw on
            //        that error either.
            if( lse.sys_errno==EINTR || lse.sys_errno==EBADF )
                break;
            // It wasn't EINTR,so now we _must_ throw!
            delete [] zeroes;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - [" << lse << "] (ask:" << waitallread << " got:" << r << ")";
            throw syscallexception(oss.str());
        }

        // Now that we've *really* read the pakkit we may update our
        // read statistics, but not before we've appended the zeroes
        if( zeroes )
            ::memcpy(location + rd_size, zeroes, nzeroes);

        counter  += waitallread;
        location += wr_size;
        pktcnt++;

        // Acknowledgement processing:
        // Send out an ack before we go into infinite wait
        if( np.ackPeriod!=oldack ) {
            // someone set a different acknowledgement period
            // let's trigger immediate send + restart with current ACK period
            lastack = 0;
            oldack  = np.ackPeriod;
            DEBUG(2, "udpreader: switch to ACK every " << oldack << "th packet" << std::endl);
        }
        if( lastack<=0 ) {
            if( acks[ack].empty() )
                ack = 0;
            // Only warn if we fail to send. Try again in two minutes
            if( ::sendto(network->fd, acks[ack].c_str(), acks[ack].size(), 0,
                         (const struct sockaddr*)&sender, sizeof(struct sockaddr_in))==-1 )
                DEBUG(-1, "udpreader: WARN failed to send ACK back to sender" << std::endl);
            lastack = oldack;
            ack++;
        } else {
            // lower ACK counter 
            lastack--;
        }
    } while( true );
    // Make sure we're deregistered for interest in getting signals
    SYNCEXEC(args, delete network->threadid; network->threadid = 0);

    // Clean up
    delete [] zeroes;
    DEBUG(0, "udpreader: stopping" << std::endl);
}

#endif
