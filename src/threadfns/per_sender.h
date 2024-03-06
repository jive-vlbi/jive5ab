// Keep packet statistics per (UDP) sender (IPv4 addr and port)
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
#ifndef JIVE5A_THREADFNS_PER_SENDER_H
#define JIVE5A_THREADFNS_PER_SENDER_H

#include <inttypes.h>
#include <netinet/in.h>
#include <circular_buffer.h>

// As we expect this reader to take data from different senders, let's keep
// the packet statistics per sender?
struct per_sender_type {
    int                        ack, lastack, oldack;
    uint64_t                   expectseqnr, maxseq, minseq;
    uint64_t                   loscnt, pktcnt, ooocnt, ooosum;
    struct sockaddr_in         sender;
    circular_buffer<uint64_t>  psn;

    per_sender_type();
    per_sender_type(struct sockaddr_in const& sin, uint64_t seqnr);

    per_sender_type const& operator=(per_sender_type const& other);

    void handle_seqnr(uint64_t seqnr, int fd, int ackperiod);
};

// Helper to find an entry by socket address
struct find_by_sender_type {
    find_by_sender_type(struct sockaddr_in* sinptr);

    inline bool operator()(per_sender_type const& per_sender) const {
        struct sockaddr_in const&   sin( per_sender.sender );

        return sin.sin_port==__m_sockaddr_ptr->sin_port && sin.sin_addr.s_addr==__m_sockaddr_ptr->sin_addr.s_addr;
    }
    struct sockaddr_in* __m_sockaddr_ptr;
};

#endif
