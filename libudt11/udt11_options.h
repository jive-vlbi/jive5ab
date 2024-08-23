// Options specific for the UDT protocol
// (c) 2024 Marjolein Verkouter
#ifndef LIBUDT11_UDT_OPTIONS_H
#define LIBUDT11_UDT_OPTIONS_H

enum UDTOpt {
    UDT_MSS,             // the Maximum Transfer Unit
    UDT_SNDSYN,          // if sending is blocking
    UDT_RCVSYN,          // if receiving is blocking
    UDT_CC,              // custom congestion control algorithm
    UDT_FC,		// Flight flag size (window size)
    UDT_SNDBUF,          // maximum buffer in sending queue
    UDT_RCVBUF,          // UDT receiving buffer size
    UDT_LINGER,          // waiting for unsent data when closing
    UDP_SNDBUF,          // UDP sending buffer size
    UDP_RCVBUF,          // UDP receiving buffer size
    UDT_MAXMSG,          // maximum datagram message size
    UDT_MSGTTL,          // time-to-live of a datagram message
    UDT_RENDEZVOUS,      // rendezvous connection mode
    UDT_SNDTIMEO,        // send() timeout
    UDT_RCVTIMEO,        // recv() timeout
    UDT_REUSEADDR,	// reuse an existing port or create a new one
    UDT_MAXBW,		// maximum bandwidth (bytes per second) that the connection can use
    UDT_STATE,		// current socket state, see UDTSTATUS, read only
    UDT_EVENT,		// current avalable events associated with the socket
    UDT_SNDDATA,		// size of data in the sending buffer
    UDT_RCVDATA		// size of data available for recv
};

#endif
