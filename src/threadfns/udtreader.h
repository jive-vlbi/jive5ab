// UDT reader - using libudt5ab
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
#ifndef JIVE5A_THREADFNS_UDTREADER_H
#define JIVE5A_THREADFNS_UDTREADER_H

#include <chain.h>
#include <block.h>
#include <sciprint.h>
#include <threadfns.h>
//#include <threadfns/per_sender.h>
#include <threadfns/do_push_block.h>
#include <udt.h>

#include <list>
#include <string>
#include <sstream>
#include <iostream>

//#include <netinet/in.h>
//#include <arpa/inet.h>

template <typename Item>
void udtreader(outq_type<Item>* outq, sync_type<fdreaderargs>* args) {
    bool                   stop;
    uint64_t               bytesread;
    runtime*               rteptr;
    fdreaderargs*          network = args->userdata;

    rteptr = network->rteptr;
    ASSERT_COND(rteptr!=0);

    // Before diving in too deep  ...
    // this asserts that all sizes make sense and meet certain constraints
    RTEEXEC(*rteptr,
            rteptr->sizes.validate();
            rteptr->evlbi_stats[ network->tag ] = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdtReadv2"));

    counter_type&        counter( rteptr->statistics.counter(args->stepid) );
    const unsigned int   rd_size = rteptr->sizes[constraints::write_size];
    const unsigned int   wr_size = rteptr->sizes[constraints::read_size];
    const unsigned int   bl_size = rteptr->sizes[constraints::blocksize];
    const unsigned int   n_blank = (wr_size - rd_size);
    const unsigned int   tag     = network->tag;

    ucounter_type&       loscnt( rteptr->evlbi_stats[ network->tag ].pkt_lost );
    ucounter_type&       pktcnt( rteptr->evlbi_stats[ network->tag ].pkt_in );
    SYNCEXEC(args,
             stop = args->cancelled;
             delete network->threadid; network->threadid = new pthread_t( ::pthread_self() );
             if(!stop) network->pool = new blockpool_type(bl_size,16););

    if( stop ) {
        DEBUG(0, "udtreader: stop signalled before we actually started" << std::endl);
        return;
    }

    DEBUG(0, "udtreader: read fd=" << network->fd << " rd:" << rd_size
             << " wr:" << wr_size <<  " bs:" << bl_size << std::endl);
    bytesread = 0;
    while( !stop ) {
        block                b = network->pool->get();
        unsigned char*       ptr  = (unsigned char*)b.iov_base;
        UDT::TRACEINFO       ti;
        const unsigned char* eptr = (ptr + b.iov_len);

        // do read data orf the network. keep on readin' until we have a
        // full block. the constraintsolvert makes sure that an integral
        // number of write-sizes will fit into a block. 
        while( !stop && (ptrdiff_t)(eptr-ptr)>=(ptrdiff_t)wr_size ) {
            // Read 'rd_size' bytes off the UDT socket
            int             r;
            unsigned int    nrec = 0;

            while( nrec!=rd_size ) {
                if( (r=UDT::recv(network->fd, ((char*)ptr)+nrec, rd_size-nrec, 0))==UDT::ERROR ) {
                    UDT::ERRORINFO& udterror = UDT::getlasterror();
                    DEBUG(0, "udtreader: error " << udterror.getErrorMessage() << " (" 
                              << udterror.getErrorCode() << ")" << std::endl);
                    // We encountered an error - signal it and break from reading "rd_size"
                    stop = true;
                    break;
                }
                // This will work because the UDT lib returns "int" as
                // parameter so it can't ever return a negative number
                // (other than to signal an error, which has already been
                // covered)
                nrec += (unsigned int)r;
            }
            counter   += nrec;
            bytesread += nrec;
            ptr       += nrec;

            // Upon succesfull read of 'rd_size' bytes (stop==false => nrec==rd_size)
            // blank out the amount of bytes that we must blank out
            if( !stop && n_blank ) {
                ::memset(ptr, 0x00, n_blank);
                ptr += n_blank;
            }

            if( UDT::perfmon(network->fd, &ti, true)==0 ) {
                pktcnt  = ti.pktRecvTotal;
                loscnt += ti.pktRcvLoss;
            }
        }
        // If we read an incomplete block, allow this only if we were allowed to
        if( ptr!=eptr ) {
            if( !network->allow_variable_block_size ) {
                DEBUG(-1, "udtreader: skip blok because of constraint error. blocksize not integral multiple of write_size" << std::endl);
                continue;
            }
            // Ok, variable block size allowed, update size of current block
            // The expected length of the block is up to 'eptr' (==b.iov_len)
            // so the bit that wasn't filled in can be subtracted
            b.iov_len -= (size_t)(eptr - ptr);
            DEBUG(3, "udtreader: partial block; adjusting block size by -" << (size_t)(eptr - ptr) << std::endl);
        }
        // push it downstream
        //if( outq->push(b)==false )
        if( do_push_block(outq, b, tag)==false )
            break;
    }
    SYNCEXEC(args, delete network->threadid; network->threadid=0);
    DEBUG(0, "udtreader: stopping. read " << bytesread << " (" <<
             byteprint((double)bytesread,"byte") << ")" << std::endl);
}

#endif
