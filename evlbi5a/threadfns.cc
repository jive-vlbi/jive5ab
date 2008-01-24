// implementations of the threadfunctions
#include <threadfns.h>

#include <runtime.h>
#include <xlrdevice.h>
#include <pthreadcall.h>
#include <playpointer.h>
#include <dosyscall.h>
#include <streamutil.h>
#include <evlbidebug.h>
#include <getsok.h>
#include <headersearch.h>


#include <sstream>
#include <string>
#include <iostream>

#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>


using namespace std;


// thread function which reads data from disc into memory
void* disk2mem( void* argptr ) {
    runtime*        rte = (runtime*)argptr;
    SSHANDLE        sshandle;
    S_READDESC      readdesc;
    playpointer     cur_pp( 0 );
    unsigned int    idx;
    unsigned int    blocksize;
    unsigned int    nblock;
    unsigned char*  buffer = 0;

    try { 
        bool   stop;

        // We can't get cancelled. Signal us to stop via
        // rte->stop==true [and use pthread_condbroadcast()]
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );

        // indicate we're doing disk2mem
        rte->tomem_dev      = dev_disk;

        // Can fill in parts of the readdesc that will not change
        // across invocations.
        // NOTE: WE RELY ON THE SYSTEM TO ENSURE THAT BLOCKSIZE IS
        //       A MULTIPLE OF 8!
        // NOTE: We allocate (at least) one block more than how many
        //       will fit in the queue; once a block is in the queue
        //       we should not overwrite it, so by having (at least)
        //       one more block locally, we can always fill up the
        //       entire queue and still have block to ourselves in
        //       which we can read data
        nblock              = rte->netparms.nblock + 1;
        blocksize           = rte->netparms.get_blocksize();
        readdesc.XferLength = blocksize;
        // init current playpointer
        rte->pp_current     = 0;

        // allocate local storage. At least "nblock * blocksize". More could
        // also be done but not very usefull as they queue will only fit
        // nblock anyhoo
        buffer = new unsigned char[ nblock * blocksize ];

        // Wait for 'start' or 'stop' [ie: state change from
        // default 'start==false' and 'stop==false'
        // grab the mutex
        PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
        while( !rte->stop && !rte->run )
            PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
        // copy shared state variable whilst be still have the mutex.
        // Only have to get 'stop' since if it's 'true' the value of
        // run is insignificant and if it's 'false' then run MUST be
        // true [see while() condition...]
        stop   = rte->stop;
        // initialize the current play-pointer, just for
        // when we're supposed to run
        cur_pp   = rte->pp_start;
        sshandle = rte->xlrdev.sshandle();
        PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

        // Now, enter main thread loop.
        idx    = 0;
        while( !stop ) {
            // Read a bit'o data into memry
            readdesc.AddrHi     = cur_pp.AddrHi;
            readdesc.AddrLo     = cur_pp.AddrLo;
            readdesc.BufferAddr = (long unsigned int*)(buffer + idx * blocksize);

            XLRCALL( ::XLRRead(sshandle, &readdesc) );

            // great. Now attempt to push it onto the queue.
            // push() will always succeed [it will block until it *can* push]
            // unless the queue is disabled. If queue is disabled, that's when
            // we decide to bail out
            if( !rte->queue.push( block(readdesc.BufferAddr, readdesc.XferLength) ) )
                break;

            // weehee. Done a block. Update our local loop variables
            cur_pp += readdesc.XferLength;
            idx     = (idx+1)%nblock;

            // Now update shared-state variables
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );

            rte->pp_current    = cur_pp;
            rte->nbyte_to_mem += readdesc.XferLength;

            // Whilst we still have the mutex, inspect
            // global stop flag and possibly if we
            // reached end-of-playable range and maybe if
            // we need to restart from rte->pp_start.
            stop = rte->stop;
            if( !stop ) {
                if( cur_pp>=rte->pp_end )
                    if( (stop=!rte->repeat)==false )
                        cur_pp = rte->pp_start;
            }

            // Done all our accounting + checking, release mutex
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
        }
        DEBUG(1, "disk2mem stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "disk2mem caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "disk2mem caught unknown exception?!" << endl;
    }
    if( rte )
        rte->tomem_dev = dev_none;
    delete [] buffer;
    return (void*)0;
}

// read from StreamStor FIFO -> memory
void* fifo2mem( void* argptr ) {
    // high-water mark for the fifo.
    // If fifolen>=hiwater we should
    // attempt to read until the level
    // falls below this [if the fifo becomes
    // full the device gets stuck and we're
    // up da creek since only a reset can clear
    // that]
    // Currently, set it at 50%. The FIFO on
    // the streamstor is 512MB deep
    // 
    // 'num_unsignedlongs' is the number of unsigned longs
    // the system will read from the fifo if it is too full
    const DWORDLONG    hiwater = (512*1024*1024)/2;
    const unsigned int num_unsignedlongs = 256000; 
    // automatic variables
    runtime*           rte = (runtime*)argptr;
    SSHANDLE           sshandle;
    DWORDLONG          fifolen;
    unsigned int       idx;
    unsigned int       blocksize;
    unsigned int       nblock;
    unsigned char*     buffer = 0;
    unsigned long int* emergency_block = 0;
    unsigned long int* ptr;

    try { 
        bool   stop;

        // We can't get cancelled. Signal us to stop via
        // rte->stop==true [and use pthread_condbroadcast()]
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );

        emergency_block = new unsigned long int[ num_unsignedlongs ];

        // indicate we're doing disk2mem
        rte->tomem_dev      = dev_fifo;

        // Can fill in parts of the readdesc that will not change
        // across invocations.
        // NOTE: WE RELY ON THE SYSTEM TO ENSURE THAT BLOCKSIZE IS
        //       A MULTIPLE OF 8!
        // Note: we get (at least) one more block than what will
        //       fit in the queue; the blocks that *are* in the queue
        //       we should NOT mess with; we don't know if they're
        //       being accessed or not
        nblock              = rte->netparms.nblock + 1;
        blocksize           = rte->netparms.get_blocksize();

        // allocate local storage. At least "nblock * blocksize". More could
        // also be done but not very usefull as they queue will only fit
        // nblock anyhoo
        buffer = new unsigned char[ nblock * blocksize ];

        // Wait for 'start' or 'stop' [ie: state change from
        // default 'start==false' and 'stop==false'
        // grab the mutex
        PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
        while( !rte->stop && !rte->run )
            PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
        // copy shared state variable whilst be still have the mutex.
        // Only have to get 'stop' since if it's 'true' the value of
        // run is insignificant and if it's 'false' then run MUST be
        // true [see while() condition...]
        stop     = rte->stop;
        sshandle = rte->xlrdev.sshandle();
        PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

        // Now, enter main thread loop.
        idx    = 0;
        while( !stop ) {
            unsigned long  nread = 0;

            // read a bit'o data into memry
            ptr = (long unsigned int*)(buffer + idx * blocksize);

            // Make sure the FIFO is not too full
            // Use a (relative) large block for this so it will work
            // regardless of the network settings
            while( (fifolen=::XLRGetFIFOLength(sshandle))>=hiwater )
                XLRCALL2( ::XLRReadFifo(sshandle, emergency_block,
                                        num_unsignedlongs*sizeof(unsigned long), 0),
                          XLRINFO(" whilst trying to get FIFO level < hiwatermark"); );

            // Depending on if enough data available or not, do something
            if( fifolen<blocksize ) {
                struct timespec  ts;

                // Let's sleep for, say, 100u sec 
                // we don't care if we do not fully sleep the requested amount
                ts.tv_sec  = 0;
                ts.tv_nsec = 100000;
                ASSERT_ZERO( ::nanosleep(&ts, 0) );
            } else {
                // ok, enough data available. Read and stick in queue
                XLRCALL( ::XLRReadFifo(sshandle, ptr, blocksize, 0) );

                // and push it on da queue. push() always succeeds [will block
                // until it *can* push] UNLESS the queue was disabled before or
                // whilst waiting for the queue to become push()-able.
                if( rte->queue.push(block(ptr, blocksize))==false )
                    break;

                // indicate we've read another 'blocksize' amount of
                // bytes into mem
                nread = blocksize;
            }

            // Update shared state variables (and whilst we're at it,
            // see if we're requested to stop)
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            rte->nbyte_to_mem += nread;
            stop               = rte->stop;
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

            // and move on to next block
            idx = (idx+1)%nblock;            
        }
        DEBUG(1, "fifo2mem stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "fifo2mem caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "fifo2mem caught unknown exception?!" << endl;
    }
    if( rte )
        rte->tomem_dev = dev_none;
    delete [] buffer;
    delete [] emergency_block;
    return (void*)0;
}

// thread function which transfers data from mem'ry to FIFO
void* mem2streamstor( void* argptr ) {
    // hi-water mark. If FIFOlen>=this value, do NOT
    // write data to the device anymore.
    // The device seems to hang up around 62% so for
    // now we stick with 60% as hiwatermark
    // [note: FIFO is 512MByte deep]
    const DWORDLONG      hiwater = (DWORDLONG)(0.6*(512.0*1024.0*1024.0)); 
    // variables
    block              blk;
    runtime*           rte = (runtime*)argptr;
    SSHANDLE           sshandle;
    // variables for restricting the output-rate of errormessages +
    // count how many data was NOT written to the device
    struct timeb*      tptr = 0;
    unsigned long long nskipped = 0ULL;

    try { 
        // not cancellable. stop us via:
        // rte->stop==true [and use pthread_condbroadcast()]
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );

        // indicate we're writing to the 'FIFO'
        rte->frommem_dev = dev_fifo;

        // take over values from the runtime
        sshandle           = rte->xlrdev.sshandle();

        // enter thread mainloop
        DEBUG(1,"mem2streamstor: starting" << endl);
        while( true ) {
            bool stop;
            // pop will blocking wait until someone
            // stuffs something in the queue or until
            // someone disables the queue.
            blk = rte->queue.pop();

            // if pop() returns an empty block, that is taken to mean
            // that the queue was disabled, ie, we're signalled to stop
            if( blk.empty() )
                break;

            // if 'tptr' not null and 'delta-t' >= 2.0 seconds, print a report
            // on how many bytes have been skipped since the last report
            // if we haven't skipped any data since the last report, we assume
            // the blockage has lifted?
            if( tptr ) {
                double         dt;
                struct timeb   tnow;

                ::ftime( &tnow );
                dt = ((double)tnow.time + ((double)tnow.millitm/1000.0)) -
                     ((double)tptr->time + ((double)tptr->millitm/1000.0));
                if( dt>=2.0 ) {
                    if( nskipped ) {
                        char      tb[32];
                        time_t    time_t_now;
                        struct tm tm_now;

                        time_t_now = ::time(0);
                        ::localtime_r(&time_t_now, &tm_now);
                        ::strftime(tb, sizeof(tb), "%T", &tm_now);
                        *tptr    = tnow;
                        cerr << "mem2streamstor: " << tb << " FIFO too full! "
                             << nskipped << " bytes lost" << endl;
                    } else {
                        // no more bytes skipped. clean up
                        delete tptr;
                        tptr = 0;
                    }
                    nskipped = 0ULL;
                }
            }
            // Check if we actually can stick the block in the FIFO
            if( ::XLRGetFIFOLength(sshandle)>=hiwater ) {
                // up the number of bytes skipped
                nskipped += blk.iov_len;

                // if no timeptr yet, create it
                if( !tptr ) {
                    tptr = new struct timeb;

                    // and initialize it
                    ::ftime( tptr );
                }
                continue;
            }

            // Now write the current block to the StreamStor
            XLRCALL( ::XLRWriteData(sshandle, blk.iov_base, blk.iov_len) );

            // Ok, we've written another block,
            // tell the other that
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            rte->nbyte_from_mem += blk.iov_len;
            stop = rte->stop;
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

            if( rte->stop )
                break;
        }
        DEBUG(1,"mem2streamstor: finished" << endl);
    }
    catch( const exception& e ) {
        cerr << "mem2streamstor caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "mem2streamstor caught unknown exception?!" << endl;
    }
    if( rte )
        rte->frommem_dev = dev_none;
    return (void*)0;
}


// function to calibrate the number of "volatile unsigned long long int" ++
// operations to perform in order to waste 1 microsecond of time
unsigned long long int counts_per_usec( void ) {
    // ==0 => uninitialized/uncalibrated
    static unsigned long long int  counts = 0ULL;

    if( counts )
        return counts;

    // do try to calibrate
    const unsigned int              ntries = 10;

    double                          deltat[ntries];
    unsigned long long int          total_n_count;
    unsigned long long int          total_n_microsec;
    unsigned long long int          sval = 100000;
    volatile unsigned long long int counted[ntries];

    

    for( unsigned int i=0; i<ntries; ++i ) {
        struct timeval s, e;

        ::gettimeofday(&s, 0);
        for( counted[i]=0; counted[i]<sval; ++counted[i] );
        ::gettimeofday(&e, 0);

        deltat[i] = ((double)e.tv_sec + (((double)e.tv_usec)/1.0e6)) -
                    ((double)s.tv_sec + (((double)s.tv_usec)/1.0e6));

        // 1) if it was too short: do not compute average and
        //    make 'sval' larger
        // 2) if it took > 1msec, lower sval, but do compute counts/microsecond.
        // 3) otherwise: just get another measure of "number of counts/microsecond"
        if( deltat[i]<=1.0e-6 ) {
            sval      *= 100;
            deltat[i]  = 0.0;
            counted[i] = 0ULL;
        } else if( deltat[i]>=1.0e-3 ) {
            unsigned long long divider;

            // make sure we do not divide by zero OR end up
            // with an sval of 0
            divider = (unsigned long long)(deltat[i]/2000.0);
            if( divider==0 )
                divider = 2;
            DEBUG(3,"counts_p_usec: took >1msec - dividing by " << divider << endl);
            if( sval==0 )
                sval = 100;
        }
        DEBUG(3, "counts_p_usec[" << i << "]: 0 -> " << counted[i] << " took " << deltat[i] << "s" << endl);
    }

    // accumulate
    total_n_count    = 0ULL;
    total_n_microsec = 0ULL;
    for( unsigned int i=0; i<ntries; ++i ) {
        unsigned long long int n_microsec = (unsigned long long int)(deltat[i] * 1.0e6);

        if( n_microsec==0 )
            continue;
        total_n_microsec += n_microsec;
        total_n_count    += counted[i];
    }

    DEBUG(2, "counts_p_usec/totals: " << total_n_count << " in " << total_n_microsec << "usec" << endl);
    if( !total_n_microsec )
        throw syscallexception("total_n_microsec is 0 in counts_p_microsec()!");
    if( !total_n_count )
        throw syscallexception("total_n_count is 0 in counts_p_microsec()!");


    counts = (total_n_count/total_n_microsec);
    DEBUG(2, "counts_p_usec/counts_p_usec = " << counts << endl);
    ASSERT_NZERO( counts );
    return counts;
}


// Go from memory to net, specialization for UDP.
// * send data out in chunks of "datagramsize"
// * prepend each datagram with a sequencenr 
// * optionally drop packets
// * but not if they seem to contain (part of) a header
void* mem2net_udp( void* argptr ) {
    ssize_t                ntosend;
    runtime*               rte = (runtime*)argptr;
    unsigned int           datagramsize;
    struct iovec           iovect[2];
    struct msghdr          msg;
    unsigned char*         ptr;
    unsigned char*         e_ptr;
    headersearch_type      hdrsrch;
    unsigned long long int seqnr;
    unsigned long long int n_dg_sent;
    unsigned long long int n_dg_to_send;
    unsigned long long int pdr;

    try { 
        const unsigned long long int c_p_usec = counts_per_usec();

        // we're  not to be cancellable.
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Initialize the sequence number with a random 32bit value
        // - just to make sure that the receiver does not make any
        // implicit assumptions on the sequencenr other than that it
        // is linearly increasing.
        ::srandom( (unsigned int)time(0) );
        seqnr = (unsigned long long int)::random();
        cerr << "mem2net_udp: Starting with sequencenr " << seqnr << endl;

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );
        ASSERT2_POS( rte->fd, SCINFO("No socket given (must be >=0)") );
        ASSERT2_NZERO( rte->ntrack(), SCINFO("No tracks configured. Must be >0") );

        // Indicate we're doing mem2net
        rte->frommem_dev   = dev_network;

        // take over values from the runtime
        datagramsize       = rte->netparms.get_datagramsize();
        pdr                = rte->packet_drop_rate;
        // this can be done unconditionally. See below (at (*)
        // why)
        n_dg_to_send       = pdr-1;
        hdrsrch.reset( rte->ntrack() );

        // Initialize stuff that will not change (sizes, some adresses etc)
        msg.msg_name       = 0;
        msg.msg_namelen    = 0;
        msg.msg_iov        = &iovect[0];
        msg.msg_iovlen     = sizeof(iovect)/sizeof(struct iovec);
        msg.msg_control    = 0;
        msg.msg_controllen = 0;
        msg.msg_flags      = 0;

        // part of the iovec can also be filled in [ie: the header]
        iovect[0].iov_base = &seqnr;
        iovect[0].iov_len  = sizeof(seqnr);
        // we always send datagrams of size datagramsize
        iovect[1].iov_len  = datagramsize;

        // Can precompute how many bytes should be sent in a sendmsg call
        ntosend = iovect[0].iov_len + iovect[1].iov_len;

        // and start off with 'no datagrams sent yet' ...
        n_dg_sent          = 0ULL;

        DEBUG(1, "mem2net_udp starting" << endl);
        // enter thread main loop
        while( true ) {
            bool    stop;
            block   blk;

            // attempt to pop a block. Will blocking wait on the queue
            // until either someone push()es a block or cancels the queue
            blk = rte->queue.pop();

            // if we pop an empty block, that signals the queue's been
            // disabled
            if( blk.empty() )
                break;

            // And send it out in packets of 'datagramsize'

            // keep track of nsent out of the loop [could've
            // made it a zuper-local-loop-only var] so we
            // can correctly count how many bytes went to
            // the network, even if we break from the for-loop
            ptr   = (unsigned char*)blk.iov_base;
            e_ptr = ptr + blk.iov_len;
            stop  = false;
            while( !stop && ptr<e_ptr ) {
                bool         send;
                unsigned int ipd;

                iovect[1].iov_base = ptr;

                // test if we must rilly send this pakkit
                send = (pdr==0 || hdrsrch(ptr, datagramsize) ||
                       (n_dg_sent<n_dg_to_send) );
                // attempt send
                if( send ) {
                    ASSERT_COND( ::sendmsg(rte->fd, &msg, MSG_EOR)==ntosend );
                    n_dg_sent++;
                } else {
                    // Ok, we decided not to send this packet
                    // Subtract 'packet-drop-rate' from the number.
                    // We want to drop 1 in every 'pdr' number of packets.
                    // However, it may be that we actually send more than
                    // 'pdr' packets before deciding to drop because of
                    // headerdata being present in the packet we'd like to drop.
                    // By subtracting 'pdr' from the number of sent packets,
                    // we may start counting from a number of sent packets > 0
                    // effecting that we reach 'pdr' sooner, and, as a result of that,
                    // stay as close as we can, to dropping 1 packet every 'pdr'
                    // number of packets.
                    // Note: we may safely do this (unsigned!)
                    // arithmetic since we cannot end up in this 'else' clause
                    // UNLESS pdr>0 AND n_dg_sent>=pdr
                    n_dg_sent -= n_dg_to_send;
                }

                // we (possibly) did send out another datagram.
                // Update loopvariables. We MUST increment the sequencenr
                // irrespectively of whether we did or did not sent the
                // packet: the other side must know that it didn't receive 
                // a packet (in case we decided to drop it) ...
                seqnr++;
                ptr   += datagramsize;

                // Update shared-state variable(s)
                // also gives us a chance to see if we
                // should quit and also update the
                // "inter-packet-delay" and "packet-drop-rate"
                // values; they may have changed since last
                // time we checked
                PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
                rte->nbyte_from_mem += ((send)?(datagramsize):(0));
                stop                 = rte->stop;
                ipd                  = rte->netparms.interpacketdelay;
                // if we detect a change in pdr, we restart
                // counting sent packets
                if( rte->packet_drop_rate!=pdr )
                    n_dg_sent = 0;
                // (*) We can do the following unconditionally
                // (especially the unsigned "pdr-1" arithmetic).
                // It only produces an incorrect value for 'n_dg_to_send'
                // when pdr==0. At the same time, that is the very condition
                // under which the value of 'n_dg_to_send' will be ignored ...
                pdr                  = rte->packet_drop_rate;
                n_dg_to_send         = pdr-1;
                PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

                // test for premature loop continue
                // if we didn't send a packet, there's no need to
                // delay either!
                if( stop || ipd==0 || !send )
                    continue;

                // Ok not stopped AND do interpacketdelay [units is usec]
                for(volatile unsigned long long int i = 0;
                        i < (ipd * c_p_usec); ++i );
            }
            if( stop )
                break;
        }
        DEBUG(1, "mem2net_udp stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "mem2net_udp caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "mem2net_udp caught unknown exception?!" << endl;
    }
    if( rte )
        rte->frommem_dev = dev_none;
    return (void*)0;
}

// specialization of mem2net for reliable links: just a blind
// write to the network.
void* mem2net_tcp( void* argptr ) {
    runtime*            rte = (runtime*)argptr;
    struct iovec        iovect[1];
    struct msghdr       msg;

    try { 
        // we're  not to be cancellable.
        // disable the queue and/or 
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Assert that we did get some arguments
        ASSERT2_NZERO( rte, SCINFO("Nullpointer threadargument!") );
        ASSERT2_POS( rte->fd, SCINFO("No socket given (must be >=0)") );

        // Indicate we're doing mem2net
        rte->frommem_dev   = dev_network;

        // Initialize stuff that will not change (sizes, some adresses etc)
        msg.msg_name       = 0;
        msg.msg_namelen    = 0;
        msg.msg_iov        = &iovect[0];
        msg.msg_iovlen     = sizeof(iovect)/sizeof(struct iovec);
        msg.msg_control    = 0;
        msg.msg_controllen = 0;
        msg.msg_flags      = 0;

        // enter thread main loop
        DEBUG(1, "mem2net_tcp starting" << endl);
        while( true ) {
            bool         stop;
            block        blk;

            // attempt to pop a block. Will blocking wait on the queue
            // until either someone push()es a block or cancels the queue
            blk = rte->queue.pop();

            // if we pop an empty block, that signals the queue's been
            // disabled
            if( blk.empty() )
                break;

            // And send it out in one chunk
            iovect[0].iov_base = blk.iov_base;
            iovect[0].iov_len  = blk.iov_len;

            ASSERT_COND( ::sendmsg(rte->fd, &msg, MSG_EOR)==(ssize_t)blk.iov_len );

            // Update shared-state variable(s)
            // also gives us a chance to see if we
            // should quit
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            rte->nbyte_from_mem += blk.iov_len;
            stop                 = rte->stop;
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
            
            if( stop )
                break;
        }
        DEBUG(1, "mem2net_tcp stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "mem2net_tcp caught exception:\n"
             << e.what() << endl;
    }
    catch( ... ) {
        cerr << "mem2net_tcp caught unknown exception?!" << endl;
    }
    if( rte )
        rte->frommem_dev = dev_none;
    return (void*)0;
}

// thread function which reads data from the net into memory
// This is the "straight-through" implementation meant for
// reliable links: everything that is sent is expected to be
// received AND in the same order it was sent.
// This function assumes that when 'run' is signalled for
// the first time, filedescriptor
// "runtime->fd" is the fd for incoming data.
// Other part(s) of the system should take care of accepting
// any connections, if applicable.
// Currently, it is the "main()" thread that does the accepting:
// as it is already in an infinite loop of doing just that + 
// handling the incoming commands it seemed the logical place to
// do that.

struct helperargs_type {
    int            fd;
    runtime*       rte;
    unsigned int   nblock;
    unsigned int   blocksize;
    unsigned int   datagramsize;
    unsigned char* buffer;

    helperargs_type():
        fd( -1 ), rte( 0 ), nblock( 0 ),
        blocksize( 0 ), datagramsize( 0 ), buffer( 0 )
    {}
};

// tcp helper. does blocking reads a block of size "blocksize"
void* tcphelper( void* harg ) {
    helperargs_type*   hlp( (helperargs_type*)harg );
    // message stuff
    struct iovec       iov;
    struct msghdr      msg;

    // if no argument, bail out.
    // otherwise, we blindly trust what we're given
    if( !hlp ) {
        DEBUG(0, "tcphelper called with NULL-pointer" << endl);
        return (void*)1;
    }
    // We are expected to be cancellable. Make it so.
    // First set canceltype, than enable it
    THRD_CALL( ::pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0) );
    THRD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0) );

    // set up the message

    // no name
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;
    // one fragment only, the datapart
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // The size of the parts of the message are known
    // and for the sequencenumber, we already know the destination
    // address
    iov.iov_len     = hlp->blocksize;

    try {
        // this is known (and fixed). Safe for blocksize up to 2G
        // i hope
        const int      n2read = (int)hlp->blocksize;
        // variables
        unsigned int   idx;

        // Make rilly RLY sure the zocket/fd is in blocking mode
        setfdblockingmode(hlp->fd, true);

        idx                    = 0;
        hlp->rte->nbyte_to_mem = 0ULL;

        // now go into our mainloop
        DEBUG(1, "tcphelper starting mainloop on fd#" << hlp->fd << ", expect " << n2read << endl);
        while( true ) {
            // read the message
            iov.iov_base = (void*)(hlp->buffer + idx*hlp->blocksize);
            pthread_testcancel();
            ASSERT_COND( ::recvmsg(hlp->fd, &msg, MSG_WAITALL)==n2read );
            pthread_testcancel();

            // push only fails when the queue is 'cancelled'(disabled)
            if( hlp->rte->queue.push(block(iov.iov_base, iov.iov_len))==false ) {
                DEBUG(1, "tcphelper detected queue-cancel!" << endl);
                break;
            }

            // updata number-of-bytes transferred to memry
            hlp->rte->nbyte_to_mem += iov.iov_len;

            // and move on to nxt block
            idx = (idx+1)%hlp->nblock;
        }
        DEBUG(1, "tcphelper stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "tcphelper got exception: " << e.what() << endl;
    }
    catch( ... ) {
        cerr << "tcphelper got unknown exception!" << endl;
    }
    return (void*)0;
}

// udp version. Uses 'datagrams' + sequencenumbers
void* udphelper_smart( void* harg ) {
    helperargs_type*             hlp( (helperargs_type*)harg );
    struct iovec                 iov[2];
    struct msghdr                msg;
    unsigned long long int       seqnr;
    unsigned long long int       firstseqnr;
    unsigned long long int       maxseqnr;
    const unsigned long int      fp   = 0x11223344;
    const unsigned long long int fill = (((unsigned long long int)fp << 32) + fp);

    // if no argument, bail out.
    // otherwise, we blindly trust what we're given
    if( !hlp ) {
        DEBUG(0, "udphelper_smart called with NULL-pointer" << endl);
        return (void*)1;
    }

    // we require at least 3 blocks for this algorithm to work
    if( hlp->nblock<=2 ) {
        DEBUG(0, "udphelper_smart needs at least 3 blocks in array, only " << hlp->nblock 
                 << " present; prematurely stopping thread!" << endl);
        return (void*)1;
    }
    // We are expected to be cancellable. Make it so.
    // First set canceltype, than enable it
    THRD_CALL( ::pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0) );
    THRD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0) );

    // set up the message

    // no name
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;
    // two fragments. Sequence number and datapart
    msg.msg_iov        = &iov[0];
    msg.msg_iovlen     = 2;
    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // The size of the parts of the message are known
    // and for the sequencenumber, we already know the destination
    // address
    iov[0].iov_base    = &seqnr;
    iov[0].iov_len     = sizeof(seqnr);
    iov[1].iov_len     = hlp->datagramsize;

    try {
        // these are known (and fixed):
        // we should be safe for datagrams up to 2G i hope
        const int          n2read = (int)(iov[0].iov_len + iov[1].iov_len);
        const unsigned int nblock( hlp->nblock );
        const unsigned int blocksize( hlp->blocksize );
        const unsigned int dgsize( hlp->datagramsize );
        const unsigned int n_dg_p_block( hlp->blocksize/hlp->datagramsize );
        const unsigned int n_dg_p_buf( n_dg_p_block * hlp->nblock );
        const unsigned int n_ull_p_block( hlp->blocksize/sizeof(unsigned long long int) );
        // variables
        bool                    first[ nblock ];
        bool                    dgflag[ n_dg_p_buf ];
        unsigned int            idx;
        unsigned int            bidx;
        unsigned int            dgidx;
        unsigned char*          buffer( (unsigned char*)hlp->buffer );
        evlbi_stats_type&       es( hlp->rte->evlbi_stats );

        // Make rilly RLY sure the zocket/fd is in blocking mode
        setfdblockingmode(hlp->fd, true);

        idx          = 0;
        dgidx        = 0;

        for( unsigned int i=0; i<nblock; ++i )
            first[i] = true;
        for( unsigned int i=0; i<n_dg_p_buf; ++i )
            dgflag[i] = false;

        // reset evlbi statistics
        es = evlbi_stats_type();

        // now go into our mainloop
        DEBUG(1, "udphelper_smart starting mainloop on fd#" << hlp->fd << ", expect " << n2read << endl);
        while( true ) {
            // keep on reading until we are 2 blocks ahead
            // 'idx' is the index of the block that will be released
            // when 'bidx' [the block in which we are currently writing]
            // gets to be at least two units larger than idx.
            // 'bidx' is derived from the datagramindex that we are currently
            // writing into
            while( ::abs((int)(bidx=(dgidx/n_dg_p_block))-(int)idx)<2 ) {
                // the first time we touch a block, we initialize it with fillpattern
                if( first[bidx] ) {
                    unsigned long long int* ullptr = (unsigned long long int*)
                                            (buffer + bidx * blocksize); 
                    for(unsigned int i=0; i<n_ull_p_block; ++i)
                        ullptr[i] = fill;
                    first[ bidx ] = false;
                }

                // new packet always goes at "maxseqnr+1".
                iov[1].iov_base = (void*)(buffer + dgidx*dgsize);

                // do receive a datagram
                pthread_testcancel();
                ASSERT_COND( ::recvmsg(hlp->fd, &msg, MSG_WAITALL)==n2read );
                pthread_testcancel();

                if( (es.pkt_total++)==0 ) {
                    firstseqnr = maxseqnr = seqnr;
                    cerr << "udphelper_smart: first sequencenr received " << firstseqnr << endl;
                }

                // check if it rly should be where it's at
                long long int      diff  = (seqnr-firstseqnr);
                const unsigned int dgpos = (diff % n_dg_p_buf);

                // datagrams arriving with sequencenr < firstseqnr are totally discarded
                if( diff<0 ) {
                    es.pkt_ooo++;
                    es.pkt_lost++;
                    continue;
                }
                // only if the computed location is not where it should be,
                // move it!
                if( dgpos!=dgidx ) {
                    // record an out-of-order event
                    es.pkt_ooo++;

                    // if the destination location already taken
                    // do NOT overwrite it but signal it by
                    // upping the "repeat" counter
                    if( dgflag[dgidx] ) {
                        es.pkt_rpt++;
                    } else {
                        // compute the location where the datagram *should* be
                        unsigned char*  location = buffer + dgpos*dgsize; 

                        // and do copy the data
                        memcpy( (void*)location, iov[1].iov_base, dgsize );
                    }
                }
                // dgpos will *always* be the position at which we wrote
                // the datagram, be it out-of-order or not
                dgflag[ dgpos ] = true;

                // see if we got a new 'maximum' sequencenr
                if( seqnr>maxseqnr )
                    maxseqnr = seqnr;

                // We expect that the next datagram coming in will be
                // the "maxseqnr+1", so compute the datagram-idx for 
                // *that* position
                dgidx = (((maxseqnr-firstseqnr)+1) % n_dg_p_buf);

                // that position should better be empty!
                // for now we just scream
                if( dgflag[dgidx] ) {
                    cerr << "AAARGH - udphelper_smart: "
                         << "about to overwrite previously filled datagramposition"
                         << endl;
                }
            } 
            // Now we can (try to) push the block at 'idx'
            // push only fails when the queue is 'cancelled' (disabled)
            if( hlp->rte->queue.push(block(buffer+idx*blocksize, blocksize))==false ) {
                DEBUG(1, "udphelper_smart detected queue-cancel!" << endl);
                break;
            }
            hlp->rte->nbyte_to_mem += blocksize;

            // this blocks needs initializing next time we visit it
            first[ idx ] = true;
            // whilst resetting the datagram flags, count how many were not
            // filled in
            for( unsigned int i=idx*n_dg_p_block, j=0; j<n_dg_p_block; ++i, ++j) {
                if( !dgflag[i] )
                    es.pkt_lost++;
                dgflag[ i ] = false;
            }

            // and move on to nxt block
            idx = ((idx+1)%nblock);
        }
        DEBUG(1, "udphelper_smart stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "udphelper_smart got exception: " << e.what() << endl;
    }
    catch( ... ) {
        cerr << "udphelper_smart got unknown exception!" << endl;
    }
    return (void*)0;
}

void* udphelper_st( void* harg ) {
    helperargs_type*       hlp( (helperargs_type*)harg );
    // message stuff
    struct iovec           iov[2];
    struct msghdr          msg;
    unsigned long long int seqnr;
    unsigned long long int firstseqnr;
    unsigned long long int maxseqnr;

    // if no argument, bail out.
    // otherwise, we blindly trust what we're given
    if( !hlp ) {
        DEBUG(0, "udphelper_st called with NULL-pointer" << endl);
        return (void*)1;
    }
    // We are expected to be cancellable. Make it so.
    // First set canceltype, than enable it
    THRD_CALL( ::pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0) );
    THRD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0) );

    // set up the message

    // no name
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;
    // two fragments. Sequence number and datapart
    msg.msg_iov        = &iov[0];
    msg.msg_iovlen     = 2;
    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // The size of the parts of the message are known
    // and for the sequencenumber, we already know the destination
    // address
    iov[0].iov_base    = &seqnr;
    iov[0].iov_len     = sizeof(seqnr);
    iov[1].iov_len     = hlp->datagramsize;

    try {
        // this is known (and fixed):
        // we should be safe for datagrams up to 2G i hope
        const int         n2read = (int)(iov[0].iov_len + iov[1].iov_len);
        // variables
        unsigned int      idx;
        unsigned int      n_dg_p_block;
        unsigned char*    ptr;
        evlbi_stats_type& es( hlp->rte->evlbi_stats );

        // Make rilly RLY sure the zocket/fd is in blocking mode
        setfdblockingmode(hlp->fd, true);

        idx          = 0;
        n_dg_p_block = hlp->blocksize/hlp->datagramsize;

        // reset evlbi statistics
        es = evlbi_stats_type();

        // now go into our mainloop
        DEBUG(1, "udphelper_st starting mainloop on fd#" << hlp->fd << ", expect " << n2read << endl);
        while( true ) {
            // compute location of current block
            ptr = hlp->buffer + idx*hlp->blocksize;

            // read parts
            for( unsigned int i=0; i<n_dg_p_block; ++i) {
                iov[1].iov_base = (void*)(ptr + i*hlp->datagramsize);
                pthread_testcancel();
                ASSERT_COND( ::recvmsg(hlp->fd, &msg, MSG_WAITALL)==n2read );
                pthread_testcancel();

                // if this is the very first pkt we receive, initialize our
                // 'expected sequencenumber' variable
                if( (es.pkt_total++)==0 ) {
                    firstseqnr = maxseqnr = seqnr;
                    cerr << "udphelper_st: first sequencenr received " << firstseqnr << endl;
                } else {
                    long long int  dmax = (seqnr-maxseqnr);

                    // if the seqnr < maximumseqnr, it's definitely out-of-order.
                    if( dmax<0 ) {
                        DEBUG(2, "udphelper_st: OOO: d=" << format("%8lld",dmax)
                            << " C: " << seqnr << " T: " << es.pkt_total << " M: " << maxseqnr << endl);
                        es.pkt_ooo++;
                        if( seqnr>firstseqnr )
                            es.pkt_lost--;
                    } else if( dmax>0 ) {
                        // we may have lost pakkits.
                        // Typically, we expect "current = max+1"
                        // so if dmax > 1, we lost pakkits!
                        if( dmax>1 ) {
                            es.pkt_lost += (dmax-1);
                            DEBUG(2,"udphelper_ct: LST: d=" << format("%8lld",dmax)
                                << " C: " << seqnr << " T: " << es.pkt_total << " M: " << maxseqnr << endl);
                        }
                        // whatever was the case: current max is this one
                        maxseqnr = seqnr;
                    } else {
                        // dmax==0 => repeat?!
                        es.pkt_rpt++;
                    }
                }
            }
            // push only fails when the queue is 'cancelled'(disabled)
            if( hlp->rte->queue.push(block(ptr, hlp->blocksize))==false ) {
                DEBUG(1, "udphelper_st detected queue-cancel!" << endl);
                break;
            }
            hlp->rte->nbyte_to_mem += hlp->blocksize;
            // and move on to nxt block
            idx = (idx+1)%hlp->nblock;
        }
        DEBUG(1, "udphelper_st stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "udphelper_st got exception: " << e.what() << endl;
    }
    catch( ... ) {
        cerr << "udphelper_st got unknown exception!" << endl;
    }
    return (void*)0;
}



const udphelper_maptype& udphelper_map( void ) {
    static udphelper_maptype   __map = udphelper_maptype();

    if( __map.size() )
        return __map;

    // do fill the map
    // don't check - really we should do ...
    __map.insert( make_pair("smart", udphelper_smart) );
    __map.insert( make_pair("st", udphelper_st) );

    return __map;
}




// main net2mem thread. Depends on helper thread which does the
// blocking I/O. This one is NOT blocking and will do
// resource allocation and cleaning up if we decide to
// stop this transport
void* net2mem( void* argptr ) {
    int                 rcvbufsz;
    bool                stop;
    string              udphelper;
    runtime*            rte = (runtime*)argptr;
    pthread_t*          thrid = 0;
    helperargs_type     hlpargs;
    const unsigned int  olen( sizeof(rcvbufsz) );

    try {
        // We are not cancellable. Signal us to stop
        // via cond_broadcast.
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0) );

        // Good. Assert we did get a pointer
        ASSERT2_NZERO( rte, SCINFO(" Nullpointer thread-argument!") );

        // indicate we're doing net2mem
        rte->tomem_dev       = dev_network;

        // Now get settings from the runtime and 
        // prepare the buffer part of the helper-thread
        // argument thingy.
        // Allocate more blocks than will fit in the
        // queue! If a block is in the queue we shouldn't
        // overwrite it so by allocating more block(s) than
        // will fit in the queue we can always fill up the
        // queue and *still* have (a) block(s) available for
        // writing to.
        PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
        rcvbufsz             = rte->netparms.rcvbufsize;
        udphelper            = rte->netparms.udphelper;
        hlpargs.rte          = rte;
        hlpargs.nblock       = rte->netparms.nblock + 4;
        hlpargs.blocksize    = rte->netparms.get_blocksize();
        hlpargs.datagramsize = rte->netparms.get_datagramsize();
        PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
        hlpargs.buffer       = new unsigned char[ hlpargs.nblock * hlpargs.blocksize ];

        // Only the "fd" needs to be filled in.
        // We wait for 'start' to become true [or cancel]
        // for that is the signal that "rte->fd" has gotten
        // a sensible value
        PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
        while( !rte->run && !rte->stop )
            PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
        stop       = rte->stop;
        hlpargs.fd = rte->fd;
        PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );

        // if not stop, fire up helper thread
        if( !stop ) {
            void*          (*fptr)(void*) = 0;
            sigset_t       oss, nss;
            pthread_attr_t tattr;

            // Set the rcv bufsize on the filedescriptor
            ASSERT_ZERO( ::setsockopt(hlpargs.fd, SOL_SOCKET, SO_RCVBUF, &rcvbufsz, olen) );
            // set up for a detached thread with ALL signals blocked
            ASSERT_ZERO( ::sigfillset(&nss) );

            PTHREAD_CALL( ::pthread_attr_init(&tattr) );
            PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE) );
            PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &nss, &oss) );

            thrid = new pthread_t;

            // Decide which helperthread to start
            if( rte->netparms.get_protocol()=="tcp" ) {
                fptr = tcphelper;
            } else {
                const udphelper_maptype&          helpermap( udphelper_map() );
                udphelper_maptype::const_iterator curhelper;

                curhelper = helpermap.find( udphelper );
                ASSERT2_COND( (curhelper!=helpermap.end()), SCINFO(" no helper-function "
                                  << "for '" << udphelper << "'"));
                fptr = curhelper->second;
            }
            PTHREAD2_CALL( ::pthread_create(thrid, &tattr, fptr, &hlpargs),
                          delete thrid; thrid=0; );
            // good put back old sigmask
            PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &oss, 0) );
            // and destroy resources
            PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );
        }

        //  helper thread is started (if we're not 'stop'ed)
        while( !stop ) {
            // wait until we *are* stopped!
            PTHREAD_CALL( ::pthread_mutex_lock(rte->mutex) );
            while( !rte->stop )
                PTHREAD_CALL( ::pthread_cond_wait(rte->condition, rte->mutex) );
            PTHREAD_CALL( ::pthread_mutex_unlock(rte->mutex) );
            // great! we fell out of the while() cond_wait(); loop so it's time to
            // call it a day!
            break;
        }
        // now we bluntly cancel the helper thread.
        if( thrid ) {
            DEBUG(1, "net2mem: Cancelling helper thread" << endl);
            ::pthread_cancel(*thrid);
            // and wait for it to be gone
            PTHREAD_CALL( ::pthread_join(*thrid, 0) );
            DEBUG(1, "net2mem: helper thread joined" << endl);
        }
        DEBUG(1, "net2mem: stopping" << endl);
    }
    catch( const exception& e ) {
        cerr << "net2mem: caught exception: " << e.what() << endl;
    }
    catch( ... ) {
        cerr << "net2mem: caught unknown exception" << endl;
    }

    if( rte )
        rte->tomem_dev = dev_none;
    // do cleanup
    delete [] hlpargs.buffer;
    delete thrid;

    return (void*)0;
}
