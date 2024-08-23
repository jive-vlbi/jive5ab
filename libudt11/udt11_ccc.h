// congestion control
#ifndef LIBUDT11_CCC_H
#define LIBUDT11_CCC_H

#include <inttypes.h>

namespace libudt11 {
    namespace ccc {
        struct CPacket;

        class CCC {

            public:
                CCC() {}
                virtual ~CCC() {}

                // Functionality:
                //    Callback function to be called (only) at the start of a UDT connection.
                //    note that this is different from CCC(), which is always called.
                // Parameters:
                //    None.
                // Returned value:
                //    None.

                virtual void init() {}

                // Functionality:
                //    Callback function to be called when a UDT connection is closed.
                // Parameters:
                //    None.
                // Returned value:
                //    None.

                virtual void close() {}

                // Functionality:
                //    Callback function to be called when an ACK packet is received.
                // Parameters:
                //    0) [in] ackno: the data sequence number acknowledged by this ACK.
                // Returned value:
                //    None.

                virtual void onACK(int32_t) {}

                // Functionality:
                //    Callback function to be called when a loss report is received.
                // Parameters:
                //    0) [in] losslist: list of sequence number of packets, in the format describled in packet.cpp.
                //    1) [in] size: length of the loss list.
                // Returned value:
                //    None.

                virtual void onLoss(const int32_t*, int) {}

                // Functionality:
                //    Callback function to be called when a timeout event occurs.
                // Parameters:
                //    None.
                // Returned value:
                //    None.

                virtual void onTimeout() {}

                // Functionality:
                //    Callback function to be called when a data is sent.
                // Parameters:
                //    0) [in] seqno: the data sequence number.
                //    1) [in] size: the payload size.
                // Returned value:
                //    None.

                virtual void onPktSent(const CPacket*) {}

                // Functionality:
                //    Callback function to be called when a data is received.
                // Parameters:
                //    0) [in] seqno: the data sequence number.
                //    1) [in] size: the payload size.
                // Returned value:
                //    None.

                virtual void onPktReceived(const CPacket*) {}

                // Functionality:
                //    Callback function to Process a user defined packet.
                // Parameters:
                //    0) [in] pkt: the user defined packet.
                // Returned value:
                //    None.

                virtual void processCustomMsg(const CPacket*) {}
        };

        class CCCVirtualFactory
        {
            public:
                virtual ~CCCVirtualFactory() {}

                virtual CCC* create() = 0;
                virtual CCCVirtualFactory* clone() = 0;
        };

        template <class T>
            class CCCFactory: public CCCVirtualFactory
        {
            public:
                virtual ~CCCFactory() {}

                virtual CCC* create() {return new T;}
                virtual CCCVirtualFactory* clone() {return new CCCFactory<T>;}
        };


        class CUDTCC:
            public CCC
        {
            public:
                CUDTCC();

                virtual void init();
                virtual void onACK(int32_t);
                virtual void onLoss(const int32_t*, int);
                virtual void onTimeout();

                double m_dPktSndPeriod;
        };
    } // ccc
} // libudt11

// The original UDT library exposes these in the global namespace
using CUDTCC            = libudt11::ccc::CUDTCC;
using CCCVirtualFactory = libudt11::ccc::CCCVirtualFactory;
template <typename T>
using CCCFactory        = libudt11::ccc::CCCFactory<T>;

#endif
