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
#include <threadfns/per_sender.h>
#include <evlbidebug.h>

#include <string>       // std::string
#include <iostream>
#include <string.h>     // memcpy
#include <arpa/inet.h>  // ntohs



static std::string __m_acks[] = {"xhg", "xybbgmnx",
                                 "xyreryvwre", "tbqireqbzzr",
                                 "obxxryhy", "rvxryovwgre",
                                 "qebrsgbrgre", "" /* leave empty string last!*/};


per_sender_type::per_sender_type():
    ack( 0 ), lastack( 0 ), oldack( 0 ), expectseqnr( 0 ), maxseq( 0 ), minseq( 0 ),
    loscnt( 0 ), pktcnt( 0 ), ooocnt( 0 ), ooosum( 0 ), psn(16)
{
  ::memset(&sender, 0x0, sizeof(struct sockaddr_in));
}

per_sender_type::per_sender_type(struct sockaddr_in const& sin, uint64_t seqnr):
        ack( 0 ), lastack( 0 ), oldack( 0 ), expectseqnr( seqnr ), maxseq( seqnr ), minseq( seqnr ),
        loscnt( 0 ), pktcnt( 0 ), ooocnt( 0 ), ooosum( 0 ), psn(16)
{
  ::memcpy(&sender, &sin, sizeof(struct sockaddr_in));
  DEBUG(0, "per_sender_type[" << inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << "] - " <<
          "first sequencenr# " << seqnr << std::endl);
}

per_sender_type const& per_sender_type::operator=(per_sender_type const& other) {
    if( this==&other )
        return *this;
    ack         = other.ack;
    lastack     = other.lastack;
    oldack      = other.oldack;
    expectseqnr = other.expectseqnr;
    maxseq      = other.maxseq;
    minseq      = other.minseq;
    loscnt      = other.loscnt;
    pktcnt      = other.pktcnt;
    ooocnt      = other.ooocnt;
    ooosum      = other.ooosum;
    ::memcpy(&sender, &other.sender, sizeof(struct sockaddr_in));
    return *this;
}

void per_sender_type::handle_seqnr(uint64_t seqnr, int fd, int ackperiod) {
    // Got another pakkit
    pktcnt++;
    if( seqnr>maxseq )
        maxseq = seqnr;
    else if( seqnr<minseq )
        minseq = seqnr;
    loscnt = (maxseq - minseq + 1 - pktcnt);

    // Do PSN statistics processing if we think there IS
    // a (changing) sequence number. The udpreader_stream
    // will continuously pass the same sequence nr because there isn't
    // one. But we /do/ want the back traffic ("ACK processing")
    if( maxseq!=minseq ) {
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
    }

    // Acknowledgement processing:
    // Send out an ack before we go into infinite wait
    if( ackperiod!=oldack ) {
        // someone set a different acknowledgement period
        // let's trigger immediate send + restart with current ACK period
        lastack = 0;
        oldack  = ackperiod;
        DEBUG(2, "handle_seqnr[" << inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << "] - " <<
                "switch to ACK every " << oldack << "th packet" << std::endl);
    }

    lastack--;
    if( lastack>0 )
        return;

    // ACK counter <= 0, i.e. time to send new ACKnowledgement
    if( __m_acks[ack].empty() )
        ack = 0;
    // Only warn if we fail to send. Try again in two minutes
    if( ::sendto(fd, __m_acks[ack].c_str(), __m_acks[ack].size(), 0,
                (const struct sockaddr*)&sender, sizeof(struct sockaddr_in))==-1 )
        DEBUG(-1, "handle_seqnr[" << inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << "] - " <<
                "WARN failed to send ACK back to sender" << std::endl);
    lastack = oldack;
    ack++;
}

// Helper to find an entry by socket address
find_by_sender_type::find_by_sender_type(struct sockaddr_in* sinptr):
    __m_sockaddr_ptr( sinptr )
{}

