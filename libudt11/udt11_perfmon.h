#ifndef LIBUDT11_UDT_PERFMON_H
#define LIBUDT11_UDT_PERFMON_H

#include <inttypes.h>

namespace libudt11 {
    namespace perfmon {
        struct TRACEINFO {
            // global measurements
            int64_t msTimeStamp;                 // time since the UDT entity is started, in milliseconds
            int64_t pktSentTotal;                // total number of sent data packets, including retransmissions
            int64_t pktRecvTotal;                // total number of received packets
            int pktSndLossTotal;                 // total number of lost packets (sender side)
            int pktRcvLossTotal;                 // total number of lost packets (receiver side)
            int pktRetransTotal;                 // total number of retransmitted packets
            int pktSentACKTotal;                 // total number of sent ACK packets
            int pktRecvACKTotal;                 // total number of received ACK packets
            int pktSentNAKTotal;                 // total number of sent NAK packets
            int pktRecvNAKTotal;                 // total number of received NAK packets
            int64_t usSndDurationTotal;		// total time duration when UDT is sending data (idle time exclusive)

            // local measurements
            int64_t pktSent;                     // number of sent data packets, including retransmissions
            int64_t pktRecv;                     // number of received packets
            int pktSndLoss;                      // number of lost packets (sender side)
            int pktRcvLoss;                      // number of lost packets (receiver side)
            int pktRetrans;                      // number of retransmitted packets
            int pktSentACK;                      // number of sent ACK packets
            int pktRecvACK;                      // number of received ACK packets
            int pktSentNAK;                      // number of sent NAK packets
            int pktRecvNAK;                      // number of received NAK packets
            double mbpsSendRate;                 // sending rate in Mb/s
            double mbpsRecvRate;                 // receiving rate in Mb/s
            int64_t usSndDuration;		// busy sending time (i.e., idle time exclusive)

            // instant measurements
            double usPktSndPeriod;               // packet sending period, in microseconds
            int pktFlowWindow;                   // flow window size, in number of packets
            int pktCongestionWindow;             // congestion window size, in number of packets
            int pktFlightSize;                   // number of packets on flight
            double msRTT;                        // RTT, in milliseconds
            double mbpsBandwidth;                // estimated bandwidth, in Mb/s
            int byteAvailSndBuf;                 // available UDT sender buffer size
            int byteAvailRcvBuf;                 // available UDT receiver buffer size
        };

        int perfmon(int /*fd*/, TRACEINFO* /*tiptr*/, bool);
    }
}


#endif
