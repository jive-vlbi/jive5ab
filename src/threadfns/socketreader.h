// read from a connected (tcp) socket
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
#ifndef JIVE5A_THREADFNS_SOCKETREADER_H
#define JIVE5A_THREADFNS_SOCKETREADER_H

#include <chain.h>
#include <block.h>
#include <sciprint.h>
#include <threadfns.h>
//#include <threadfns/per_sender.h>
#include <threadfns/do_push_block.h>

#include <list>
#include <string>
#include <sstream>
#include <iostream>

// read from a socket. we always allocate chunks of size <read_size> and
// read from the network <write_size> since these sizes are what went *into*
// the network so we just reverse them
// (reading <read_size> optionally compressing into <write_size> before
// writing to the network)
template <typename Item>
void socketreader(outq_type<Item>* outq, sync_type<fdreaderargs>* args) {
    bool                   stop;
    uint64_t               bytesread;
    runtime*               rteptr;
    fdreaderargs*          network = args->userdata;

    rteptr = network->rteptr;
    ASSERT_COND(rteptr!=0);

    DEBUG(-1, "socketreader: allow_variable_block_size=" << network->allow_variable_block_size << " ptr=" << (void*)network << std::endl);

    // Before diving in too deep  ...
    // this asserts that all sizes make sense and meet certain constraints
    RTEEXEC(*rteptr,
            rteptr->sizes.validate();
            rteptr->statistics.init(args->stepid, "SocketReadv2"));

    counter_type&        counter( rteptr->statistics.counter(args->stepid) );
    const unsigned int   rd_size = rteptr->sizes[constraints::write_size];
    const unsigned int   wr_size = rteptr->sizes[constraints::read_size];
    const unsigned int   bl_size = rteptr->sizes[constraints::blocksize];
    const unsigned int   n_blank = (wr_size - rd_size); 
    const unsigned int   tag     = network->tag;

    SYNCEXEC(args,
            stop = args->cancelled;
            delete network->threadid;
            network->threadid = new pthread_t( ::pthread_self() );
            if( !stop ) network->pool = new blockpool_type(bl_size, rteptr->netparms.nblock););
    install_zig_for_this_thread(SIGUSR1);

    if( stop ) {
        DEBUG(0, "socketreader: stop signalled before we actually started" << std::endl);
        SYNCEXEC(args, delete network->threadid; network->threadid = 0);
        return;
    }
    DEBUG(0, "socketreader: read fd=" << network->fd << " rd:" << rd_size
            << " wr:" << wr_size <<  " bs:" << bl_size << std::endl);
    bytesread = 0;
    while( !stop ) {
        block                b = network->pool->get();
        int                  r;
        unsigned char*       ptr  = (unsigned char*)b.iov_base;
        const unsigned char* eptr = (ptr + b.iov_len);

        // do read data orf the network. keep on readin' until we have a
        // full block. the constraintsolvert makes sure that an integral
        // number of write-sizes will fit into a block.
        while( !stop && (ptrdiff_t)(eptr-ptr)>=(ptrdiff_t)wr_size ) {
            r = ::recvfrom(network->fd, ptr, rd_size, MSG_WAITALL, 0, 0);
            // this check will go wrong when network->blocksize >2.1GB
            // (INT_MAX for 32bit integer). oh well.
            stop = (r!=(int)rd_size);
            if( r<=0 ) {
                lastsyserror_type lse;
                // ==0 means hangup
                //  <0 means error
                // both cases warrant us breaking from the loop
                if( r==0 ) {
                    DEBUG(0, "socketreader: remote side closed connection" << std::endl);
                } else {
                    DEBUG(0, "socketreader: read failure " << lse << std::endl);
                }
                break;
            }
            // Ok, we got sum dataz
            counter   += (unsigned int)r;
            bytesread += (unsigned int)r;
            ptr       += (unsigned int)r;
            // If the read worked normally (stop==false), we must add
            // the difference between 'wr_size' and 'rd_size' and
            // blank it
            if( !stop && n_blank ) {
                ::memset(ptr, 0x00, n_blank);
                ptr += n_blank;
            }
        }
        if( ptr!=eptr ) {
            if( !network->allow_variable_block_size ) {
                DEBUG(-1, "socketreader: skip blok because of constraint error. blocksize not integral multiple of write_size" << std::endl);
                continue;
            }
            // Ok, variable block size allowed, update size of current block
            // The expected length of the block is up to 'eptr' (==b.iov_len)
            // so the bit that wasn't filled in can be subtracted
            b.iov_len -= (size_t)(eptr - ptr);
            DEBUG(3, "socketreader: partial block; adjusting block size by -" << (size_t)(eptr - ptr) << std::endl);
        }
        // push the block downstream
        //if( outq->push(b)==false )
        if( do_push_block(outq, b, tag)==false )
            break;
    }
    // We won't be reading from the socket anymore - better inform the
    // remote side about this
    char dummy;
    if( ::write(network->fd, &dummy, 1)==0 )
        if( ::shutdown(network->fd, SHUT_RD) ) {}

    DEBUG(0, "socketreader: stopping. read " << bytesread << " (" <<
            byteprint((double)bytesread,"byte") << ")" << std::endl);
    
    SYNCEXEC(args, delete network->threadid; network->threadid = 0);
    network->finished = true;
}

#endif
