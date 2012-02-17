// implementations of the threadfunctions
// Copyright (C) 2007-2008 Harro Verkouter
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
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
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
#include <busywait.h>
#include <timewrap.h>
#include <stringutil.h>
#include <sciprint.h>
#include <boyer_moore.h>
#include <sse_dechannelizer.h>

#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm> // for std::min 
#include <queue>
#include <list>

#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>
#include <stdlib.h> // for ::llabs()
#include <limits.h>
#include <stdarg.h>
#include <time.h>   // for ::clock_gettime


using namespace std;

typedef volatile int64_t   counter_type;
typedef volatile uint64_t  ucounter_type;

// When dealing with circular buffers these macro's give you the
// wrap-around-safe next and previous index given the current index and the
// size of the circular buffer.
// CIRCDIST: always returns a positive distance between your b(egin) and
// e(nd) point
#define CIRCNEXT(cur, sz)   ((cur+1)%sz)
#define CIRCPREV(cur, sz)   CIRCNEXT((cur+sz-2), sz)
#define CIRCDIST(b , e, sz) (((b>e)?(sz-b+e):(e-b))%sz)

// dummy empty function meant to install as signal-handler
// that doesn't actually do anything.
// it is necessary to be able to, under linux, wake up
// a thread from a blocking systemcall: send it an explicit
// signal. in order to limit sideeffects we use SIGUSR1
// and install a signal handler "dat don't do nuttin'"
void zig_func(int) {}

void install_zig_for_this_thread(int sig) {
    sigset_t      set;

    // Unblock the indicated SIGNAL from the set all all signals
    sigfillset(&set);
    sigdelset(&set, sig);
    // We do not care about the existing signalset
    ::pthread_sigmask(SIG_SETMASK, &set, 0);
    // install the empty handler 'zig()' for this signal
    ::signal(sig, zig_func);
}

// thread arguments struct(s)

dplay_args::dplay_args():
    rot( 0.0 ), rteptr( 0 )
{}



// delayed play thread function.
// will wait until ROT for argptr->rteptr->current_taskid reaches
// argptr->rot [if >0.0]. If argptr->rot NOT >0.0 it will
// act as an immediate play.
void* delayed_play_fn( void* dplay_args_ptr ) {
    dplay_args*  dpaptr = (dplay_args*)dplay_args_ptr;
    if( !dpaptr ) {
        DEBUG(-1, "delayed_play_fn: passed a NULL-pointer as argument?!" << endl);
        return (void*)0;
    }

    // start off by being uncancellable
    // note: not called via 'PTHREAD_CALL()' macro since that
    // _may_ throw but at this point we're not within a
    // try-catch block and throwing exceptions across
    // threads is NOT A GOOD THING. Actually, it's very bad.
    // It doesn't work either.
    int oldstate;
    ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

    // fine, knowing that argptr!=0, we can create a copy
    // of the arguments
    double      rot( ((const dplay_args*)dplay_args_ptr)->rot );
    runtime*    rteptr( ((const dplay_args*)dplay_args_ptr)->rteptr );
    playpointer pp_start( ((const dplay_args*)dplay_args_ptr)->pp_start );

    try {
        pcint::timediff                  tdiff;

        // during the sleep/wait we may be cancellable
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate) );

        // wait for the actual ROT - we try to approximate it to about a millisecond
        do {
            pcint::timeval_type              start( pcint::timeval_type::tv_now );
            task2rotmap_type::const_iterator task2rotptr;

            // find the rot-to-systime mapping if a taskid given
            // Note: we keep on evaluating the start-time since the mapping
            // from systime->ROT can be updated whilst we are waiting.
            RTEEXEC(*rteptr,
                task2rotptr=rteptr->task2rotmap.find(rteptr->current_taskid);
                ASSERT2_COND(rteptr->current_taskid!=runtime::invalid_taskid &&
                             task2rotptr!=rteptr->task2rotmap.end(),
                             SCINFO("No ROT->systime mapping for JOB#"
                                    << rteptr->current_taskid << endl)) );
            // compute what the desired start-time in localtime is
            // based on requestedrot - lastknownrot (in wallclockseconds,
            // NOT ROT-seconds; they may be speedupified or slowdownified)
            if( task2rotptr!=rteptr->task2rotmap.end() ) {
                double       drot;
                rot2systime  r2tmap;

                r2tmap = task2rotptr->second;
                drot   = (rot - r2tmap.rot)/r2tmap.rotrate;
                start   = r2tmap.systime + drot;
            }
            tdiff = start - pcint::timeval_type::now();

            // if this condition holds, we're already (way?) past
            // the ROT we're supposed to start at
            if( tdiff<=0.0 )
                break;

            // if we have to sleep > 1second, we use ordinary sleep
            // as soon as it falls below 1 second, we start using usleep
            DEBUG(1, "delayed_play_fn: sleeping for " << ((double)tdiff)/2.0 << "s" << endl);
            if( tdiff>2.0 )
                ::sleep( (unsigned int)(tdiff/2.0) );
            else 
                ::usleep( (unsigned int)(tdiff*1.0e6/2.0) );
            // compute _actual_ diff [amount of sleep may not quite
            // be what we requested]
            tdiff = start - pcint::timeval_type::now();
        } while( tdiff>1.0e-3 );
        DEBUG(1, "delayed_play_fn: wait-loop finished" << endl);

        // now disable cancellability
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) );
        
        XLRCALL( ::XLRPlayback(rteptr->xlrdev.sshandle(),
                               pp_start.AddrHi, pp_start.AddrLo) );

        // do not forget to update the transfersubmodeflags
        RTEEXEC(*rteptr, rteptr->transfersubmode.clr(wait_flag).set(run_flag));
        DEBUG(1, "delayed_play_fn: now playing back" << endl);
    }
    catch( const std::exception& e ) {
        DEBUG(0, "delayed_play_fn: caught deadly exception - " << e.what() << endl);
    }
    catch( ... ) {
        DEBUG(0, "delayed_play_fn: caught unknown exception" << endl);
    }
    // boohoo! we're done!
    return (void*)0;
}

string blockpool_memstat_fn(blockpool_type* bp) {
    bp->show_usecnt();
    return string("blockpool status");
}

void fillpatterngenerator(outq_type<block>* outq, sync_type<fillpatargs>* args) {
    bool          stop;
    runtime*      rteptr;
    uint64_t      wordcount;
    thunk_type    oldmemstat;
    fillpatargs*  fpargs = args->userdata;

    // Assert we do have a runtime pointer!
    ASSERT2_COND(rteptr = fpargs->rteptr, SCINFO("OH NOES! No runtime pointer!"));

    const uint64_t bs = fpargs->rteptr->sizes[constraints::blocksize];
    const uint64_t nfill_per_block = (bs/sizeof(fpargs->fill));

    // Create the blockpool. The allocation unit is 32 blocks per pool
    // (let's see how this works out)
    SYNCEXEC(args,
             fpargs->pool = new blockpool_type(bs, 32));

    // wait for the "GO" signal
    args->lock();
    while( !args->cancelled && !fpargs->run )
        args->cond_wait();
    // whilst we have the lock, do copy important values across
    stop = args->cancelled;
    const uint64_t nword = args->userdata->nword;
    args->unlock();

    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "Fill", 0));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    if( stop ) {
        DEBUG(0, "fillpatterngenerator: cancelled before starting" << endl);
        return;
    }
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.clr(wait_flag).set(run_flag);
            oldmemstat = rteptr->set_memstat_getter(makethunk(&blockpool_memstat_fn, fpargs->pool)) );

    DEBUG(0, "fillpatterngenerator: starting" << endl);
    wordcount = nword;
    while( wordcount>=nfill_per_block ) {
        block     b = fpargs->pool->get();
        uint64_t* bptr = (uint64_t*)b.iov_base;

        for(unsigned int i=0; i<nfill_per_block; i++)
            bptr[i] = fpargs->fill;

        if( outq->push(b)==false )
            break;

        wordcount    -= nfill_per_block;
        fpargs->fill += fpargs->inc;

        // update global statistics
        counter += bs;
    }
    RTEEXEC(*rteptr,
            rteptr->set_memstat_getter(oldmemstat) );
    DEBUG(0, "fillpatterngenerator: done." << endl);
    DEBUG(0, "   req:" << nword << ", leftover:" << wordcount
             << " (blocksize:" << nfill_per_block << "words/" << bs << "bytes)." << endl);
    return;
}

void framepatterngenerator(outq_type<block>* outq, sync_type<fillpatargs>* args) {
    bool           stop;
    runtime*       rteptr;
    uint64_t       framecount = 0;
    uint64_t       wordcount;
    fillpatargs*   fpargs = args->userdata;

    // Assert we do have a runtime pointer!
    ASSERT2_COND(rteptr = fpargs->rteptr, SCINFO("OH NOES! No runtime pointer!"));

    // construct a headersearchtype. it must not evaluate to 'false' when
    // casting to bool
    const headersearch_type header(rteptr->trackformat(), rteptr->ntrack(), (unsigned int)rteptr->trackbitrate());
    ASSERT2_COND(header, SCINFO("Request to generate frames of fillpattern of unknown format"));

    // Now we can safely compute these 
    const uint64_t     bs = fpargs->rteptr->sizes[constraints::blocksize];
    // Assume: payload of a dataframe follows after the header, which
    // containst the syncword which starts at some offset into to frame
    const uint64_t      n_ull_p_frame = header.framesize / sizeof(uint64_t);
    const uint64_t      n_ull_p_block = bs / sizeof(uint64_t);

    // Create a blockpool - allocate 128 blocks per cycle
    SYNCEXEC(args,
             fpargs->pool = new blockpool_type(bs, 16));

    // Pointers we use
    // We keep one frame which we update and copy (by "blocksize" chunks)
    // into blocks which we push downstream
    unsigned char* const    frame = new unsigned char[ header.framesize ];
    unsigned char* const    frameend = frame + header.framesize;
    unsigned char*          frameptr;

    // Request a counter
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "Fill", 0));

    // wait for the "GO" signal
    args->lock();
    while( !args->cancelled && !fpargs->run )
        args->cond_wait();
    // whilst we have the lock, do copy important values across
    stop = args->cancelled;
    const uint64_t nword = args->userdata->nword;
    args->unlock();

    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    if( stop ) {
        DEBUG(0, "framepatterngenerator: cancelled before starting" << endl);
        return;
    }
    RTEEXEC(*rteptr, rteptr->transfersubmode.clr(wait_flag).set(run_flag));

    DEBUG(0, "framepatterngenerator: start generating " << nword << " words, formatted as " << header << " frames" << endl)
    frameptr  = frame;
    wordcount = nword;
    while( wordcount>n_ull_p_block ) {
        // produce a new block's worth of frames
        block                 b     = fpargs->pool->get(); 
        unsigned char*        bptr  = (unsigned char*)b.iov_base;
        unsigned char* const  beptr = bptr + b.iov_len;

        // Keep on copying/generating frames until the block's filled up
        while( bptr<beptr ) {
            // must we generate a new frame?
            if( frameptr==frame ) {
                uint64_t*   ull = (uint64_t*)frameptr;
                // fill whole block with (current) fillpattern
                for( unsigned int i=0; i<n_ull_p_frame; i++ )
                    ull[i] = fpargs->fill;
                // write the syncword at the correct position
                ::memcpy( (void*)(frameptr + header.syncwordoffset), (void*)header.syncword, header.syncwordsize );
                fpargs->fill += fpargs->inc;
                framecount++;
            }

            // How many bytes can/must we copy?
            const unsigned int byte_needed = (unsigned int)(beptr-bptr);
            const unsigned int byte_avail  = (unsigned int)(frameend-frameptr);
            const unsigned int n_copy      = std::min(byte_needed, byte_avail);

            // ok, now do the copy action
            ::memcpy((void*)bptr, (void*)frameptr, n_copy);

            // update pointers
            bptr     += n_copy;
            frameptr += n_copy;

            // if we've emptied the frame, set the condition which will
            // create a new one
            if( frameptr==frameend )
                frameptr=frame;
        }

        // Ok we has a blocks!
        if( outq->push(b)==false )
            break;

        wordcount -= n_ull_p_block;

        // update global statistics
        counter += bs;
    }
    // clean up
    delete [] frame;

    DEBUG(0, "framepatterngenerator: done." << endl);
    DEBUG(0, "   req:" << nword << " words, leftover:" << wordcount
             << " (generated " << framecount << " x " << header << " frames)" << endl);
    return;
}

// Look at the actual frameformat in the runtime. If no format given, start
// generating anonymous blocks, otherwise start generating frames of the
// correct persuasion
void fillpatternwrapper(outq_type<block>* oqptr, sync_type<fillpatargs>* args) {
    ASSERT_COND( args );
    ASSERT_COND( args->userdata->rteptr );

    if( args->userdata->rteptr->trackformat()==fmt_none )
        fillpatterngenerator(oqptr, args);
    else
        framepatterngenerator(oqptr, args);
}

// The threadfunctions are now moulded into producers, steps and consumers
// so we can use them in a chain

void fiforeader(outq_type<block>* outq, sync_type<fiforeaderargs>* args) {
    // If we must empty the FIFO we must read the data to somewhere so
    // we allocate an emergency block
    const DWORDLONG    hiwater = (512*1024*1024)/2;
    const unsigned int num_unsignedlongs = 256000;

    // Make sure we're not 'made out of 0-pointers'
    ASSERT_COND( args && args->userdata && args->userdata->rteptr );

    // automatic variables
    bool               stop;
    runtime*           rteptr = args->userdata->rteptr;
    SSHANDLE           sshandle;
    READTYPE*          emergency_block = 0;
    DWORDLONG          fifolen;
    fiforeaderargs*    ffargs = args->userdata;

    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int blocksize = ffargs->rteptr->sizes[constraints::blocksize];

    // allocate enough working space.
    // we include an emergency blob of size num_unsigned
    // long ints at the end. the read loop implements a circular buffer
    // of nblock entries of size blocksize so it will never use/overwrite
    // any bytes of the emergency block.
    SYNCEXEC( args,
              ffargs->pool = new blockpool_type(blocksize, 16) );

    // For emptying the fifo if downstream isn't fast enough
    emergency_block = new READTYPE[num_unsignedlongs];

    // Request a counter for counting into
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "Fifo", 0));

    // Provide unconditional and unlocked access to A counter,
    // which, if everything goes to plan, might even be THE counter
    counter_type& counter( rteptr->statistics.counter(args->stepid) );

    // Wait for 'start' or possibly a cancelled 
    // Copy shared state variable whilst be still have the mutex.
    // Only have to get 'cancelled' since if it's 'true' the value of
    // run is insignificant and if it's 'false' then run MUST be
    // true [see while() condition...]
    args->lock();
    while( !ffargs->run && !args->cancelled )
        args->cond_wait();
    // Ah. At least one of the conditions was met.
    stop     = args->cancelled;
    args->unlock();

    // Premature cancellation?
    if( stop ) {
        DEBUG(0, "fiforeader: cancelled before start" << endl);
        return;
    }

    // Now we can indicate we're running!
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.clr(wait_flag).set(run_flag);
            sshandle = rteptr->xlrdev.sshandle());

    DEBUG(0, "fiforeader: starting" << endl);

    // Now, enter main thread loop.
    while( !args->cancelled ) {
        // Make sure the FIFO is not too full
        // Use a (relatively) large block for this so it will work
        // regardless of the network settings
        do_xlr_lock();
        while( (fifolen=::XLRGetFIFOLength(sshandle))>hiwater ) {
            // Note: do not use "XLR_CALL*" macros since we've 
            //       manually locked access to the streamstor.
            //       Invoking an XLR_CALL macro would make us 
            //       deadlock since those macros will first
            //       try to lock access ...
            //       so only direct ::XLR* API calls in here!
            if( ::XLRReadFifo(sshandle, emergency_block, (num_unsignedlongs * sizeof(READTYPE)), 0)!=XLR_SUCCESS ) {
                do_xlr_unlock();
                throw xlrexception("Failure to XLRReadFifo whilst trying "
                        "to get below hiwater mark!");
            }
        }
        do_xlr_unlock();
        // Depending on if enough data available or not, do something
        if( fifolen<blocksize ) {
            struct timespec  ts;

            // Let's sleep for, say, 100u sec 
            // we don't care if we do not fully sleep the requested amount
            ts.tv_sec  = 0;
            ts.tv_nsec = 100000;
            ASSERT_ZERO( ::nanosleep(&ts, 0) );
            continue;
        } 
        // ok, enough data available. Read and stick in queue
        block   b = ffargs->pool->get();

        XLRCALL( ::XLRReadFifo(sshandle, (READTYPE*)b.iov_base, b.iov_len, 0) );

        // and push it on da queue. push() always succeeds [will block
        // until it *can* push] UNLESS the queue was disabled before or
        // whilst waiting for the queue to become push()-able.
        if( outq->push(b)==false )
            break;

        // indicate we've pushed another 'blocksize' amount of
        // bytes into mem
        counter += b.iov_len;
    }
    // clean up
    delete [] emergency_block;
    DEBUG(0, "fiforeader: stopping" << endl);
}

// read straight from disk
void diskreader(outq_type<block>* outq, sync_type<diskreaderargs>* args) {
    bool               stop = false;
    runtime*           rteptr;
    SSHANDLE           sshandle;
    S_READDESC         readdesc;
    playpointer        cur_pp( 0 );
    diskreaderargs*    disk = args->userdata;

    rteptr = disk->rteptr;
    // make rilly sure the values in the constrained sizes set make sense.
    // an exception is thrown if not all of the constraints imposed are met.
    RTEEXEC(*rteptr, rteptr->sizes.validate());

    // note: the d'tor of "diskreaderargs" takes care of delete[]'ing buffer -
    //       this makes sure that the memory is available until after all
    //       threads have finished, ie it is not deleted before no-one
    //       references it anymore.
    readdesc.XferLength = rteptr->sizes[constraints::blocksize];
    SYNCEXEC(args,
             disk->pool = new blockpool_type(readdesc.XferLength, 16));

    // Wait for "run" or "cancel".
    args->lock();
    while( !disk->run && !args->cancelled )
        args->cond_wait();
    stop                    = args->cancelled;
    cur_pp                  = disk->pp_start;
    args->unlock();

    if( stop ) {
        DEBUG(0, "diskreader: cancelled before start" << endl);
        return;
    }

    RTEEXEC(*rteptr,
            sshandle = rteptr->xlrdev.sshandle();
            rteptr->statistics.init(args->stepid, "Disk");
            rteptr->transfersubmode.clr(wait_flag).set(run_flag));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    DEBUG(0, "diskreader: starting" << endl);
    // now enter our main loop
    while( !stop ) {
        block   b = disk->pool->get();
        // Read a block from disk into position idx
        readdesc.AddrHi     = cur_pp.AddrHi;
        readdesc.AddrLo     = cur_pp.AddrLo;
        readdesc.BufferAddr = (READTYPE*)b.iov_base;

        XLRCALL( ::XLRRead(sshandle, &readdesc) );

        // If we fail to push it onto our output queue that's a hint for
        // us to call it a day
        if( outq->push(b)==false )
            break;

        // weehee. Done a block. Update our local loop variables
        cur_pp += readdesc.XferLength;

        // update & check global state
        args->lock();
        stop                        = args->cancelled;

        // If we didn't receive an explicit stop signal,
        // do check if we need to repeat when we've reached
        // the end of our playable region
        if( !stop && cur_pp>=disk->pp_end && (stop=!disk->repeat)==false )
            cur_pp = disk->pp_start;
        args->unlock();

        // update stats
        counter += readdesc.XferLength;
    }
    DEBUG(0, "diskreader: stopping" << endl);
    return;
}

// UDPs reader: input = UDP socket, output blocks of constraints[blocksize]
//
// UDPs reader v3: peek and *then* read a datagram; putting the packet
//                 immediately at the right position.
//                 This ONLY works for UDPs - UDP + 8 byte sequencenumber
//                 packets
// UDPs reader v4: same peek, compute, write-to-memory structure as v3
//                 but we know do not have a circular buffer of
//                 blocks anymore. we have workbuf of <readahead> blocks,
//                 assuming a linear seqnr -> position mapping over these
//                 <readahead> amount of blocks.
//                 receive a seq nr that is below the current zero-point:
//                 actively discard it (it's too late)
//                 receive a seq nr that falls past the workbuf: start
//                 releasing block(s) until the seq nr *does* fit 
//                 within the workbuf [possibly emptying it and restarting]
void udpsreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    bool           stop;
    uint64_t       seqnr;
    uint64_t       firstseqnr  = 0;
    uint64_t       expectseqnr = 0;
    runtime*       rteptr;
    fdreaderargs*  network = args->userdata;

    rteptr = network->rteptr; 
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
    unsigned char*               dummybuf = new unsigned char[ 65536 ]; // max size of a datagram 2^16 bytes
    const unsigned int           readahead = 4;
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];

    // Cache ANYTHING that is known & constant.
    // If a value MUST be constant, then MAKE IT SO.
    const unsigned int           n_ull_p_dg     = wr_size/sizeof(uint64_t);
    const unsigned int           n_ull_p_rd     = rd_size/sizeof(uint64_t);
    const unsigned int           n_dg_p_block   = blocksize/wr_size;
    const unsigned int           n_dg_p_workbuf = n_dg_p_block * readahead;

    // We need some temporary blocks:
    //   * an array of blocks, our workbuf. we keep writing packets
    //     in there until we receive a sequencenumber that would 
    //     force us to write outside the readahead buffer. then
    //     we start to release blocks until the packet CAN be
    //     written into the workbuf
    block*                       workbuf = new block[ readahead ];

    // Create a blockpool. If we need blocks we take'm from there
    SYNCEXEC(args,
            network->pool = new blockpool_type(blocksize, 32));


    // Set up the message - a lot of these fields have known & constant values
    struct iovec                 iov[2];
    struct msghdr                msg;
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;

    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // message 'msg': two fragments. Sequence number and datapart
    msg.msg_iov        = &iov[0];

    // The iov_len's will be filled in differently between
    // the PEEK phase and the WAITALL phase
    msg.msg_iovlen     = 0;

    // The size of the parts of the message are known
    // and for the sequencenumber, we already know the destination address
    iov[0].iov_base    = &seqnr;
    iov[0].iov_len     = sizeof(seqnr);
    iov[1].iov_len     = rd_size;

    // Here we fix the amount of iov's and lengths of the
    // messages for the two phases: PEEK and WAITALL
    // We should be safe for datagrams up to 2G i hope
    //   (the casts to 'int' from iov[..].iov_len because
    //    the .iov_len is-an unsigned)
    const int               npeek       = 1;
    const int               peekread    = (int)(iov[0].iov_len);
    const int               nwaitall    = 2;
    const int               waitallread = (int)(iov[0].iov_len + iov[1].iov_len);

    //   * one prototype block with fillpattern + zeroes etc
    //     in the right places to initialize a freshly allocated
    //     block with
    unsigned char*          fpblock = new unsigned char[ blocksize ];

    // Fill the fillpattern block with fillpattern
    // HV: 19May2011 - this is not quite correct at ALL!
    //     We must initialize the positions of the datagrams
    //     with fillpattern. If there is excess space,
    //     eg when reading compressed data: then we allocate
    //     space for the decompressed data but we overwrite only 
    //     a portion of that memory by the size of compressed
    //     data.
    //     As a result there would be fillpattern in the excess 
    //     space - the decompression could, potentially, leave
    //     parts of the data intact by only modifying the
    //     affected bits ...
    {
        uint64_t*      dgptr   = (uint64_t*)fpblock;
        const uint64_t fillpat = ((uint64_t)0x11223344 << 32) + 0x11223344;

        for(unsigned int dgcnt=0; dgcnt<n_dg_p_block; dgcnt++, dgptr+=n_ull_p_dg) {
            unsigned int ull = 0;
            // fillpattern up until the size of the datagram we read from the network
            for( ; ull<n_ull_p_rd; ull++ )
                dgptr[ull] = fillpat;
            // the rest (if any) is zeroes
            for( ; ull<n_ull_p_dg; ull++ )
                dgptr[ull] = 0;
        }
    }

    // reset statistics/chain and statistics/evlbi
    RTE3EXEC(*rteptr,
            rteptr->evlbi_stats = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdpsReadv4"),
            delete [] dummybuf; delete [] workbuf; delete [] fpblock);

    // Great. We're done setting up. Now let's see if we weren't cancelled
    // by any chance
    SYNC3EXEC(args, stop = args->cancelled,
            delete [] dummybuf; delete [] workbuf; delete [] fpblock);


    if( stop ) {
        delete [] dummybuf;
        delete [] workbuf;
        delete [] fpblock;
        DEBUG(0, "udpsreader: cancelled before actual start" << endl);
        return;
    }

    // No, we weren't. Now go into our mainloop!
    DEBUG(0, "udpsreader: fd=" << network->fd << " data:" << iov[1].iov_len
            << " total:" << waitallread << " readahead:" << readahead << endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    counter_type&    dsum( rteptr->evlbi_stats.deltasum );
    ucounter_type&   ooosum( rteptr->evlbi_stats.ooosum );
    ucounter_type&   pktcnt( rteptr->evlbi_stats.pkt_total );
    ucounter_type&   ooocnt( rteptr->evlbi_stats.pkt_ooo );
    ucounter_type&   disccnt( rteptr->evlbi_stats.pkt_disc );

    // inner loop variables
    bool         discard;
    void*        location;
    uint64_t     blockidx;
    unsigned int shiftcount;
    counter_type delta;

    // Our loop can be much cleaner if we wait here to receive the 
    // very first sequencenumber. We make that our current
    // first sequencenumber and then we can _finally_ drop
    // into our real readloop
    msg.msg_iovlen = npeek;
    ASSERT_COND( ::recvmsg(network->fd, &msg, MSG_PEEK)==peekread );
    expectseqnr = firstseqnr = seqnr;

    DEBUG(0, "udps_reader: first sequencenr# " << firstseqnr << endl);


    // Drop into our tight inner loop
    do {
        discard   = (seqnr<firstseqnr);
        delta     = (int64_t)(expectseqnr - seqnr);
        location  = (discard?dummybuf:0);

        // Ok, we have read another sequencenumber.
        // First up: some statistics?
        pktcnt++;
        dsum     += delta;
        ooosum   += (ucounter_type)::llabs(delta);
        if( delta )
            ooocnt++;
        if( discard )
            disccnt++;

        // Now we need to find out where to put the data for it!
        // that is, if the packet is not to be discarded
        // [if location is already set it is the dummybuf, ie discardage]
        // NOTE: blockidx can never become <0 since we've already
        //       checked that seqnr >= firstseqnr
        //       (a condition signalled by location==0 since
        //        location!=0 => seqnr < firstseqnr)
        shiftcount = 0;
        while( location==0 ) {
            blockidx = ((seqnr-firstseqnr)/n_dg_p_block);

            if( blockidx<readahead ) {
                // ok we know in which block to put our datagram
                // make sure the block is non-empty && initialized
                if( workbuf[blockidx].empty() ) {
                    workbuf[blockidx] = network->pool->get();
                    ::memcpy(workbuf[blockidx].iov_base, fpblock, blocksize);
                }
                // compute location inside block
                location = (unsigned char*)workbuf[blockidx].iov_base + 
                           ((seqnr - firstseqnr)%n_dg_p_block)*wr_size;
                break;
            } 
            // Crap. sequence number would fall outside workbuf!

            // Release the first block in our workbuf.
            if( !workbuf[0].empty() )
                if( outq->push(workbuf[0])==false )
                    break;

            // Then shift all blocks down by one,
            for(unsigned int i=1; i<readahead; i++)
                workbuf[i-1] = workbuf[i];

            // do not forget to clear the last position
            if( shiftcount==0 )
                workbuf[(readahead-1)] = block();

            // Update loopvariables
            firstseqnr += n_dg_p_block;
            if( ++shiftcount==readahead ) {
                DEBUG(0, "udpsreader: detected jump > readahead, " << seqnr - (firstseqnr+n_dg_p_workbuf) << " datagrams" << endl);
                firstseqnr = seqnr;
            }
        }

        // If location STILL is 0 then there's no point
        // in going on
        if( location==0 )
            break;

        // Our primary computations have been done and, what's most
        // important, a location for the packet has been decided upon
        // Read the pakkit into our mem'ry space before we do anything else
        msg.msg_iovlen  = nwaitall;
        iov[1].iov_base = location;
        if( ::recvmsg(network->fd, &msg, MSG_WAITALL)!=waitallread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - " << lse;
            throw syscallexception(oss.str());
        }

        // Now that we've *really* read the pakkit we may update our
        // read statistics and update our expectation
        counter       += waitallread;
        expectseqnr    = seqnr+1;

        // Wait for another pakkit to come in. 
        // When it does, take a peak at the sequencenr
        msg.msg_iovlen = npeek;
        if( ::recvmsg(network->fd, &msg, MSG_PEEK)!=peekread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            oss << "::recvmsg(network->fd, &msg, MSG_PEEK) fails - " << lse;
            throw syscallexception(oss.str());
        }
    } while( true );

    // Clean up
    delete [] dummybuf;
    delete [] workbuf;
    delete [] fpblock;
    DEBUG(0, "udpsreader: stopping" << endl);
}

// read from a socket. we always allocate chunks of size <read_size> and
// read from the network <write_size> since these sizes are what went *into*
// the network so we just reverse them
// (reading <read_size> optionally compressing into <write_size> before
// writing to the network)
void socketreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
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
            rteptr->statistics.init(args->stepid, "SocketRead"));

    counter_type&        counter( rteptr->statistics.counter(args->stepid) );
    const unsigned int   rd_size = rteptr->sizes[constraints::write_size];
    const unsigned int   wr_size = rteptr->sizes[constraints::read_size];
    const unsigned int   bl_size = rteptr->sizes[constraints::blocksize];

    SYNCEXEC(args,
             stop = args->cancelled;
             if(!stop) network->pool = new blockpool_type(bl_size,16););

    if( stop ) {
        DEBUG(0, "socketreader: stop signalled before we actually started" << endl);
        return;
    }
    DEBUG(0, "socketreader: read fd=" << network->fd << " rd:" << rd_size
             << " wr:" << wr_size <<  " bs:" << bl_size << endl);
    bytesread = 0;
    while( !stop ) {
        block                b = network->pool->get();
        int                  r;
        unsigned char*       ptr  = (unsigned char*)b.iov_base;
        const unsigned char* eptr = (ptr + b.iov_len);

        // set everything to 0
        ::memset(ptr, 0x00, b.iov_len);

        // do read data orf the network. keep on readin' until we have a
        // full block. the constraintsolvert makes sure that an integral
        // number of write-sizes will fit into a block. 
        while( (ptrdiff_t)(eptr-ptr)>=(ptrdiff_t)wr_size ) {
            r = ::recvfrom(network->fd, ptr, rd_size, MSG_WAITALL, 0, 0);
            // this check will go wrong when network->blocksize >2.1GB (INT_MAX
            // for 32bit integer). oh well.
            if( r!=(int)rd_size ) {
                lastsyserror_type lse;
                if( r==0 ) {
                    DEBUG(0, "socketreader: remote side closed connection" << endl);
                } else {
                    DEBUG(0, "socketreader: read failure " << lse << endl);
                }
                stop = true;
                break;
            }
            counter   += rd_size;
            bytesread += rd_size;
            ptr       += wr_size;
        }
        if( stop )
            break;
        if( ptr!=eptr ) {
            DEBUG(-1, "socketreader: skip blok because of constraint error. blocksize not integral multiple of write_size" << endl);
            continue;
        }
        // push it downstream. note: compute the actual start of the block since the
		// original value ("ptr") has potentially been ge-overwritten; it's been
		// used as a temp
        if( outq->push(b)==false )
            break;
    }
    DEBUG(0, "socketreader: stopping. read " << bytesread << " (" <<
             byteprint((double)bytesread,"byte") << ")" << endl);
}

// read from filedescriptor
void fdreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    bool                   stop;
    ssize_t                r;
    runtime*               rteptr;
    fdreaderargs*          file = args->userdata;

    rteptr = file->rteptr;
    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int     blocksize = rteptr->sizes[constraints::blocksize];

    SYNCEXEC(args,
             stop = args->cancelled;
             if(!stop) file->pool = new blockpool_type(blocksize, 16);
             );

    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "FdRead"));

    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    if( stop ) {
        DEBUG(0, "fdreader: stopsignal caught before actual start" << endl);
        return;
    }
    // update submode flags
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag).set(run_flag));

    DEBUG(0, "fdreader: start reading from fd=" << file->fd << endl);
    while( !stop ) {
        block           b = file->pool->get();

        // do read data orf the network
        if( (r=::read(file->fd, b.iov_base, b.iov_len))!=(int)b.iov_len ) {
            if( r==0 ) {
                DEBUG(-1, "fdreader: EOF read" << endl);
            } else if( r==-1 ) {
                DEBUG(-1, "fdreader: READ FAILURE - " << ::strerror(errno) << endl);
            } else {
                DEBUG(-1, "fdreader: unexpected EOF - want " << b.iov_len << " bytes, got " << r << endl);
            }
            break;
        }
        // update statistics counter
        counter += blocksize;

        // push it downstream
        if( outq->push(b)==false )
            break;
    }
    DEBUG(0, "fdreader: done " << byteprint((double)counter, "byte") << endl);
}

void netreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    // deal with generic networkstuff
    bool                   stop;
    fdreaderargs*          network = args->userdata;

    // set up infrastructure for accepting only SIGUSR1
    install_zig_for_this_thread(SIGUSR1);

    // first things first: register our threadid so we can be cancelled
    // if the network (if 'fd' refers to network that is) is to be closed
    // and we don't know about it because we're in a blocking syscall.
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).
    // do the malloc/new outside the critical section. operator new()
    // may throw. if that happens whilst we hold the lock we get
    // a deadlock. we no like.
    pthread_t*     tmptid = new pthread_t(::pthread_self());

    args->lock();
    stop                       = args->cancelled;
    network->threadid          = tmptid;
    args->unlock();

    //RTEEXEC(*network->rteptr,
    //        network->rteptr->statistics.init(args->stepid, "NetRead"));

    if( stop ) {
        DEBUG(0, "netreader: stop signalled before we actually started" << endl);
        return;
    }
    // we may have to accept first
    if( network->doaccept ) {
        fdprops_type::value_type* incoming = 0;
        // Attempt to accept. "do_accept_incoming" throws on wonky!
        RTEEXEC(*network->rteptr,
                network->rteptr->transfersubmode.set( wait_flag ));
        DEBUG(0, "netreader: waiting for incoming connection" << endl);

        if( network->rteptr->netparms.get_protocol()=="unix" )
            incoming = new fdprops_type::value_type(do_accept_incoming_ux(network->fd));
        else
            incoming = new fdprops_type::value_type(do_accept_incoming(network->fd));

        // great! we have accepted an incoming connection!
        // check if someone signalled us to stop (cancelled==true).
        // someone may have "pressed cancel" between the actual accept
        // and us getting time to actually process this.
        // if that wasn't the case: close the lissnin' sokkit
        // and install the newly accepted fd as network->fd.
        args->lock();
        stop = args->cancelled;
        if( !stop ) {
            ::close(network->fd);
            network->fd = incoming->first;
        }
        args->unlock();

        if( stop ) {
            DEBUG(0, "netreader: stopsignal before actual start " << endl);
            return;
        }
        // as we are not stopping yet, inform user whom we've accepted from
        DEBUG(0, "netreader: incoming dataconnection from " << incoming->second << endl);

        delete incoming;
    }
    // update submode flags
    RTEEXEC(*network->rteptr, 
            network->rteptr->transfersubmode.clr( wait_flag ).set( connected_flag ));

    // and delegate to appropriate reader
    if( network->rteptr->netparms.get_protocol()=="udps" )
        udpsreader(outq, args);
    else
        socketreader(outq, args);
}

// The framer. Gobbles in blocks of data and outputs
// compelete tape/diskframes as per Mark5 Memo #230 (Mark4/VLBA) and #... (Mark5B).
// The strictness of headerchecking is taken from the framerargs.
// If framerargs.strict == false then, basically, all that this checks is
// the syncword. (which is also all we can do for Mark5B anyway).
// For Mk4 and VLBA formats, strict==true implies CRC checking of one of the
// tracks [reasonably expensive check, but certainly a good one].
void framer(inq_type<block>* inq, outq_type<frame>* outq, sync_type<framerargs>* args) {
    bool                stop;
    block               b,accublock;
    runtime*            rteptr;
    framerargs*         framer = args->userdata;
    headersearch_type   header         = framer->hdr;
    const unsigned int  syncword_area  = header.syncwordoffset + header.syncwordsize;
    // Searchvariables must be kept out of mainloop as we may need to
    // aggregate multiple blocks from our inputqueue before we find a 
    // complete frame
    uint64_t            nFrame        = 0;
    uint64_t            nBytes        = 0;
    boyer_moore         syncwordsearch(header.syncword, header.syncwordsize);
    unsigned int        bytes_to_next = header.framesize;

    rteptr = framer->rteptr;

    // Basic assertions: we require some elements downstream (qdepth)
    // AND we require that the supplied sizes make sense:
    //     framesize  >= headersize [a header fits in a frame]
    ASSERT2_COND( args->qdepth>0, SCINFO("there is no downstream Queue?") );
    ASSERT_COND( header.framesize  >= header.headersize );

    // allocate a buffer for <nframe> frames
    SYNCEXEC(args,
             framer->pool = new blockpool_type(header.framesize, 8);
             stop           = args->cancelled;);

    RTEEXEC(*rteptr, rteptr->statistics.init(args->stepid, "Framerv2"));
    counter_type&  counter( rteptr->statistics.counter(args->stepid) );

    DEBUG(0, "framer: start looking for " << header << " dataframes" << endl);

    // Before we enter our main loop we initialize:
    accublock     = framer->pool->get();
    bytes_to_next = header.framesize;

    // off we go! 
    while( !stop && inq->pop(b) ) {
        unsigned char*              ptr      = (unsigned char*)b.iov_base;
        unsigned char*              accubase = (unsigned char*)accublock.iov_base;
        unsigned int                ncached  = header.framesize - bytes_to_next;
        unsigned char const * const base_ptr = (unsigned char const * const)b.iov_base;
        unsigned char const * const e_ptr    = ptr + b.iov_len;

        // update accounting - we must do that now since we may jump back to
        // the next iteration of this loop w/o executing part(s) of this
        // loop 
        nBytes += (uint64_t)(ptr - (unsigned char*)b.iov_base);

        // Ah. New bytes came in.
        // first deal with leftover bytes from previous block, if any.
        // We copy at most "syncword_area-1" bytes out of the new block and
        // append them to the amount that's cached: if no syncword was found
        // in that amount of bytes then we start searching the new block
        // instead, discarding all bytes that we kept.
        while( ncached && ptr<e_ptr ) {
            // can we look for syncword yet?
            const bool           search = (ncached<syncword_area);
            const unsigned int   navail = (unsigned int)(e_ptr-ptr);
            const unsigned int   ncpy   = (search)?
                                            min((2*syncword_area)-1-ncached, navail):
                                            min(bytes_to_next, navail);

            ::memcpy(accubase+ncached, ptr, ncpy);
            bytes_to_next   -= ncpy;
            ptr             += ncpy;
            ncached         += ncpy;

            if( bytes_to_next==0 ) {
                // ok, we has a frames!
                frame  f(header.frameformat, header.ntrack, accublock);

                f.frametime   = header.timestamp(accubase, 0);
                // ok! get ready to accept any leftover bytes
                // from the next main loop.
                stop = (outq->push(f)==false);

                accublock     = framer->pool->get();
                accubase      = (unsigned char*)accublock.iov_base;
                bytes_to_next = header.framesize;
                ncached       = 0;

                // update statistics!
                counter += header.framesize;
                // chalk up one more frame.
                nFrame++;
                continue;
            }

            // If we needed to search do it now
            if( search ) {
                const unsigned char*     sw = syncwordsearch(accubase, ncached);

                if( sw==0 ) {
                    // not found. depending on how many bytes we copied
                    // we take the appropriate action
                    if( ncpy>=(syncword_area-1) ) {
                        // no syncword found in concatenation of
                        // previous bytes with a full syncwordarea less 1 of
                        // the new block: *if* there is a syncword area in the
                        // new block it's at least going to be at the start!
                        // Return to the state that indicates an empty
                        // accumulator block and start the syncwordsearch
                        // in the whole block in the main loop below.
                        // We discard all local cached bytes.
                        bytes_to_next = header.framesize;
                        ptr           = (unsigned char*)b.iov_base;
                        ncached       = 0;
                    } else {
                        // It may be that we just didn't have enough
                        // bytes just yet - 'ncpy' could be 0 at this 
                        // point. 
                        // If we have syncword_area or more bytes
                        // w/o syncword we keep only the last
                        // (syncwordarea-1) bytes - this will trigger
                        // a new 'search' on the next entry of this loop
                        const unsigned int  nkeep( min(syncword_area-1, ncached) );

                        ::memmove(accubase, accubase + ncached - nkeep, nkeep);
                        bytes_to_next = header.framesize - nkeep;
                        ncached       = nkeep;
                    }
                    // Ok, the state after a failed syncword search
                    // has been set - now continue with the outer loop;
                    // it should fall out of it anyway.
                    continue;
                }
                // If the syncword is not at least at the offset where we
                // expect it, we has an ishoos!
                const unsigned int swpos = (unsigned int)(sw - accubase);
                if( swpos<header.syncwordoffset ) {
                    // we're d00med. we're missing pre-syncword bytes!
                    // best to discard all locally cached bytes and
                    // restart
                    bytes_to_next = header.framesize;
                    ptr           = (unsigned char*)b.iov_base;
                    ncached       = 0;
                    continue;
                } else if( swpos>header.syncwordoffset ) {
                    // OTOH - if it's a bit too far off, we
                    // move it to where it should be:

                    const unsigned int diff = swpos - header.syncwordoffset;
                    ::memmove(accubase, accubase + diff, ncached-diff);
                    ncached       -= diff;
                    bytes_to_next += diff;
                }
                // Ok - we now either have found the syncword and put
                // the data where it should be or we discarded everything.
                // Let's try again!
                continue;
                // end of syncwordsearch phase
            }
            // If we end up here we didn't have a complete frame
            // nor did we have to search.
            // So: we must still be needing more bytes before we fill
            // up the frame, so we don't actually have to do anything;
            // the loop condition will either copy more bytes or terminate,
            // depending on availability of more bytes

        } // end of dealing with cached bytes

        // If we already exhausted the block we can skip the code below
        if( stop || ptr>=e_ptr )
            continue;

        // Main loop over the remainder of the incoming block: we look for
        // syncwords until we run out of block and keep the remainder for
        // the next incoming block
        while( ptr<e_ptr ) {
            const unsigned int          navail = (unsigned int)(e_ptr-ptr);
            unsigned char const * const sw     = syncwordsearch(ptr, navail);

            if( sw==0 ) {
                // no more syncwords. Keep at most 'syncarea-1' bytes for the future
                const unsigned int nkeep = min(syncword_area-1, navail);

                ::memcpy(accubase, e_ptr - nkeep, nkeep);
                bytes_to_next = header.framesize - nkeep;
                // trigger end-of-loop condtion
                ptr = const_cast<unsigned char*>(e_ptr);
                continue;
            }
            // At least we HAVE a syncword
            const unsigned int swpos = (unsigned int)(sw - ptr);

            // If syncword found too close to start of search (which we
            // assume the start-of-frame to be, generally) we must discard
            // this one: we're missing pre-syncword bytes so we jump over
            // the syncword and start searching from there
            if( swpos<header.syncwordoffset ) {
                ptr = const_cast<unsigned char*>(sw) + header.syncwordsize;
                continue;
            }

            // Now the syncword is *at least* at the offset from 'ptr'  where it
            // should be so we can safely do 'syncwordptr - header.syncwordoffset'
            // Also compute how many bytes we have - it may be a truncated
            // frame.
            //
            // keep a pointer to where the current frame should start in memory
            // (ie pointing at the first 'p' or 'S')
            //    * sof  == start-of-frame
            //
            // Most important assumption: any frame looks like:
            //
            //  [pppp]SSSSShhhhhdddddddd
            //  |<------- frame ------>|
            //  |<-- header -->|
            //
            // Where:
            //  pppp : optional pre-syncword header bytes
            //  SSSS : syncword bytes
            //  hhhh : header bytes
            //  dddd : frame data bytes
            unsigned char const * const sof = sw - header.syncwordoffset;
            const unsigned int          num = (unsigned int)(e_ptr - sof);

            if( num<header.framesize ) {
                // yup, incomplete frame. Means that the current block
                // was exhausted before we could find a complete frame.
                // Copy to the local accumulator buffer so's the next
                // incoming bytes can be appended.
                ::memcpy(accubase, sof, num);
                bytes_to_next = header.framesize - num;

                // trigger loop end condition
                ptr = const_cast<unsigned char*>(e_ptr);
                continue;
            }
            // Sweet! We has a frames!
            block   fblock = b.sub((sof - base_ptr), header.framesize);
            frame   f(header.frameformat, header.ntrack, fblock);
#if 0
            // For now leave out this check. It is expensive.
            // It checks the syncword (again) AND extracts a track
            // and does a CRC check on that
            if( header.check(sof)==false ) {
                // invalid frame! Now where to restart the search?
                // let's start one byte further
                ptr++;
                continue;
            }
#endif

            f.frametime   = header.timestamp(sof, 0);

            // Fail to push downstream means: quit!
            if( (stop=(outq->push(f)==false))==true )
                break;
            // Advance ptr to what hopefully is the
            // next frame
            ptr = const_cast<unsigned char*>(sof) + header.framesize;

            // update statistics!
            counter += header.framesize;
            // chalk up one more frame.
            nFrame++;
        } // done processing block
    }
    // we take it that if nBytes==0ULL => nFrames==0ULL (...)
    // so the fraction would come out to be 0/1.0 = 0 rather than
    // a divide-by-zero exception.
    double bytes    = ((nBytes==0)?(1.0):((double)nBytes));
    double fraction = ((double)(nFrame * header.framesize)/bytes) * 100.0;
    DEBUG(0, "framer: stopping. Found " << nFrame << " frames, " << 
             "fraction=" << fraction << "%" << endl);
    return;
}

// Compress a tapeframe.
void framecompressor(inq_type<frame>* inq, outq_type<block>* outq, sync_type<compressorargs>* args) {
    frame     f;
    runtime*  rteptr = args->userdata->rteptr;

    ASSERT_NZERO( rteptr );
    DEBUG(0, "framecompressor: starting" << endl);

    // make rilly sure that the constrained sizes make sense
    RTEEXEC(*rteptr,
            rteptr->sizes.validate();
            rteptr->statistics.init(args->stepid, "Compress/Frame"));

    // extract the constrained values out of the constraintset_type
    // since we've validated them we can use these values as-are!
    counter_type&      ct  = rteptr->statistics.counter(args->stepid);
    const unsigned int rd  = rteptr->sizes[constraints::read_size];
    const unsigned int wr  = rteptr->sizes[constraints::write_size];
    const unsigned int fs  = rteptr->sizes[constraints::framesize];
    const unsigned int co  = rteptr->sizes[constraints::compress_offset];

    // generate+compile+load the (de)compressionfunctions.
    // we only compress the words after compress_offset in a block of size
    // read_size. the contrainers have figured it all out that all sizes and
    // offsets make sense so we can safely use them as-are.
    //
    // choose blocksize based on wether the constraints were solved for the
    // compressed or uncompressed case. compressed? then it was solved for
    // write_size, otherwise read_size
    compressor_type compressor(rteptr->solution, (rd-co)/sizeof(data_type),
                               false, rteptr->signmagdistance);
    
    // and off we go!
    DEBUG(0, "framecompressor: compiled/loaded OK" << endl);

    while( inq->pop(f) ) {
        if( f.framedata.iov_len!=fs ) {
            if( f.framedata.iov_len<fs ) {
                DEBUG(0, "framecompressor: skip frame of size " << f.framedata.iov_len << " expected " << fs << endl);
                continue;
            } else {
                DEBUG(0, "framecompressor: frame of size " << f.framedata.iov_len << " larger than expected "
                          << fs << ", skip " << (f.framedata.iov_len-fs) << " bytes" << endl);
            }
        }
        data_type*           ecptr; // end-of-compressptr
        unsigned char*       ptr = (unsigned char*)f.framedata.iov_base;
        const unsigned char* bptr = ptr;
        const unsigned char* eptr = ptr + fs;
        
        // compress the frame according to the following scheme
        //
        //   |-----|---------------------------------|
        //     HDR                 DATA                   = FRAME
        //
        //   |-----|----|-----|-----|-----|-----| ...
        //     OFF   CMP  OFF   CMP   OFF   CMP   ...    OFF=compressoffset,
        //                                               CMP=compressed data
        //   |----------|-----------|-----------| ...
        //       B0          B1          B2       ...
        //   
        //      B0, B1, B2 ... are the blocks pushed
        //      down our outputqueue, containing an
        //      uncompressed header and a bit of
        //      compressed data following it
        while( (ptr+rd)<=eptr )  {
            // compress only the data after compress offset
            ecptr = compressor.compress((data_type*)(ptr + co));

            if( (ptrdiff_t)(ecptr-(data_type*)ptr)!=(ptrdiff_t)(wr/sizeof(data_type)) ) {
                DEBUG(-1, "compress yields " << (ptrdiff_t)(ecptr-(data_type*)ptr) << 
                          "expect " << (ptrdiff_t)(wr/sizeof(data_type)) );
            }
            // and it yields a total of write_size of bytes of data
            // do this by pushing a slice of the old block down the queue
            if( outq->push(f.framedata.sub((unsigned int)(ptr-bptr), wr))==false )
                break;
            ptr += rd;

            // And compressed another frame!
            ct += wr;
        }
    }
    DEBUG(0, "framecompressor: stopping." << endl);

    return;
}
void blockcompressor(inq_type<block>* inq, outq_type<block>* outq, sync_type<runtime*>* args) {
    block      b;
    runtime*   rteptr = *args->userdata;

    ASSERT_NZERO( rteptr );
    DEBUG(0, "blockcompressor: starting" << endl);

    // be extra safe
    RTEEXEC(*rteptr,
            rteptr->sizes.validate();
            rteptr->statistics.init(args->stepid, "Compress/Block"));

    // extract the constrained values out of the constraintset_type
    // since we've validated them we can use these values as-are!
    // The sizes are in units of bytes. the compressor + blockpointer deal
    // with units of sizeof(data_type). so we compute how many *words* of
    // output we expect - we test against that to see if the compressor
    // indeed does give us what we SHOULD be getting.
    // Number of words is the write_size (==compressed size) divided by the
    // size of a single compressiondatum (==sizeof(data_type)).
    //const bool         cmp = (rteptr->sizes[constraints::n_mtu]!=constraints::unconstrained);
    counter_type&      ct = rteptr->statistics.counter(args->stepid);
    const unsigned int rd = rteptr->sizes[constraints::read_size];
    const unsigned int wr = rteptr->sizes[constraints::write_size];
    const unsigned int bs = rteptr->sizes[constraints::blocksize];
    const unsigned int co = rteptr->sizes[constraints::compress_offset];
    const unsigned int nw = wr/sizeof(data_type);

    // generate+compile+load the (de)compressionfunctions
    // we compress in chunks of read_size. the constrained sizes have been
    // constrained such that read_size will fit an integral amount of times
    // in blocksize
    // feed the appropriate blocksize to the codegenerator. depending on
    // wether the constraints were solved agains the compressed or
    // uncompressed size
    // (cmp==true => constrained value==wr, cmp==false => constrained
    // value==rd) 
    compressor_type compressor(rteptr->solution, (rd-co)/sizeof(data_type),
                               false, rteptr->signmagdistance);
    
    // and off we go!
    DEBUG(0, "blockcompressor: compiled/loaded OK" << endl);
    DEBUG(0, "blockcompressor: " << rteptr->sizes << endl);

    while( inq->pop(b) ) {
        // if the block is smaller then we think it should be dat's no good!
        if( b.iov_len<bs ) {
            DEBUG(0, "blockcompressor: skipping block of size " << b.iov_len << ", expected " << bs << endl);
            continue;
        } else if( b.iov_len>bs ) {
            DEBUG(0, "blockcompressor: block " << b.iov_len << " larger then expected " << bs
                     << " - skipping " << (b.iov_len-bs) << " bytes" << endl);
        }
        // now that we know it has at least the amount of bytes we are
        // configured with ... [note: if the block is LARGER we do skip some
        // data]
        data_type*           ecptr;
        unsigned char*       ptr  = (unsigned char*)b.iov_base;
        const unsigned char* bptr = ptr;
        const unsigned char* eptr = ptr + bs;

        // compress rd bytes into wr bytes and send those wr bytes off
        // downstream
        while( (ptr+rd)<=eptr )  {
            // ec = end-of-compress, so ecptr == end-of-compress-pointer
            ecptr = compressor.compress((data_type*)(ptr+co));
            if( (ptrdiff_t)(ecptr - (data_type*)ptr)!=(ptrdiff_t)nw) {
                DEBUG(-1, "blockcompressor: compress yields " << (ecptr-(data_type*)ptr) << " expect " << nw+co);
            }
            if( outq->push(b.sub((unsigned int)(ptr-bptr), wr))==false )
                break;
            ptr += rd;
            // And compressed another block
            ct  += wr;
        }
        // next!
    }
    DEBUG(0, "blockcompressor: stopping." << endl);
    return;
}

void blockdecompressor(inq_type<block>* inq, outq_type<block>* outq, sync_type<runtime*>* args) {
    block          b;
    runtime*       rteptr = *args->userdata;
    const uint64_t fillpat = ((uint64_t)0x11223344 << 32) + 0x11223344;

    ASSERT_NZERO( rteptr );
    DEBUG(0, "blockdecompressor: starting" << endl);

    // be extra safe
    RTEEXEC(*rteptr,
            rteptr->sizes.validate();
            rteptr->statistics.init(args->stepid, "Decompress/Block"));

    // extract the constrained values out of the constraintset_type
    // since we've validated them we can use these values as-are!
    // since we are the decompressor we swap the read/write values
    // since they are computed in the "compression" direction like:
    //  <readsize data> => COMPRESS() => <writesize data>
    counter_type&      ct = rteptr->statistics.counter(args->stepid);
    const unsigned int wr = rteptr->sizes[constraints::read_size];
    const unsigned int rd = rteptr->sizes[constraints::write_size];
    const unsigned int bs = rteptr->sizes[constraints::blocksize];
    const unsigned int co = rteptr->sizes[constraints::compress_offset];
    const unsigned int nr = (rteptr->sizes[constraints::write_size]/sizeof(data_type));

    // generate+compile+load the (de)compressionfunctions
    // we compress in chunks of read_size. the constrained sizes have been
    // constrained such that read_size will fit an integral amount of times
    // in blocksize
    // feed the codegenerator the correct blocksize - depending on wether
    // the constraints were solved for the compressed or uncompressed size.
    // cmp == true => constraints were solved against compressed size == the
    // write_size, ie OUR readsize [since we are working in the other
    // direction; reading compressed data and writing uncompressed data].
    compressor_type compressor(rteptr->solution, (wr-co)/sizeof(data_type),
                               false, rteptr->signmagdistance);
    
    // and off we go!
    DEBUG(0, "blockdecompressor: compiled/loaded OK" << endl);
    DEBUG(0, "    constraints " << rteptr->sizes << endl);

    while( inq->pop(b) ) {
        // if the block is smaller then we think it should be dat's no good!
        if( b.iov_len<bs ) {
            DEBUG(0, "blockdecompressor: skipping block of size " << b.iov_len << ", expected " << bs << endl);
            continue;
        } else if( b.iov_len>bs ) {
            DEBUG(0, "blockdecompressor: block " << b.iov_len << " larger then expected " << bs
                     << " - skipping " << (b.iov_len-bs) << " bytes" << endl);
        }
        // now that we know it has at least the amount of bytes we are
        // configured with ... [note: if the block is LARGER we do skip some
        // data]
		data_type*           edcptr;
        unsigned char*       ptr  = (unsigned char*)b.iov_base;
        const unsigned char* eptr = ptr + bs;

        // do decompress the block in chunks of the same size they were read
		// we don't actually check that - the sizes.validat() should make 
		// sure everything makes sense and then the decompression will too
        //
        // We assume that the previous step allocates chunks of memory of
        // size 'wr' bytes but only fill in 'rd' size of bytes [in order 
        // to allow for inplace decompression].
        //
        // In memory the structure looks like this:
        //
        // |---------------------- bs ---------------------|
        //
        //     one block
        //
        // |--- wr ----|--- wr ----|--- wr ----|--- wr ----|
        //
        //     is made up out of <n> chunks of size 'wr'
        //
        // |++++++r+   |+++++++    |+++++++    |+++++++    |
        // |- rd -|    |- rd -|    |- rd -|    |- rd -|
        //
        //     out of which only 'rd' bytes actually contain
        //     data. the system guarantees (the constrainers)
        //     that 'rd' <= 'wr'. if 'wr' > 'rd' this means
        //     there was compression going on and that the
        //     decompressed data will actually fit exactly
        //     into 'wr' whereas the compressed size is
        //     actually 'rd'.
        while( (ptrdiff_t)(eptr-ptr)>=(ptrdiff_t)wr )  {
            if( *((uint64_t*)ptr)==fillpat ) {
                // This is a chunk of fillpattern. Let's not decompress it
                // but fill it up with fillpattern. Which is to say:
                // We start at the end of the chunk [which is where we would
                // start to write decompressed data] and work backwards to
                // the end of the area that *was* written by the previous
                // step in the chain.
                uint64_t*   eo_chunk_ptr = (uint64_t*)(ptr + wr - sizeof(fillpat));
                uint64_t*   bo_chunk_ptr = (uint64_t*)(ptr + rd);

                while( eo_chunk_ptr>bo_chunk_ptr ) {
                    *eo_chunk_ptr  = fillpat;
                    eo_chunk_ptr  -= sizeof(fillpat);
                }
            } else {
                // Looks like ordinary data - decompress it!
                edcptr = compressor.decompress((data_type*)(ptr+co));
	    		ASSERT2_COND( (ptrdiff_t)(edcptr-(data_type*)ptr)==(ptrdiff_t)nr,
		    				  SCINFO("decompress yield " << (edcptr-(data_type*)ptr) << " expect " << (nr+co)));
            }
            ptr += wr;
            ct  += wr;
        }
        // decompression is done in-place so we can push the original block
        // downstream :)
        if( outq->push(b)==false )
            break;
        // next!
    }
    DEBUG(0, "blockdecompressor: stopping." << endl);
    return;
}

void frame2block(inq_type<frame>* inq, outq_type<block>* outq) {
    frame    f;
    DEBUG(0, "frame2block: starting" << endl);
    while( inq->pop(f) ) {
        if( outq->push(f.framedata)==false )
            break;
    }
    DEBUG(0, "frame2block: stopping" << endl);
}



// Buffer a number of bytes
void bufferer(inq_type<block>* inq, outq_type<block>* outq, sync_type<buffererargs>* args) {
    typedef std::queue<block, std::list<block> >  blockqueue_type;

    block              b;
    uint64_t           bytesbuffered;
    buffererargs*      buffargs = args->userdata;
    runtime*           rteptr = (buffargs?buffargs->rte:0);
    blockqueue_type    blockqueue;

    // Make sure we *have* a runtime environment
    ASSERT_NZERO( buffargs && rteptr );

    // Now we can safely set up our buffers.
    // Total # of blocks to alloc = how many requested + qdepth downstream
    //   We round off the number of requested bytes to an integral amount of
    //   blocks.

    // Add a statisticscounter
    RTEEXEC(*rteptr,
             rteptr->statistics.init(args->stepid, "Bufferer"));
    counter_type&   counter = rteptr->statistics.counter(args->stepid);

    DEBUG(0, "bufferer: starting, buffering " << byteprint(buffargs->bytestobuffer, "B") << endl);

    bytesbuffered = 0;
    while( inq->pop(b) ) {
        // Before adding data to our bufferqueue, check if we must release
        // some of them
        while( bytesbuffered>buffargs->bytestobuffer ) {
            block topush = blockqueue.front();

            blockqueue.pop();
            bytesbuffered -= topush.iov_len;
            outq->push( topush );
        }
        blockqueue.push( b );
        bytesbuffered += b.iov_len;
        counter       += b.iov_len;
    }
    DEBUG(0, "bufferer: stopping." << endl);
}

// Write incoming blocks of data in chunks of 'constraints::write_size'
// to the network, prepending 64 bits of strict
// monotonically increasing sequencenumber in front of it
void udpswriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
    int                    oldipd = -300;
    bool                   stop = false;
    block                  b;
    ssize_t                ntosend;
    runtime*               rteptr;
    uint64_t               seqnr;
    uint64_t               nbyte = 0;
    struct iovec           iovect[2];
    fdreaderargs*          network = args->userdata;
    struct msghdr          msg;
    pcint::timeval_type    sop;// s(tart) o(f) p(acket)
    const netparms_type&   np( network->rteptr->netparms );

    rteptr = network->rteptr;

    // assert that the sizes in there make sense
    RTEEXEC(*rteptr, rteptr->sizes.validate()); 
    const unsigned int     wr_size = rteptr->sizes[constraints::write_size];
    args->lock();
    // the d'tor of "fdreaderargs" will delete the storage for us!
    stop              = args->cancelled;
    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    args->unlock();

    if( stop ) {
        DEBUG(-1, "udpswriter: cancelled before actual start" << endl);
        return;
    }
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "NetWrite/UDPs"));

    counter_type& counter( rteptr->statistics.counter(args->stepid) );

    // Initialize the sequence number with a random 32bit value
    // - just to make sure that the receiver does not make any
    // implicit assumptions on the sequencenr other than that it
    // is strictly monotonically increasing.
    seqnr = (uint64_t)::random();

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
    iovect[1].iov_len  = wr_size;

    // Can precompute how many bytes should be sent in a sendmsg call
    ntosend = iovect[0].iov_len + iovect[1].iov_len;

    DEBUG(0, "udpswriter: first sequencenr=" << seqnr
             << " fd=" << network->fd
             << " n2write=" << ntosend << endl);
    // send out any incoming blocks out over the network
    // initialize "start-of-packet" to "now()". the sendloop
    // waits with sending as long as "now()" is not later than
    // "start-of-packet". by forcing that condition to true here the
    // first packet will be sent immediately.
    // by doing it like this - having a time-of-send outside the
    // major loop and pre-computing when to send the next packet
    // (if any) - we get, hopefully, an even nicer behaved sending
    // function: it is more of an absolute timing now than a 
    // relative one ("wait ipd microseconds after you sent the
    // previous one"), which was the previous implementation.
    sop = pcint::timeval_type::now();
    while( !stop && inq->pop(b) ) {
        const int            ipd( (np.interpacketdelay<0)?(np.theoretical_ipd):(np.interpacketdelay) );
        unsigned char*       ptr = (unsigned char*)b.iov_base;
        pcint::timeval_type  now;
        const unsigned char* eptr = (ptr + b.iov_len);

        if( ipd!=oldipd ) {
            DEBUG(0, "udpswriter: switch to ipd=" << ipd << " [set=" << np.interpacketdelay << ", " <<
                     "theoretical=" << np.theoretical_ipd << "]" << endl);
            oldipd = ipd;
        }
        while( (ptr+wr_size)<=eptr ) {
            // iovect[].iov_base is of the "void*" persuasion.
            // we would've liked to use thattaone directly but
            // because of its void-pointerishness we cannot
            // (can't do pointer arith on "void*").
            iovect[1].iov_base = ptr;

            // at this point, wait until the current time
            // is at least "start-of-packet" [and ipd >0 that is].
            // in fact we delay sending of the packet until it is time to send it.
            while( ipd>0 && (now=pcint::timeval_type::now())<sop ) {};

            if( ::sendmsg(network->fd, &msg, MSG_EOR)!=ntosend ) {
                DEBUG(-1, "udpswriter: failed to send " << ntosend << " bytes - " <<
                          ::strerror(errno) << " (" << errno << ")" << endl);
                stop = true;
                break;
            }

            // update loopvariables.
            // only update send-time of next packet if ipd>0
            if( ipd>0 )
                sop = (now + ((double)ipd/1.0e6));
            ptr     += wr_size;
            nbyte   += wr_size;
            counter += ntosend;
            seqnr++;
        }
    }
    DEBUG(0, "udpswriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte, "byte") << ")"
             << endl);
}

// Write each incoming block *as a whole* to the destination,
// prepending each block with a 64bit strict monotonically
// incrementing sequencenumber
void vtpwriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
    int                    oldipd = -300;
    bool                   stop = false;
    block                  b;
    ssize_t                ntosend;
    runtime*               rteptr;
    uint64_t               seqnr;
    uint64_t               nbyte = 0;
    struct iovec           iovect[2];
    fdreaderargs*          network = args->userdata;
    struct msghdr          msg;
    pcint::timeval_type    sop;// s(tart) o(f) p(acket)
    const netparms_type&   np( network->rteptr->netparms );

    rteptr = network->rteptr;

    // assert that the sizes in there make sense
    RTEEXEC(*rteptr, rteptr->sizes.validate()); 

    args->lock();
    // the d'tor of "fdreaderargs" will delete the storage for us!
    stop              = args->cancelled;
    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    args->unlock();

    if( stop ) {
        DEBUG(-1, "vtpwriter: cancelled before actual start" << endl);
        return;
    }
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "NetWrite/VTP"));

    counter_type& counter( rteptr->statistics.counter(args->stepid) );

    // Initialize the sequence number with a random 32bit value
    // - just to make sure that the receiver does not make any
    // implicit assumptions on the sequencenr other than that it
    // is strictly monotonically increasing.
    seqnr = (uint64_t)::random();

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
    iovect[1].iov_len  = 0;


    DEBUG(0, "vtpwriter: first sequencenr=" << seqnr
             << " fd=" << network->fd << endl);
    // send out any incoming blocks out over the network
    // initialize "start-of-packet" to "now()". the sendloop
    // waits with sending as long as "now()" is not later than
    // "start-of-packet". by forcing that condition to true here the
    // first packet will be sent immediately.
    // by doing it like this - having a time-of-send outside the
    // major loop and pre-computing when to send the next packet
    // (if any) - we get, hopefully, an even nicer behaved sending
    // function: it is more of an absolute timing now than a 
    // relative one ("wait ipd microseconds after you sent the
    // previous one"), which was the previous implementation.
    sop = pcint::timeval_type::now();
    while( !stop && inq->pop(b) ) {
        const int            ipd( (np.interpacketdelay<0)?(np.theoretical_ipd):(np.interpacketdelay) );
        pcint::timeval_type  now;

        if( ipd!=oldipd ) {
            DEBUG(0, "vtpwriter: switch to ipd=" << ipd << " [set=" << np.interpacketdelay << ", " <<
                     "theoretical=" << np.theoretical_ipd << "]" << endl);
            oldipd = ipd;
        }
        // iovect[].iov_base is of the "void*" persuasion.
        // we would've liked to use thattaone directly but
        // because of its void-pointerishness we cannot
        // (can't do pointer arith on "void*").
        iovect[1].iov_base = b.iov_base;
        iovect[1].iov_len  = b.iov_len;
        ntosend            = iovect[0].iov_len + iovect[1].iov_len;

        // at this point, wait until the current time
        // is at least "start-of-packet" [and ipd >0 that is].
        // in fact we delay sending of the packet until it is time to send it.
        while( ipd>0 && (now=pcint::timeval_type::now())<sop ) {};

        if( ::sendmsg(network->fd, &msg, MSG_EOR)!=ntosend ) {
            DEBUG(-1, "vtpwriter: failed to send " << ntosend << " bytes - " <<
                    ::strerror(errno) << " (" << errno << ")" << endl);
            stop = true;
            break;
        }

        // update loopvariables.
        // only update send-time of next packet if ipd>0
        if( ipd>0 )
            sop = (now + ((double)ipd/1.0e6));
        nbyte   += ntosend;
        counter += ntosend;
        seqnr++;
    }
    DEBUG(0, "vtpwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte, "byte") << ")"
             << endl);
}

// Generic filedescriptor writer.
// whatever gets popped from the inq gets written to the filedescriptor.
// leave intelligence up to other steps.
void fdwriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
    bool           stop = false;
    block          b;
    runtime*       rteptr;
    uint64_t       nbyte = 0;
    fdreaderargs*  network = args->userdata;

    rteptr = network->rteptr;
    ASSERT_COND(rteptr!=0);
    // first things first: register our threadid so we can be cancelled
    // if the network is to be closed and we don't know about it
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).

    install_zig_for_this_thread(SIGUSR1);

    SYNCEXEC(args,
             stop              = args->cancelled;
             network->threadid = new pthread_t(::pthread_self()));

    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "FdWrite"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    if( stop ) {
        DEBUG(0, "fdwriter: got stopsignal before actually starting" << endl);
        return;
    }

    DEBUG(0, "fdwriter: writing to fd=" << network->fd << endl);

    // blind copy of incoming data to outgoing filedescriptor
    while( inq->pop(b) ) {
        if( ::write(network->fd, b.iov_base, b.iov_len)!=(int)b.iov_len ) {
            lastsyserror_type lse;
            DEBUG(0, "fdwriter: fail to write " << b.iov_len << " bytes "
                     << lse << endl);
            break;
        }
        nbyte   += b.iov_len;
        counter += b.iov_len;
    }
    DEBUG(0, "fdwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte,"byte") << ")"
             << endl);
}

// Filedescriptor writer for SFXC.
// wait for a rendezvous message from SFXC
// whatever gets popped from the inq gets written to the filedescriptor.
// leave intelligence up to other steps.
void sfxcwriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
    bool           stop = false;
    block          b;
    runtime*       rteptr;
    uint64_t       nbyte = 0;
    fdreaderargs*  network = args->userdata;
    char msg[20];
    struct sockaddr_un sun;
    socklen_t len;
    int s;

    rteptr = network->rteptr;
    ASSERT_COND(rteptr!=0);
    // first things first: register our threadid so we can be cancelled
    // if the network is to be closed and we don't know about it
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).

    install_zig_for_this_thread(SIGUSR1);

    SYNCEXEC(args,
             stop              = args->cancelled;
             network->threadid = new pthread_t(::pthread_self()));

    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "SFXCWrite"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    if( stop ) {
        DEBUG(0, "sfxcwriter: got stopsignal before actually starting" << endl);
        return;
    }

    s = network->fd;

    len = sizeof(sun);
    ASSERT_COND( (network->fd=::accept(s, (struct sockaddr *)&sun, &len))!=-1 );

    ASSERT_COND( ::close(s)!= -1 );

    ASSERT_COND( ::read(network->fd, &msg, sizeof(msg))==sizeof(msg) );

    DEBUG(0, "sfxcwriter: writing to fd=" << network->fd << endl);

    // blind copy of incoming data to outgoing filedescriptor
    while( inq->pop(b) ) {
        if( ::write(network->fd, b.iov_base, b.iov_len)!=(int)b.iov_len ) {
            lastsyserror_type lse;
            DEBUG(0, "sfxcwriter: fail to write " << b.iov_len << " bytes "
		  << lse << endl);
            break;
        }
        nbyte   += b.iov_len;
        counter += b.iov_len;
    }
    DEBUG(0, "sfxcwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte,"byte") << ")"
             << endl);
}

// Break up incoming blocks into chunks of 'constraints::write_size'
// and write them to the network
void udpwriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
    int                    oldipd = -300;
    bool                   stop = false;
    block                  b;
    runtime*               rteptr;
    uint64_t               nbyte = 0;
    fdreaderargs*          network = args->userdata;
    const unsigned int     pktsize = network->rteptr->sizes[constraints::write_size];
    pcint::timeval_type    sop;// s(tart) o(f) p(acket)
    const netparms_type&   np( network->rteptr->netparms );

    rteptr = network->rteptr;
    ASSERT2_COND(pktsize>0, SCINFO("internal error with constraints"));

    args->lock();
    stop              = args->cancelled;
    args->unlock();

    if( stop ) {
        DEBUG(0, "udpwriter: got stopsignal before actually starting" << endl);
        return;
    }
    // since we ended up here we must be connected!
    // we do not clear the wait flag since we're not the one guarding that
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag);
            rteptr->statistics.init(args->stepid, "NetWrite/UDP"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    DEBUG(0, "udpwriter: writing to fd=" << network->fd << " wr:" << pktsize << endl);
    // any block we pop we put out in chunks of pktsize, honouring the ipd
    sop = pcint::timeval_type::now();
    while( !stop && inq->pop(b) ) {
        const int            ipd( (np.interpacketdelay<0)?(np.theoretical_ipd):(np.interpacketdelay) );
        unsigned char*       ptr = (unsigned char*)b.iov_base;
        pcint::timeval_type  now;
        const unsigned char* eptr = (const unsigned char*)(ptr + b.iov_len);

        if( ipd!=oldipd ) {
            DEBUG(0, "udpwriter: switch to ipd=" << ipd << " [set=" << np.interpacketdelay << ", " <<
                     "theoretical=" << np.theoretical_ipd << "]" << endl);
            oldipd = ipd;
        }
        while( (ptr+pktsize)<=eptr ) {
            // at this point, wait until the current time
            // is at least "start-of-packet" [and ipd >0 that is].
            // in fact we delay sending of the packet until it is time to send it.
            while( ipd>0 && (now=pcint::timeval_type::now())<sop ) {};

            if( ::write(network->fd, ptr, pktsize)!=(int)pktsize ) {
                lastsyserror_type lse;
                DEBUG(0, "udpwriter: fail to write " << pktsize << " bytes " << lse << endl);
                stop = true;
                break;
            }
            if( ipd>0 )
                sop = (now + ((double)ipd/1.0e6));
            nbyte   += pktsize;
            ptr     += pktsize;
            counter += pktsize;
        }
        if( !stop && ptr!=eptr )
            DEBUG(-1, "udpwriter: internal constraint problem - block is not multiple of pkt\n"
                    <<"           block:" << b.iov_len << " pkt:" << pktsize << endl);
    }
    DEBUG(0, "udpwriter: stopping. wrote "
             << nbyte << " (" << byteprint((double)nbyte,"byte") << ")"
             << endl);
}

// Highlevel networkwriter interface. Does the accepting if necessary
// and delegates to either the generic filedescriptorwriter or the udp-smart
// writer, depending on the actual protocol
void netwriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
    bool                   stop;
    fdreaderargs*          network = args->userdata;

    // first things first: register our threadid so we can be cancelled
    // if the network (if 'fd' refers to network that is) is to be closed
    // and we don't know about it because we're in a blocking syscall.
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).
    SYNCEXEC(args, 
              stop              = args->cancelled;
              network->threadid = new pthread_t(::pthread_self()));

    // set up infrastructure for accepting only SIGUSR1
    install_zig_for_this_thread(SIGUSR1);

    if( stop ) {
        DEBUG(0, "netwriter: stop signalled before we actually started" << endl);
        return;
    }
    // we may have to accept first [eg "rtcp"]
    if( network->doaccept ) {
        // Attempt to accept. "do_accept_incoming" throws on wonky!
        RTEEXEC(*network->rteptr, network->rteptr->transfersubmode.set(wait_flag));
        DEBUG(0, "netwriter: waiting for incoming connection" << endl);
        fdprops_type::value_type incoming( do_accept_incoming(network->fd) );

        // great! we have accepted an incoming connection!
        // check if someone signalled us to stop (cancelled==true).
        // someone may have "pressed cancel" between the actual accept
        // and us getting time to actually process this.
        // if that wasn't the case: close the lissnin' sokkit
        // and install the newly accepted fd as network->fd.
        args->lock();
        stop = args->cancelled;
        if( !stop ) {
            ::close(network->fd);
            network->fd = incoming.first;
        }
        args->unlock();

        if( stop ) {
            DEBUG(0, "netwriter: stopsignal before actual start " << endl);
            return;
        }
        // as we are not stopping yet, inform user whom we've accepted from
        DEBUG(0, "netwriter: incoming dataconnection from " << incoming.second << endl);
    }
    // update submode flags. we can safely say that we're connected
    // but if we're running? leave that up to someone else to decide
    RTEEXEC(*network->rteptr,
            network->rteptr->transfersubmode.clr(wait_flag).set(connected_flag));

    // now drop into either the generic fdwriter or the udpswriter
    if( network->rteptr->netparms.get_protocol()=="udps" )
        ::udpswriter(inq, args);
    else if( network->rteptr->netparms.get_protocol()=="udp+vdif" )
        ::vtpwriter(inq, args);
    else if( network->rteptr->netparms.get_protocol()=="udp" )
        ::udpwriter(inq, args);
    else
        ::fdwriter(inq, args);
}


// Write to the streamstor FIFO
void fifowriter(inq_type<block>* inq, sync_type<runtime*>* args) {
    // hi-water mark. If FIFOlen>=this value, do NOT
    // write data to the device anymore.
    // The device seems to hang up around 62% so for
    // now we stick with 60% as hiwatermark
    // [note: FIFO is 512MByte deep]
    const DWORDLONG      hiwater = (DWORDLONG)(0.6*(512.0*1024.0*1024.0)); 
    // variables
    block                blk;
    runtime*             rteptr = (*args->userdata);
    uint64_t             nskipped = 0;
    SSHANDLE             sshandle;
    // variables for restricting the output-rate of errormessages +
    // count how many data was NOT written to the device
    struct timeb*        tptr = 0;

    // indicate we're writing to the 'FIFO'
    // and take over stuff from the runtime.
    // Since we will immediately fall into a "pop()" we do
    // not have to check the "args->cancelled" condition
    // since that one only is "true" if the input-queue
    // is also disabled, hence we will pick up the cancel-condition
    // immediately.
    args->lock();
    sshandle            = rteptr->xlrdev.sshandle();
    args->unlock();

    RTEEXEC(*rteptr, rteptr->statistics.init(args->stepid, "FifoWriter"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    // enter thread mainloop
    DEBUG(0,"fifowriter: starting" << endl);
    while( inq->pop(blk) ) {
        // if 'tptr' not null and 'delta-t' >= 2.0 seconds, print a report
        // on how many bytes have been skipped since the last report.
        // if we haven't skipped any data since the last report, we assume
        // the blockage has lifted and produce no output.
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
                    DEBUG(-1, "fifowriter: " << tb << " FIFO too full! " <<
                              nskipped << " bytes lost" << endl);
                } else {
                    // no more bytes skipped. clean up
                    delete tptr;
                    tptr = 0;
                }
                nskipped = 0;
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

        // Ok, we've written another block
        counter += blk.iov_len;
    }
    DEBUG(0,"fifowriter: finished" << endl);
}


// A checkfunction, which analyzes the incoming data
// If 'framesize' is set in the runtime->sizes constraints we expect blocks
// the size of frames to come in, or at least a multiple thereof.
// Otherwise we expect blocks of size constraints['blocksize']
void blockchecker(inq_type<block>* inq, sync_type<fillpatargs>* args) {
    ASSERT2_COND(args->userdata->rteptr, SCINFO("No runtime pointer!"));

    block        b;
    runtime*     rteptr = args->userdata->rteptr;
    fillpatargs* fpargs = args->userdata;

    RTEEXEC(*rteptr, rteptr->statistics.init(args->stepid, "Check/Block"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    // Anonymous blocks are made up of an integral amount of read_size
    // chunks. The chunk may have been sent compressed and expanded to
    // be 'read_size' again. However, if the (compressed) chunk was lost the
    // WHOLE chunk of size 'read_size' should consist of fillpattern.
    // IF a 
    ostringstream      oss;
    const uint64_t     fp = ((uint64_t)0x11223344 << 32) + 0x11223344;
    const uint64_t     m  = ((rteptr->solution)?(rteptr->solution.mask()):(~(uint64_t)0));
    const unsigned int bs = rteptr->sizes[constraints::blocksize];
    const unsigned int rd = rteptr->sizes[constraints::read_size];
    const unsigned int n_ull_p_rd = rd/sizeof(uint64_t);

    ASSERT2_COND( (rd%sizeof(uint64_t))==0, SCINFO("read_size must be multiple of 8"));

    oss << "blockchecker: bs=" << bs << ", rd=" << rd;
    if( m!=(~(uint64_t)0) )
        oss << ", m=" << hex << m;
    DEBUG(0, oss.str() << endl);

    while( inq->pop(b) ) {
        if( b.iov_len!=bs ) {
            DEBUG(-1, "blockchecker: got wrong blocksize. expect "
                      << bs << ", got " << b.iov_len << endl);
            break;
        }
        if( (b.iov_len % rd)!=0 ) {
            DEBUG(-1, "blockchecker: blocksize " << b.iov_len
                      << " is not a multiple of read_size (" << rd << ")" << endl);
            break;
        }
        // Block size checked out ok, 
        // Now verify contents
        uint64_t*       start  = ((uint64_t*)b.iov_base);
        uint64_t* const end    = (uint64_t*)((unsigned char*)b.iov_base + b.iov_len);
        
        while( start<end ) {
            uint64_t     expect = ((fpargs->fill!=fp && *start==fp)?(fp):(fpargs->fill & m));
            unsigned int i;

            for(i=0; i<n_ull_p_rd; i++, start++)
                if( *start!=expect )
                    break;
            if( i!=n_ull_p_rd ) {
                DEBUG(-1, "blockchecker: #FAIL at byte "
                          << i*sizeof(uint64_t) << ", expect "
                          << expect << " got " << *start << endl);
                break;
            }
            counter += i*sizeof(uint64_t);
        }
        // processed another block - increment the fillpattern (if there is
        // one, that is)
        fpargs->fill += fpargs->inc;
    }
    DEBUG(0, "blockchecker: done" << endl);
}

// Multiple frames may arrive in one block or it may take multiple blocks to
// fill up a single frame.
// However, data is sent in chunks of readsize. After (optional
// decompression) each chunk of size 'read_size' should either contain
// exclusively fillpattern, syncword or the expected bitpattern
void framechecker(inq_type<block>* inq, sync_type<fillpatargs>* args) {
    ASSERT2_COND(args->userdata->rteptr, SCINFO("No runtime pointer!"));

    block          b;
    runtime*       rteptr = args->userdata->rteptr;
    fillpatargs*   fpargs = args->userdata;

    RTEEXEC(*rteptr, rteptr->statistics.init(args->stepid, "Check/Frame"));
    counter_type&  counter = rteptr->statistics.counter(args->stepid);

    // Note: take the framesize from the dataformat.
    // The reason is that under certain conditions there *is* a dataformat
    // but the transfers are not constrained to that size - notably in TCP
    // or potential other *lossless* transfermechanisms.
    // If there is a reliable transport between sender and receiver we can
    // blindly transfer full blocks since we know that nothing will get
    // lost/out of sync anyway.
    //
    // Note: use the headersearcher in less-strict mode; we only react to
    // the syncword, not the CRC check. The fillpatterngenerator only puts
    // in the syncword at the location for the dataformat.
    const headersearch_type hdr(rteptr->trackformat(), rteptr->ntrack(), (unsigned int)rteptr->trackbitrate());
    const uint64_t          fp = ((uint64_t)0x11223344 << 32) + 0x11223344;
    const uint64_t          m  = ((rteptr->solution)?(rteptr->solution.mask()):(~(uint64_t)0));
    const unsigned int      fs = hdr.framesize;
    const unsigned int      co = rteptr->sizes[constraints::compress_offset];
    const unsigned int      rd = rteptr->sizes[constraints::read_size];
    const unsigned int      n_ull_p_rd = rd/sizeof(uint64_t);
    const unsigned int      n_ull_p_fr = fs/sizeof(uint64_t);

    ASSERT2_COND( (bool)hdr, SCINFO("framecheck requested but no frameformat known?"));
    ASSERT2_COND( (rd % sizeof(uint64_t))==0, SCINFO("read_size is not a multiple of 8"));
    ASSERT2_COND( (fs % sizeof(uint64_t))==0, SCINFO("framesize is not a multiple of 8"));

    DEBUG(0, "framechecker: starting to check [fs=" << fs << ", rd=" << rd << "]" << endl <<
             "              " << hdr << endl);
    bool                    ok;
    uint64_t                nbyte;
    uint64_t                framebyte;
    uint64_t                fpbyte;
    uint64_t                framecnt;
    uint64_t                blockcnt;
    unsigned char* const    framebuf  = new unsigned char[fs];
    unsigned char* const    fppkt     = new unsigned char[rd];
    // don't care what checkptr is, as long as endcheck==check, this triggers re-initialization
    // in the checkloop
    uint64_t*               countptr = 0;
    unsigned const char*    checkptr = 0;

    // fill the fillpatternpacket with fillpattern
    for( unsigned int i=0; i<n_ull_p_rd; i++)
        ((uint64_t*)fppkt)[i] = fp;

    // countin' stuff
    framecnt  = 0;
    blockcnt  = 0;
    nbyte     = 0;
    framebyte = 0;
    fpbyte    = 0;
    ok        = true;
    while( ok && inq->pop(b) ) {
        unsigned char* const   bptr = (unsigned char*)b.iov_base;

        // Check all the bytes of the block
        for( unsigned int i=0; ok && i<b.iov_len; i++ ) {

            // At every frameboundary we must generate a new frame
            if( (nbyte%fs)==0 ) {
                uint64_t*   ull = (uint64_t*)framebuf;
                // yes. first, fill the frame with the current 64bit
                // fillpattern pattern. Take care of compressoffset
                DEBUG(4,format("%8llu ", nbyte) << "Generating FR#" << framecnt
                        << ", fill=" << format("0x%llx", fpargs->fill)
                        << ", m=" << format("0x%llx", m)
                        << endl);
                for( unsigned int j=0, k=0; j<n_ull_p_fr; j++, k+=sizeof(uint64_t) ) {
                    // a frame may be sent in multiple chunks of
                    // 'readsize'. each 'readsize' starts with an
                    // optional part of uncompressed data; the
                    // compressoffset
                    if( (j%n_ull_p_rd)==0 )
                        k = 0;
                    ull[j] = (k<co)?(fpargs->fill):(fpargs->fill & m);
                }
                // overwrite the syncword
                ::memcpy(framebuf + hdr.syncwordoffset, hdr.syncword, hdr.syncwordsize);

                // generated another frame, increment fillpattern to the next value
                fpargs->fill += fpargs->inc;

                // start the next frame
                framebyte = 0;
                framecnt++;
            }

            // At each datagramboundary we decide what to check
            // next:
            //  * checking fillpattern
            //  * (keep on) checking framebytes
            if( (nbyte%rd)==0 ) {
                uint64_t*  data  = (uint64_t*)(bptr + i);
                uint64_t*  check = (uint64_t*)(framebuf + framebyte);
                if( *data==fp && *data!=*check ) {
                    // don't expect fillpattern but we see it in the data
                    // so we check for a whole packet of fillpattern
                    fpbyte      = 0;
                    checkptr    = fppkt;
                    countptr    = &fpbyte;
                    DEBUG(4,format("%8llu ", nbyte) <<  "DG boundary => detected unexpected fillpattern" << endl);
                } else {
                    checkptr    = framebuf;
                    countptr    = &framebyte;
                    DEBUG(4,format("%8llu ", nbyte) <<  "DG boundary => continuing checking frame @" << framebyte << endl);
                }
            }
            // carry on checking current byte
            if( (ok = (bptr[i]==checkptr[*countptr]))==false ) {
                ostringstream   msg;
                msg << "framechecker[" << byteprint((double)nbyte, "byte") << "]: BLK" << blockcnt << "[" << i << "] "
                    << "FR" << (framecnt-1) << "[" << framebyte << "] "
                    << format("0x%02x", bptr[i]) << " != " << format("0x%02x", checkptr[*countptr])
                    << endl;
                DEBUG(-1, msg.str());
            }
            // Update loopvariables
            nbyte++;
            counter++;
            (*countptr)++;
        }
        blockcnt++;
    }
    delete [] framebuf;
    delete [] fppkt;
    DEBUG(0, "framechecker: done" << endl);
}

// Depending on wether we expect frames or blocks drop into the appropriate
// checkfuntion
void checker(inq_type<block>* inq, sync_type<fillpatargs>* args) {
    ASSERT2_COND(args->userdata->rteptr, SCINFO("No runtime pointer!"));

    runtime*                rteptr = args->userdata->rteptr;
    const headersearch_type hdr(rteptr->trackformat(), rteptr->ntrack(), (unsigned int)rteptr->trackbitrate());

    // Data is sent in frames if the framesize is constrained
    if( hdr )
        framechecker(inq, args);
    else
        blockchecker(inq, args);
    return;
}

// Decode + print all timestamps
void timeprinter(inq_type<frame>* inq, sync_type<headersearch_type>* args) {
    char                buf[64];
    frame               f;
    size_t              l;
    uint64_t            nframe = 0;
    struct tm           frametime_tm;
    struct timespec     frametime;
    headersearch_type   header = *args->userdata;

    DEBUG(2,"timeprinter: starting - " << header.frameformat << " " << header.ntrack << endl);
    while( inq->pop(f) ) {
        if( f.frametype!=header.frameformat ||
            f.ntrack!=header.ntrack ) {
            DEBUG(-1, "timeprinter: expect " << header.ntrack << " track " << header.frameformat
                      << ", got " << f.ntrack << " track " << f.frametype << endl);
            continue;
        }
        frametime = header.timestamp((unsigned char const*)f.framedata.iov_base);
        ::gmtime_r(&frametime.tv_sec, &frametime_tm);
        // Format the data + hours & minutes. Seconds will be dealt with
        // separately
        l = ::strftime(&buf[0], sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm", &frametime_tm);
        ::snprintf(&buf[l], sizeof(buf)-l, "%07.4fs", frametime_tm.tm_sec + ((double)frametime.tv_nsec * 1.0e-9));
        DEBUG(1, "[" << header.frameformat << " " << header.ntrack << "] " << buf << endl);
        nframe++;
    }
    DEBUG(2,"timeprinter: stopping" << endl);
}

void timedecoder(inq_type<frame>* inq, outq_type<frame>* oq, sync_type<headersearch_type>* args) {
    frame               f;
    struct timespec     frametime;
    headersearch_type   header = *args->userdata;

    DEBUG(2,"timedecoder: starting - " << header.frameformat << " " << header.ntrack << endl);
    while( inq->pop(f) ) {
        if( f.frametype!=header.frameformat ||
            f.ntrack!=header.ntrack ) {
            DEBUG(-1, "timedecoder: expect " << header.ntrack << " track " << header.frameformat
                      << ", got " << f.ntrack << " track " << f.frametype << endl);
            continue;
        }
        frametime = header.timestamp((unsigned char const*)f.framedata.iov_base);
        if( oq->push(f)==false )
            break;
    }
    DEBUG(2,"timedecodeer: stopping" << endl);
}


void bitbucket(inq_type<block>* inq) {
    block  b;
    DEBUG(2, "bitbucket: starting" << endl);
    while( inq->pop(b) ) { };
    DEBUG(2, "bitbucket: stopping" << endl);
}


#define MK4_TRACK_FRAME_SIZE	2500
#define MK4_TRACK_FRAME_WORDS	(MK4_TRACK_FRAME_SIZE / sizeof(uint32_t))

void
fakerargs::init_mk4_frame()
{
    int ntracks = rteptr->ntrack();
    uint32_t *frame32;
    size_t i;

    size = ntracks * MK4_TRACK_FRAME_SIZE;
    buffer = new unsigned char[size];
    frame32 = (uint32_t *)buffer;

    memset(frame32, 0, ntracks * sizeof(header));
    memset(header, 0, sizeof(header));

    header[8] = 0xff;
    header[9] = 0xff;
    header[10] = 0xff;
    header[11] = 0xff;
    for (i = ntracks * 2; i < (size_t)ntracks * 3; i++)
        frame32[i] = 0xffffffff;

    for (i = ntracks * 5; i < ntracks * MK4_TRACK_FRAME_WORDS; i++)
        frame32[i] = 0x11223344;
}

void
fakerargs::update_mk4_frame(time_t clock)
{
    int ntracks = rteptr->ntrack();
    uint8_t *frame8 = (uint8_t *)buffer;
    uint16_t *frame16 = (uint16_t *)buffer;
    uint32_t *frame32 = (uint32_t *)buffer;
    uint64_t *frame64 = (uint64_t *)buffer;
    unsigned int crc;
    struct tm tm;
    int i, j;

    gmtime_r(&clock, &tm);
    tm.tm_yday += 1;

    header[12] = (unsigned char)((tm.tm_year / 1) % 10 << 4);
    header[12] = (unsigned char)(header[12] | ((tm.tm_yday / 100) % 10));
    header[13] = (unsigned char)((tm.tm_yday / 10) % 10 << 4);
    header[13] = (unsigned char)(header[13] | ((tm.tm_yday / 1) % 10));
    header[14] = (unsigned char)((tm.tm_hour / 10) % 10 << 4);
    header[14] = (unsigned char)(header[14] | ((tm.tm_hour / 1) % 10 << 0));
    header[15] = (unsigned char)((tm.tm_min / 10) % 10 << 4);
    header[15] = (unsigned char)(header[15] | ((tm.tm_min / 1) % 10));

    header[16] = (unsigned char)((tm.tm_sec / 10) % 10 << 4);
    header[16] = (unsigned char)(header[16] | ((tm.tm_sec / 1) % 10));
    header[17] = (unsigned char)0;
    header[18] = (unsigned char)0;
    header[19] = (unsigned char)0;
    crc = crc12_mark4((unsigned char *)&header, sizeof(header));
    header[18] = (unsigned char)((crc >> 8) & 0x0f);
    header[19] = (unsigned char)(crc & 0xff);

    switch (ntracks) {
    case 8:
        for (i = 12; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame8[(i * 8) + j] = ~0;
                else
                    frame8[(i * 8) + j] = 0;
            }
        }
        break;
    case 16:
        for (i = 12; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame16[(i * 8) + j] = ~0;
                else
                    frame16[(i * 8) + j] = 0;
            }
        }
        break;
    case 32:
        for (i = 12; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame32[(i * 8) + j] = ~0;
                else
                    frame32[(i * 8) + j] = 0;
            }
        }
        break;
    case 64:
        for (i = 12; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame64[(i * 8) + j] = ~0;
                else
                    frame64[(i * 8) + j] = 0;
            }
        }
        break;
    }
}

#define MK5B_FRAME_WORDS	2504
#define MK5B_FRAME_SIZE		(MK5B_FRAME_WORDS * sizeof(uint32_t))

void
fakerargs::init_mk5b_frame()
{
    uint32_t *frame32;
    int i, j;

    size = 16 * MK5B_FRAME_SIZE;
    buffer = new unsigned char[size];
    frame32 = (uint32_t *)buffer;

    for (i = 0; i < 16; i++) {
        frame32[i * MK5B_FRAME_WORDS + 0] = 0xabaddeed;
        frame32[i * MK5B_FRAME_WORDS + 1] = i;
        frame32[i * MK5B_FRAME_WORDS + 2] = 0x00000000;
        frame32[i * MK5B_FRAME_WORDS + 3] = 0x00000000;

        for (j = 4; j < MK5B_FRAME_WORDS; j++)
            frame32[i * MK5B_FRAME_WORDS + j] = 0x11223344;
    }
}

void
fakerargs::update_mk5b_frame(time_t clock)
{
    uint32_t *frame32 = (uint32_t *)buffer;
    int mjd, sec, i;
    uint32_t word;

    mjd = 40587 + (clock / 86400);
    sec = clock % 86400;

    word = 0;
    word |= ((sec / 1) % 10) << 0;
    word |= ((sec / 10) % 10) << 4;
    word |= ((sec / 100) % 10) << 8;
    word |= ((sec / 1000) % 10) << 12;
    word |= ((sec / 10000) % 10) << 16;
    word |= ((mjd / 1) % 10) << 20;
    word |= ((mjd / 10) % 10) << 24;
    word |= ((mjd / 100) % 10) << 28;

    for (i = 0; i < 16; i++)
        frame32[i * MK5B_FRAME_WORDS + 2] = word;
}

void fakerargs::init_frame()
{
    switch(rteptr->trackformat()) {
    case fmt_mark4:
        init_mk4_frame();
        break;
    case fmt_mark5b:
        init_mk5b_frame();
        break;
    default:
        break;
    }
}

void fakerargs::update_frame(time_t clock)
{
    switch(rteptr->trackformat()) {
    case fmt_mark4:
        update_mk4_frame(clock);
        break;
    case fmt_mark5b:
        update_mk5b_frame(clock);
        break;
    default:
        break;
    }
}

void faker(inq_type<block>* inq, outq_type<block>* outq, sync_type<fakerargs>* args)
{
    runtime* rteptr;
    fakerargs* fakeargs = args->userdata;
    struct timespec tv;
    pop_result_type ret;
    int ntimeouts = 0;
    time_t clock = 0;
    block b;

    // Assert we do have a runtime pointer!
    ASSERT2_COND(rteptr = fakeargs->rteptr, SCINFO("OH NOES! No runtime pointer!"));

    // Do the initializing of the fakerargs with the lock held
    SYNCEXEC(args,
             fakeargs->init_frame();
             if( fakeargs->framepool==0 )
                fakeargs->framepool = new blockpool_type(fakeargs->size, 4));

    while( true ) {
#if !defined(__APPLE__)
        ::clock_gettime(CLOCK_REALTIME, &tv);
#else
        struct timeval  osx_doesnt_fucking_have_clockgettime_fucking_retards;
        ::gettimeofday(&osx_doesnt_fucking_have_clockgettime_fucking_retards, 0);
        tv.tv_sec  = osx_doesnt_fucking_have_clockgettime_fucking_retards.tv_sec;
        tv.tv_nsec = osx_doesnt_fucking_have_clockgettime_fucking_retards.tv_usec*1000;
#endif
        tv.tv_sec += 1;
        ret = inq->pop(b, tv);
        if (ret == pop_disabled)
            break;
        if (ret == pop_timeout) {
            if (ntimeouts++ > 1) {
                fakeargs->update_frame(clock);
                // get a fresh block from the fakeframepool - so as not to
                // mess up refcounting
                b = fakeargs->framepool->get();
                // and copy over the prepared framedata
                ::memcpy(b.iov_base, fakeargs->buffer, b.iov_len);
                clock = ::time(NULL);
            } else {
              clock = ::time(NULL);
              continue;
            }
        } else {
            ntimeouts = 0;
        }

	if( outq->push(b)==false )
            break;
    }
}

// create a networkserver from the settings
// in "networkargs.(runtime*)->netparms_type"
// if protocol==rtcp (reverse tcp) it will be an
// outgoing connection.
fdreaderargs* net_server(networkargs net) {
    // get access to the actual network parameters
    const netparms_type&  np = net.rteptr->netparms;
    const string          proto = np.get_protocol();
    unsigned int          olen( sizeof(np.sndbufsize) );
    // we're supposed to deliver a fresh instance of one of these
    fdreaderargs*         rv = new fdreaderargs();

    // copy over the runtime pointer
    rv->rteptr    = net.rteptr;
    rv->doaccept  = (proto=="tcp" || proto=="unix");
    rv->blocksize = net.rteptr->netparms.get_blocksize();

    // and do our thang
    if( proto=="rtcp" )
        rv->fd = getsok(np.host, np.get_port(), "tcp");
    else if( proto=="unix" )
        rv->fd = getsok_unix_server(np.host);
    else
        rv->fd = getsok(np.get_port(), proto, np.host);

    // set send/receive bufsize on the sokkit
    if( np.sndbufsize>0 ) {
        ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_SNDBUF, &np.sndbufsize, olen) );
    }
    if( np.rcvbufsize>0 ) {
        ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_RCVBUF, &np.rcvbufsize, olen) );
    }
    return rv;
}

// Given a "networkargs" argument, this function transforms
// that into a newly allocated fdreaderargs with the filedescriptor
// initialized to a sokkit pertaining to have the props (protocol,
// server or client sokkit ("reverse" tcp)) that can be derived
// from said networkargs.
fdreaderargs* net_client(networkargs net) {
    // get access to the actual network parameters
    const netparms_type&  np = net.rteptr->netparms;
    const string          proto = np.get_protocol();
    // we're supposed to deliver a fresh instance of one of these
    unsigned int          olen( sizeof(np.rcvbufsize) );
    fdreaderargs*         rv = new fdreaderargs();

    // copy over the runtime pointer
    rv->rteptr   = net.rteptr;
    rv->doaccept = (proto=="rtcp");

    // and do our thang
    if( proto=="rtcp" )
        rv->fd = getsok(np.get_port(), "tcp");
    else if( proto=="unix" )
        rv->fd = getsok_unix_client(np.host);
    else
        rv->fd = getsok(np.host, np.get_port(), proto);

    // set send/receive bufsize on the sokkit
    if( np.rcvbufsize>0 ) {
        ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_RCVBUF, &np.rcvbufsize, olen) );
    }
    if( np.sndbufsize>0 ) {
        ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_SNDBUF, &np.sndbufsize, olen) );
    }
    return rv;
}

struct modal {
    modal(int m) :
        mode(m)
    {}
    int mode;
};

#define OPENKEES(s, i, flag) \
    if( (i&flag)!=0 ) s << #flag << ",";

ostream& operator<<(ostream& os, const modal& m) {
    os << "<";
    OPENKEES(os, m.mode, O_RDONLY);
    OPENKEES(os, m.mode, O_WRONLY);
    OPENKEES(os, m.mode, O_CREAT);
    OPENKEES(os, m.mode, O_APPEND);
    OPENKEES(os, m.mode, O_TRUNC);
    OPENKEES(os, m.mode, O_RDWR);
    OPENKEES(os, m.mode, O_EXCL);
    os << ">";
    return os;
}

// If the OS we're running on has O_LARGEFILE ... use that!
#ifdef O_LARGEFILE
    #define LARGEFILEFLAG  O_LARGEFILE
#else
    #define LARGEFILEFLAG  0
#endif

// <filename> should be
//   [/]path/to/file.name,<mode>
// where <mode> is
//    "r"   => open file readonly
//    "w"   => open file for writing, create it if necessary, truncate it
//    "a"   => open file for appending, create it if necessary
// the file will be opened in binary 
fdreaderargs* open_file(string filename, runtime* r) {
    int               flag = LARGEFILEFLAG; // smoke'm if you got'em
    mode_t            mode = 0; // only used when creating
    string            openmode;
    string            actualfilename;
    fdreaderargs*     rv = new fdreaderargs();
    string::size_type openmodeptr;

    openmodeptr = filename.find(",");
    ASSERT2_COND( openmodeptr!=string::npos,
                  SCINFO(" add ',<mode>' to the filename (r,w,a)") );

    openmode       = tolower(filename.substr(openmodeptr+1));
    actualfilename = filename.substr(0, openmodeptr);

    ASSERT2_COND( filename.size()>0,
                  SCINFO(" no actual filename given") );
    ASSERT2_COND( openmode.size()>0,
                  SCINFO(" no actual openmode (r,w,a) given") );

    if( openmode=="r" ) 
        flag |= O_RDONLY;
    else if( openmode=="w" ) {
        // create the file if it don't exist
        flag |= (O_WRONLY | O_CREAT | O_TRUNC);
        // file rw for us, r for everyone else
        mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH;
    }
    else if( openmode=="a" ) {
        // create the file if it don't exist
        flag |= (O_WRONLY | O_CREAT | O_APPEND);
        // file rw for us, r for everyone else
        mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH;
    } else {
        ASSERT2_COND( false, SCINFO("invalid openmode '" << openmode << "'") );
    }
    ASSERT2_COND( (rv->fd=::open(actualfilename.c_str(), flag, mode))!=-1,
                  SCINFO(actualfilename << ", flag=" << modal(flag) << ", mode=" << mode) );
    DEBUG(0, "open_file: opened " << actualfilename << " as fd=" << rv->fd << endl);
    rv->rteptr = r;
    return rv;
}

fdreaderargs* open_socket(string filename, runtime* r) {
    fdreaderargs*     rv = new fdreaderargs();
    struct sockaddr_un sun;
    int s;

    ::unlink(filename.c_str());

    ASSERT_COND( (s=::socket(PF_LOCAL, SOCK_STREAM, 0))!=-1 );

    sun.sun_family = AF_LOCAL;
    strncpy(sun.sun_path, filename.c_str(), sizeof(sun.sun_path));
    ASSERT2_COND( ::bind(s, (struct sockaddr *)&sun, sizeof(sun))!=-1,
		  SCINFO(filename) );

    ASSERT_COND( ::listen(s, 1)!=-1 );

    rv->fd = s;
    DEBUG(0, "open_socket: opened " << filename << " as fd=" << rv->fd << endl);
    rv->rteptr = r;
    return rv;
}



// * close the filedescriptor
// * set the value to "-1"
// * if the threadid is not-null, signal the thread so it will
//   fall out of any blocking systemcall
void close_filedescriptor(fdreaderargs* fdreader) {
    ASSERT_COND(fdreader);

    if( fdreader->fd!=-1 ) {
        ASSERT_ZERO( ::close(fdreader->fd) );
        DEBUG(3, "close_filedescriptor: closed fd#" << fdreader->fd << endl);
    }
    fdreader->fd = -1;
    if( fdreader->threadid!=0 ) {
        int rv = ::pthread_kill(*fdreader->threadid, SIGUSR1);

        // only acceptable returnvalues are 0 (=success) or ESRCH,
        // which means the thread has already terminated.
        if( rv!=0 && rv!=ESRCH ) {
            DEBUG(-1, "close_network: FAILED to SIGNAL THREAD - " << ::strerror(rv) << endl);
        }
    }
}






// ..

frame::frame() :
    frametype( fmt_unknown ), ntrack( 0 )
{ frametime.tv_sec = 0; frametime.tv_nsec = 0; }
frame::frame(format_type tp, unsigned int n, block data):
    frametype( tp ), ntrack( n ), framedata( data )
{ frametime.tv_sec = 0; frametime.tv_nsec = 0;}
frame::frame(format_type tp, unsigned int n, struct timespec ft, block data):
    frametype( tp ), ntrack( n ), frametime( ft ), framedata( data )
{}



framerargs::framerargs(headersearch_type h, runtime* rte) :
    rteptr(rte), pool(0), hdr(h)
{ ASSERT_NZERO(rteptr); }
framerargs::~framerargs() {
    delete pool;
}

fillpatargs::fillpatargs():
    run( false ), fill( ((uint64_t)0x11223344 << 32) + 0x11223344 ),
    inc( 0 ), rteptr( 0 ), nword( (uint64_t)-1), pool( 0 )
{}

fillpatargs::fillpatargs(runtime* r):
    run( false ), fill( ((uint64_t)0x11223344 << 32) + 0x11223344 ),
    inc( 0 ), rteptr( r ), nword( (uint64_t)-1), pool( 0 )
{ ASSERT_NZERO(rteptr); }

void fillpatargs::set_run(bool newval) {
    run = newval;
}
void fillpatargs::set_nword(uint64_t n) {
    nword = n;
}

fillpatargs::~fillpatargs() {
    delete pool;
}


fakerargs::fakerargs():
    rteptr( 0 ), buffer( 0 ), framepool( 0 )
{}

fakerargs::fakerargs(runtime* rte):
  rteptr( rte ), buffer( 0 )
{ ASSERT_NZERO(rteptr); }

fakerargs::~fakerargs() {
    delete [] buffer;
    delete framepool;
}


compressorargs::compressorargs(): rteptr(0) {}
compressorargs::compressorargs(runtime* p):
    rteptr(p)
{ ASSERT_NZERO(rteptr); }


fiforeaderargs::fiforeaderargs() :
    run( false ), rteptr( 0 ), pool( 0 )
{}
fiforeaderargs::fiforeaderargs(runtime* r) :
    run( false ), rteptr( r ), pool( 0 )
{ ASSERT_NZERO(rteptr); }

void fiforeaderargs::set_run( bool newrunval ) {
    run = newrunval;
}

fiforeaderargs::~fiforeaderargs() {
    delete pool;
}

diskreaderargs::diskreaderargs() :
    run( false ), repeat( false ), rteptr( 0 ), pool( 0 )
{}
diskreaderargs::diskreaderargs(runtime* r) :
    run( false ), repeat( false ), rteptr( r ), pool( 0 )
{ ASSERT_NZERO(rteptr); }

void diskreaderargs::set_start( playpointer s ) {
    pp_start = s;
}
void diskreaderargs::set_end( playpointer e ) {
    pp_end = e;
}
void diskreaderargs::set_repeat( bool b ) {
    repeat = b;
}
void diskreaderargs::set_run( bool b ) {
    run = b;
}
diskreaderargs::~diskreaderargs() {
    delete pool;
}

reorderargs::reorderargs():
    dgflag(0), first(0), rteptr(0), buffer(0)
{}
reorderargs::reorderargs(runtime* r):
    dgflag(0), first(0), rteptr(r), buffer(0)
{ ASSERT_NZERO(rteptr); }

reorderargs::~reorderargs() {
    delete [] dgflag;
    delete [] first;
    delete [] buffer;
}

networkargs::networkargs() :
    rteptr( 0 )
{}
networkargs::networkargs(runtime* r):
    rteptr( r )
{ ASSERT_NZERO(rteptr); }

fdreaderargs::fdreaderargs():
    fd( -1 ), doaccept(false), 
    rteptr( 0 ), threadid( 0 ),
    blocksize( 0 ), pool( 0 )
{}
fdreaderargs::~fdreaderargs() {
    delete pool;
    delete threadid;
}

buffererargs::buffererargs() :
    rte(0), bytestobuffer(0)
{}
buffererargs::buffererargs(runtime* rteptr, unsigned int n) :
    rte(rteptr), bytestobuffer(n)
{ ASSERT_NZERO(rteptr); }

unsigned int buffererargs::get_bufsize( void ) {
    return bytestobuffer;
}

buffererargs::~buffererargs() {
}

splitterargs::splitterargs():
    rte(0), pool(0), nchunk(0), multiplier(0)
{}

splitterargs::splitterargs(runtime* rteptr, unsigned int n, unsigned int m):
    rte(rteptr), pool(0), nchunk(n), multiplier(m)
{ ASSERT_NZERO(rteptr); ASSERT_NZERO(nchunk); }

splitterargs::~splitterargs() {
    delete pool;
}


reframe_args::reframe_args(uint16_t sid, unsigned int br,
                           unsigned int ip, unsigned int op):
    station_id(sid), pool(0),
    bitrate(br), input_size(ip), output_size(op)
{}

reframe_args::~reframe_args() {
    delete pool;
}


//
//    multi-destination stuff
//



multidestparms::multidestparms(runtime* rte, const chunkdestmap_type& cdm) :
    rteptr( rte ), chunkdestmap( cdm )
{ ASSERT_NZERO(rteptr); }

multifdargs::multifdargs(runtime* rte) :
    rteptr( rte )
{ ASSERT_NZERO(rteptr); }

// Chunkdest-Map: maps chunkid (uint) => destination (string)
// First create the set of unique destinations
//   (>1 chunk could go to 1 destination)
// Then create those filedescriptors
// Then go through chunkdest-map again and build 
// a new mapping from destination => filedescriptor
// Build a return value of chunkid => filedescriptor
multifdargs* multiopener( multidestparms mdp ) {
    typedef std::map<std::string, int>  destfdmap_type;
    multifdargs*                      rv = new multifdargs( mdp.rteptr );
    destfdmap_type                    destfdmap;
    const std::string                 proto( mdp.rteptr->netparms.get_protocol() );
    chunkdestmap_type::const_iterator curchunk;

    for(curchunk=mdp.chunkdestmap.begin(); curchunk!=mdp.chunkdestmap.end(); curchunk++) {
        // check if we already have this destination
        destfdmap_type::iterator  chunkdestfdptr = destfdmap.find( curchunk->second );

        if( chunkdestfdptr==destfdmap.end() ) {
            unsigned short                            port  = netparms_type::defPort;
            std::vector<std::string>                  parts = ::split(curchunk->second, '@');
            std::pair<destfdmap_type::iterator, bool> insres;

            ASSERT2_COND( parts.size()>0 && parts.size()<=2 && parts[0].empty()==false,
                          SCINFO("Invalid formatted address " << curchunk->second) );
            if( parts.size()>1 ) {
                long int v = -1;
                
                v = ::strtol(parts[1].c_str(), 0, 0);

                ASSERT2_COND(v>0 && v<=USHRT_MAX, SCINFO("invalid portnumber " << parts[1] ) );
                port = (unsigned short)v;
            }
            insres = destfdmap.insert( make_pair(curchunk->second, ::getsok(parts[0], port, proto)) );
            ASSERT2_COND(insres.second, SCINFO(" Arg! Failed to insert entry into map!"));
            chunkdestfdptr = insres.first;
            DEBUG(3," multiopener: opened destination " << curchunk->second << " as fd #" << chunkdestfdptr->second << endl);
        }
        // Now add it to our outputmap
        rv->dstfdmap.insert( make_pair(curchunk->first, chunkdestfdptr->second) );
    }
    return rv;
}

// Chunkdest-Map: maps chunkid (uint) => destination (string)
// First create the set of unique destinations
//   (>1 chunk could go to 1 destination)
// Then create those filedescriptors
// Then go through chunkdest-map again and build 
// a new mapping from destination => filedescriptor
// Build a return value of chunkid => filedescriptor
multifdargs* multifileopener( multidestparms mdp ) {
    typedef std::map<std::string, int>  destfdmap_type;
    multifdargs*                      rv = new multifdargs( mdp.rteptr );
    destfdmap_type                    destfdmap;
    chunkdestmap_type::const_iterator curchunk;

    for(curchunk=mdp.chunkdestmap.begin(); curchunk!=mdp.chunkdestmap.end(); curchunk++) {
        // check if we already have this destination
        destfdmap_type::iterator  chunkdestfdptr = destfdmap.find( curchunk->second );

        if( chunkdestfdptr==destfdmap.end() ) {
            fdreaderargs*                             of = open_file(curchunk->second, 0);
            std::pair<destfdmap_type::iterator, bool> insres;

            insres = destfdmap.insert( make_pair(curchunk->second, of->fd) );
            ASSERT2_COND(insres.second, SCINFO(" Arg! Failed to insert entry into map!"));
            chunkdestfdptr = insres.first;
            DEBUG(3," multifileopener: opened destination " << curchunk->second << " as fd #" << chunkdestfdptr->second << endl);
            delete of;
        }
        // Now add it to our outputmap
        rv->dstfdmap.insert( make_pair(curchunk->first, chunkdestfdptr->second) );
    }
    return rv;
}

// a split function takes a void* to the block that needs splitting,
// the size of the block and a variable amount of pointers to buffers.
// The caller is responsible for making sure there are enough pointers
// passed and they point to appropriately sized buffers
typedef void (*split_func)(void* block, unsigned int blocksize, ...);


void marks_2Ch2bit1to2(void* block, unsigned int blocksize, ...) {
    void*   chunk[2];
    va_list args;

    // varargs processing
    va_start(args, blocksize);
    for(unsigned int i=0; i<2; i++)
        chunk[i] = va_arg(args, void*);
    va_end(args);

    // and fall into the correct version
    extract_2Ch2bit1to2(block, chunk[0], chunk[1], blocksize/2);
    return;
}
void marks_4Ch2bit1to2(void* block, unsigned int blocksize, ...) {
    void*   chunk[4];
    va_list args;

    // varargs processing
    va_start(args, blocksize);
    for(unsigned int i=0; i<4; i++)
        chunk[i] = va_arg(args, void*);
    va_end(args);

    // and fall into the correct version
    extract_4Ch2bit1to2(block, chunk[0], chunk[1], chunk[2], chunk[3], blocksize/4);
    return;
}
void marks_8Ch2bit1to2(void* block, unsigned int blocksize, ...) {
    void*   chunk[8];
    va_list args;

    // varargs processing
    va_start(args, blocksize);
    for(unsigned int i=0; i<8; i++)
        chunk[i] = va_arg(args, void*);
    va_end(args);

    // and fall into the correct version
    extract_8Ch2bit1to2(block, chunk[0], chunk[1], chunk[2], chunk[3], 
                               chunk[4], chunk[5], chunk[6], chunk[7],
                               blocksize/8);
    return;
}

void marks_16Ch2bit1to2(void* block, unsigned int blocksize, ...) {
    void*   chunk[16];
    va_list args;

    // varargs processing
    va_start(args, blocksize);
    for(unsigned int i=0; i<16; i++)
        chunk[i] = va_arg(args, void*);
    va_end(args);

    // and fall into the correct version
    extract_16Ch2bit1to2(block, chunk[0], chunk[1], chunk[2], chunk[3], 
                                chunk[4], chunk[5], chunk[6], chunk[7],
                                chunk[8], chunk[9], chunk[10], chunk[11], 
                                chunk[12], chunk[13], chunk[14], chunk[15],
                                blocksize/16);
    return;
}

// Take incoming blocks of data and split into parts as per splitfunction in
// the splitterargs. Outputs tagged SHORT frames with tags as defined 
// in the splitterargs.
// This might be a slow one - you may want to use the 'tagger' +
// 'coalescing_splitter' below. 
// First tag all frames with a default tag and then coalesce 'nchunk' input
// frames to 'nchunk' output frames - but with the bytes rearranged
void splitter( inq_type<frame>* inq, outq_type<tagged<frame> >* outq, sync_type<splitterargs>* args) {
    frame          f;
    splitterargs*  splitargs = args->userdata;
    runtime*       rteptr    = (splitargs?splitargs->rte:0);
    split_func     split     = (split_func)&marks_4Ch2bit1to2;
    void*          chunk[16];
    const unsigned int   nchunk = 16;
    uint64_t       framenr = 0;

    // Assert we have arguments
    ASSERT_NZERO( splitargs && rteptr );

    // Input- and output headertypes. We assume the frame is split in nchunk
    // equal parts, effectively reducing the number-of-tracks by a factor of
    // nchunk. 
    const headersearch_type header(rteptr->trackformat(), rteptr->ntrack(), (unsigned int)rteptr->trackbitrate());
    const format_type       out_fmt     = header.frameformat;
    const unsigned int      out_ntrk    = header.ntrack/nchunk;
    const unsigned int      channel_len = header.framesize/nchunk;

    // We must prepare our blockpool depending on which data we expect to be
    // getting.
    // Mark K's sse-dechannelizing routines access 16 bytes past the end.
    // So we add extra bytes to the end
    SYNCEXEC(args,
             splitargs->pool = new blockpool_type(header.framesize+nchunk*16, 8));
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "extract_16Ch2bit1to2/frame", 0));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    DEBUG(-1, "splitter/frame starting: expect " << header << endl);
    DEBUG(-1, "splitter/frame splitting into " << nchunk << " pieces of " << channel_len << endl);
    while( inq->pop(f) ) {
        block          b = splitargs->pool->get();
        unsigned int   i;
        unsigned int   j;
        unsigned char* buffer = (unsigned char*)b.iov_base;

        if( !(f.frametype==header.frameformat  && f.ntrack==header.ntrack && f.framedata.iov_len==header.framesize) ) {
            DEBUG(-1, "splitter/frame: expect " << header << " got " << f.ntrack << " x " << f.frametype << endl);
            continue;
        }

        // compute chunkpointers - make sure that unused args are set to 0
        for(i=0; i<nchunk; i++, buffer += (channel_len+16)) 
            chunk[i] = buffer;
        for( ; i<16; i++)
            chunk[i] = 0;

        // split the frame's data into its constituent parts
        split(f.framedata.iov_base, f.framedata.iov_len,
              chunk[0], chunk[1], chunk[2], chunk[3],
              chunk[4], chunk[5], chunk[6], chunk[7],
              chunk[8], chunk[9], chunk[10], chunk[11],
              chunk[12], chunk[13], chunk[14], chunk[15]);
        framenr++;
        counter += f.framedata.iov_len;

        // and send the chunks downstream
        for(j=0; j<nchunk; j++)
            if( outq->push( tagged<frame>(j, frame(out_fmt, out_ntrk, f.frametime,
                                                   b.sub(j*(channel_len+16), channel_len))) )==false )
                break;

        if( j<nchunk ) {
            DEBUG(-1, "splitter/frame: failed to push channelized data. stopping." << endl);
            break;
        }
    }
    DEBUG(2, "splitter/frame done " << framenr << " frames" << endl);
}


// If you want to assign a default tag to untagged blocks,
// use this one.
// This allows you to use the coalescing_splitter below (which only
// accepts tagged frames as input)
void tagger( inq_type<frame>* inq, outq_type<tagged<frame> >* outq, sync_type<unsigned int>* args ) {
    frame              f;
    const unsigned int tag = *args->userdata;
    while( inq->pop(f) )
        outq->push( tagged<frame>(tag, f) );
}

// The coalesing_splitter below splits individual incoming tags into N output tags, coalescing
// N input frames (such that N input frames of tag X result into
// N output frames with tags Z[0], Z[1], ... , Z[N-1]
// where Z[n] == splitterargs.outputtag(X, n)

// state for each incoming tag X
struct tag_state {
    block               tagblock[16];
    unsigned int        fcount;
    unsigned char*      chunk[16];
    struct timespec     out_ts;

    tag_state( blockpool_type* bp, unsigned int nch ):
        fcount( 0 )
    {
        for(unsigned  int tmpt=0; tmpt<nch; tmpt++) {
            tagblock[tmpt] = bp->get();
            chunk[tmpt]    = (unsigned char*)tagblock[tmpt].iov_base;
        }
        out_ts.tv_sec  = 0;
        out_ts.tv_nsec = 0;
    }

    private:
    tag_state();
};


void coalescing_splitter( inq_type<tagged<frame> >* inq, outq_type<tagged<frame> >* outq, sync_type<splitterargs>* args) {
    typedef std::map<unsigned int,tag_state> tag_state_map_type;

    splitterargs*       splitargs = args->userdata;
    tagged<frame>       tf;
    runtime*            rteptr    = (splitargs?splitargs->rte:0);
    split_func          splitfn   = 0;
    tag_state_map_type  tagstatemap;
    const unsigned int  nchunk     = (splitargs?splitargs->nchunk:0);
    const unsigned int  multiplier = (splitargs?splitargs->multiplier:0);

    // Assert we have arguments
    ASSERT_NZERO( splitargs && rteptr );

    switch( nchunk ) {
        case 2:
//            splitfn = (split_func)&marks_2Ch2bit1to2;
            splitfn = (split_func)&split16bitby2;
            break;
        case 4:
//            splitfn = (split_func)&marks_4Ch2bit1to2;
            splitfn = (split_func)&split8bitby4;
            break;
        case 8:
            splitfn = (split_func)&marks_8Ch2bit1to2;
            break;
        case 16:
            splitfn = (split_func)&marks_16Ch2bit1to2;
            break;
        default:
            break;
    }
    ASSERT2_COND( splitfn!=0,
                  SCINFO("cannot split in " << nchunk << " chunks [no splitfunction known]") );

    // Input- and output headertypes. We assume the frame is split in nchunk
    // equal parts, effectively reducing the number-of-tracks by a factor of
    // nchunk. 
    ostringstream           stepnm;
    blockpool_type*         blkpool  = 0;
    const headersearch_type header(rteptr->trackformat(), rteptr->ntrack(), (unsigned int)rteptr->trackbitrate());
    const format_type       out_fmt  = header.frameformat;
    const unsigned int      out_ntrk = header.ntrack/nchunk;
    const unsigned int      ch_len   = header.framesize/nchunk;


    // We must prepare our blockpool depending on which data we expect to be
    // getting.
    // Mark K's sse-dechannelizing routines access 16 bytes past the end.
    // So we add extra bytes to the end
    SYNCEXEC(args,
             blkpool = splitargs->pool = new blockpool_type(nchunk * header.framesize, nchunk));

    stepnm << "extract_" << nchunk << "Ch2bit1to2";
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, stepnm.str(), 0));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    DEBUG(-1, "coalescing_splitter/frame starting: expect " << header
              << " (coalescing " << nchunk << " frames of "
              << header.framesize << "bytes)" << endl);
    DEBUG(-1, "coalescing_splitter/frame splitting into " << nchunk << " pieces" << endl);

    while( inq->pop(tf) ) {
        if( !(tf.item.frametype==header.frameformat  &&
              tf.item.framedata.iov_len==header.framesize) ) {
            DEBUG(-1, "coalescing_splitter/frame: expect " << header
                      << " got " << tf.item.ntrack << " x " <<
                      tf.item.frametype << endl);
            break;
        }
        // OH NOES! SOME DATA CAME IN!
        // First of all, find the correct 'integration' -
        // we split <nchunk> blocks of each incoming <tag>
        // into <nchunk> different pieces. After having processed
        // <nchunk> frames of a particular tag, we send them
        // onwards downstream, potentially re-tagging them
        unsigned char**               chunk;
        tag_state_map_type::iterator  curtag;
       
        if( (curtag=tagstatemap.find(tf.tag))==tagstatemap.end() ) {
            // first time we see this tag - get a new integration state
            pair<tag_state_map_type::iterator, bool> insres;
            insres = tagstatemap.insert( make_pair(tf.tag, tag_state(blkpool, nchunk)) );
            ASSERT2_COND(insres.second,
                         SCINFO("Failed to insert new state for splitting into for tag #" << tf.tag));
            curtag = insres.first;

            // remember the time of the first frame
            curtag->second.out_ts = tf.item.frametime;
        }

        // Everything has been precomputed so we can get going right away
        tag_state&  tagstate( curtag->second );

        chunk = tagstate.chunk;
        splitfn(tf.item.framedata.iov_base, tf.item.framedata.iov_len,
                chunk[0], chunk[1], chunk[2], chunk[3],
                chunk[4], chunk[5], chunk[6], chunk[7], 
                chunk[8], chunk[9], chunk[10], chunk[11], 
                chunk[12], chunk[13], chunk[14], chunk[15]);

        // If we need to do another iteration, increment the pointers
        if( (++tagstate.fcount)<nchunk ) {
            for(unsigned int tmpt=0; tmpt<nchunk; tmpt++)
                chunk[tmpt] += ch_len;
            continue;
        }

        // Ok now push the dechannelized frames onward
        block*       tagblock = tagstate.tagblock;
        unsigned int j;
        for(j=0; j<nchunk; j++)
            if( outq->push( tagged<frame>(curtag->first*multiplier + j,
                                          frame(out_fmt, out_ntrk, tagstate.out_ts,
                                                tagblock[j].sub(0, header.framesize))) )==false )
                break;

        if( j<nchunk ) {
            DEBUG(-1, "coalescing_splitter/frame: failed to push channelized data. stopping." << endl);
            break;
        }
        counter += nchunk*header.framesize;

        // And reset for the next iteration - ie erase the integration for
        // the current tag
        tagstatemap.erase(curtag);
    }
    DEBUG(2, "coalescing_splitter/frame done " << endl);
}

struct vdif_header {
	  /* Word 0 */
	  unsigned int invalid:1;
	  unsigned int legacy:1;
	  unsigned int epoch_seconds:30;
	  /* Word 1 */
	  unsigned int unused:2;
	  unsigned int ref_epoch:6;
	  unsigned int data_frame_num:24;
	  /* Word 2 */
	  unsigned int version:3;
	  unsigned int log2nchans:5;
	  unsigned int data_frame_len8:24;
	  /* Word 3 */
	  unsigned int complex:1;
	  unsigned int bits_per_sample:5;
	  unsigned int thread_id:10;
	  /* station_id moved out of bit field */
	  unsigned int station_id:16;
#if 0
	  /* Word 4 */
	  unsigned int edv:8;
	  unsigned int extended_user_data:24;
	  /* words 5 t/m 7 */
	  unsigned int w4:32;
	  unsigned int w5:32;
	  unsigned int w6:32;
#endif
      vdif_header() {
          ::memset((void*)this, 0x0, sizeof(vdif_header));
          this->legacy = 1;
      }
};

// Reframe to vdif
void reframe_to_vdif(inq_type<tagged<frame> >* inq, outq_type<tagged<block> >* outq, sync_type<reframe_args>* args) {
    typedef std::map<unsigned int, vdif_header>  tagheadermap_type;
    bool               stop              = false;
    uint64_t           done              = 0;
    unsigned int       nchunk            = 0;
    unsigned int       dataframe_length  = 0;
    unsigned int       chunk_duration_ns = 0;
    reframe_args*      reframe = args->userdata;
    tagged<frame>      tf;
    tagheadermap_type  tagheader;
    const unsigned int bitrate     = reframe->bitrate;
    const unsigned int input_size  = reframe->input_size;
    const unsigned int output_size = reframe->output_size;

    // Given input and maxpayloadsize compute how large each chunk must be
    // We're looking for the largest multiple of 8 that will divide our
    // input size into an integral number of outputs
    for(unsigned int i=1; dataframe_length==0 && i<input_size; i++) {
        const unsigned int dfl = input_size/i;
        if( dfl%8==0 && dfl<(output_size-sizeof(vdif_header)) )
            dataframe_length = dfl;
    }
    ASSERT2_COND(dataframe_length!=0,
                 SCINFO("failed to find suitable VDIF dataframelength: input="
                        << input_size << ", output=" << output_size));
    nchunk            = input_size/dataframe_length;
    chunk_duration_ns = (unsigned int)((((double)dataframe_length * 8)/((double)bitrate))*1.0e9);

    // Now that we know how big our output chunks are going to be we can get
    // ourselves an appropriate blockpool
    SYNCEXEC(args,
             reframe->pool = new blockpool_type(dataframe_length+sizeof(vdif_header), 16));
    blockpool_type* pool = reframe->pool;

    DEBUG(1, "reframe_to_vdif: VDIF dataframe_length = " << dataframe_length <<
             " ipsize=" << input_size << ", opsize=" << output_size << ", bitrate=" << bitrate << endl <<
             "                 chunk duration = " << chunk_duration_ns << "ns" << endl);

    if( inq->pop(tf)==false ) {
        DEBUG(1, "reframe_to_vdif: cancelled before beginning" << endl);
        return;
    }
    // Ok, we *have* a frame! Now we can initialize our VDIF epoch stuff
    struct tm klad;

    ::gmtime_r(&tf.item.frametime.tv_sec, &klad);
    const int epoch    = (klad.tm_year + 1900 - 2000)/2 + (klad.tm_mon>=6);

    // Now set the zero point of that epoch, 00h00m00s on the 1st day of
    // month 0 (Jan) or 6 (July)
    klad.tm_hour  = 0;
    klad.tm_min   = 0; 
    klad.tm_sec   = 0;
    klad.tm_mon   = (klad.tm_mon/6)*6;
    klad.tm_mday  = 1;
    const time_t    tm_epoch = ::mktime(&klad);

    DEBUG(3, "reframe_to_vdif: epoch=" << epoch << ", tm_epoch=" << tm_epoch << endl);

    // Let's pre-create 16 headers for tags 0 .. 15
    // ASSUME we only have 1 channel of 2bits/sample in each incoming
    // frame (ie log2nchan == 0)
    for(unsigned int t=0; t<16; t++) {
        vdif_header&  hdr( tagheader[t] );

        hdr.station_id      = reframe->station_id;
        hdr.thread_id       = (short unsigned int)(t & 0x3ff);
        hdr.data_frame_len8 = (unsigned int)((dataframe_length/8) & 0x00ffffff);
        hdr.bits_per_sample = (unsigned char)(1 & 0x1f);
        hdr.ref_epoch       = (unsigned char)(epoch & 0x3f);
    }

    // By having waited for the first frame for setting up our timing,
    // our fast inner loop can be way cleanur!
    do {
        const unsigned int    last = tf.item.framedata.iov_len;
        unsigned char const*  data = (unsigned char const*)tf.item.framedata.iov_base;
        const struct timespec time = tf.item.frametime;

        if( last!=input_size ) {
            DEBUG(-1, "reframe_to_vdif: got inputsize " << last << ", expected " << input_size << endl);
            continue;
        }
        // break up the frame into smaller bits?
        vdif_header&      hdr      = tagheader[tf.tag];

        // dataframes cannot span second boundaries so this can be done
        // easily outside the breaking-up loop
        hdr.epoch_seconds   = (unsigned int)((time.tv_sec - tm_epoch) & 0x3fffffff);
//        hdr.data_frame_len8 = (unsigned int)((dataframe_length/8) & 0x00ffffff);

//        DEBUG(3, "reframe_to_vdif: new frame VDIF[" << tf.tag << "]:" << hdr.epoch_seconds << endl <<
//                 "                 " << time.tv_sec << "s " << time.tv_nsec << "ns" << endl);
        for(unsigned int dfn=time.tv_nsec/chunk_duration_ns, pos=0; !stop && pos<last; dfn++, pos+=dataframe_length) {
            block          b( pool->get() );
            unsigned char* bdata( (unsigned char*)b.iov_base );

            hdr.data_frame_num = (unsigned int)(dfn & 0x00ffffff);
            // copy vdif header and data
            ::memcpy(bdata, &hdr, sizeof(vdif_header));
            ::memcpy(bdata + sizeof(vdif_header), data+pos, dataframe_length);
            stop = (outq->push(tagged<block>(tf.tag, b))==false);
        }
        done++;
    } while( !stop && inq->pop(tf) );
    DEBUG(1, "reframe_to_vdif: done " << done << " frames " << endl);
}

void reframe_to_vdif_new(inq_type<tagged<frame> >* inq, outq_type<tagged<block> >* outq, sync_type<reframe_args>* args) {
    typedef std::map<unsigned int, vdif_header>  tagheadermap_type;
    bool               stop              = false;
    uint64_t           done              = 0;
    unsigned int       nchunk            = 0;
    unsigned int       dataframe_length  = 0;
    unsigned int       chunk_duration_ns = 0;
    reframe_args*      reframe = args->userdata;
    tagged<frame>      tf;
    tagheadermap_type  tagheader;
    const unsigned int bitrate     = reframe->bitrate;
    const unsigned int input_size  = reframe->input_size;
    const unsigned int output_size = reframe->output_size;

    // Given input and maxpayloadsize compute how large each chunk must be
    // We're looking for the largest multiple of 8 that will divide our
    // input size into an integral number of outputs
    for(unsigned int i=1; dataframe_length==0 && i<input_size; i++) {
        const unsigned int dfl = input_size/i;
        const unsigned int rem = input_size%dfl;
        if( dfl%8==0 && rem==0 && dfl<(output_size-sizeof(vdif_header)) )
            dataframe_length = dfl;
    }
    ASSERT2_COND(dataframe_length!=0,
                 SCINFO("failed to find suitable VDIF dataframelength: input="
                        << input_size << ", output=" << output_size));
    nchunk            = input_size/dataframe_length;
    chunk_duration_ns = (unsigned int)((((double)dataframe_length * 8)/((double)bitrate))*1.0e9);

    // Now that we know how big our output chunks are going to be we can get
    // ourselves an appropriate blockpool
    SYNCEXEC(args,
             reframe->pool = new blockpool_type(dataframe_length+sizeof(vdif_header), 16));
    blockpool_type* pool = reframe->pool;

    DEBUG(1, "reframe_to_vdif: VDIF dataframe_length = " << dataframe_length <<
             " ipsize=" << input_size << ", opsize=" << output_size << endl <<
             "                 chunk duration = " << chunk_duration_ns << "ns" << endl);

    if( inq->pop(tf)==false ) {
        DEBUG(1, "reframe_to_vdif: cancelled before beginning" << endl);
        return;
    }
    // Ok, we *have* a frame! Now we can initialize our VDIF epoch stuff
    struct tm klad;

    ::gmtime_r(&tf.item.frametime.tv_sec, &klad);
    const int epoch    = (klad.tm_year + 1900 - 2000)/2 + (klad.tm_mon>=6);

    // Now set the zero point of that epoch, 00h00m00s on the 1st day of
    // month 0 (Jan) or 6 (July)
    klad.tm_hour  = 0;
    klad.tm_min   = 0; 
    klad.tm_sec   = 0;
    klad.tm_mon   = (klad.tm_mon/6)*6;
    klad.tm_mday  = 1;
    const time_t    tm_epoch = ::mktime(&klad);

    DEBUG(3, "reframe_to_vdif: epoch=" << epoch << ", tm_epoch=" << tm_epoch << endl);

    // Let's pre-create 16 headers for tags 0 .. 15
    // ASSUME we only have 1 channel of 2bits/sample in each incoming
    // frame (ie log2nchan == 0)
    for(unsigned int t=0; t<16; t++) {
        vdif_header&  hdr( tagheader[t] );

        hdr.station_id      = reframe->station_id;
        hdr.thread_id       = (short unsigned int)(t & 0x3ff);
        hdr.data_frame_len8 = (unsigned int)((dataframe_length/8) & 0x00ffffff);
        hdr.bits_per_sample = (unsigned char)(1 & 0x1f);
        hdr.ref_epoch       = (unsigned char)(epoch & 0x3f);
    }

    // By having waited for the first frame for setting up our timing,
    // our fast inner loop can be way cleanur!
    do {
        const unsigned int    last = tf.item.framedata.iov_len;
        unsigned char const*  data = (unsigned char const*)tf.item.framedata.iov_base;
        const struct timespec time = tf.item.frametime;
#if 0
    for(unsigned int i=1; dataframe_length==0 && i<input_size; i++) {
        const unsigned int dfl = input_size/i;
        if( dfl%8==0 && dfl<(output_size-sizeof(vdif_header)) )
            dataframe_length = dfl;
    }
    if( dataframe_length==0 ) {
        DEBUG(-1, "reframe_to_vdif: failed to break dataframe up insize=" << last << endl);
        continue;
    }
#endif
        if( last!=input_size ) {
            DEBUG(-1, "reframe_to_vdif: got inputsize " << last << ", expected " << input_size << endl);
            continue;
        }
        // break up the frame into smaller bits?
        vdif_header&      hdr      = tagheader[tf.tag];

        // dataframes cannot span second boundaries so this can be done
        // easily outside the breaking-up loop
        hdr.epoch_seconds   = (unsigned int)((time.tv_sec - tm_epoch) & 0x3fffffff);
        hdr.data_frame_len8 = (unsigned int)((dataframe_length/8) & 0x00ffffff);

//        DEBUG(3, "reframe_to_vdif: new frame VDIF[" << tf.tag << "]:" << hdr.epoch_seconds << endl <<
//                 "                 " << time.tv_sec << "s " << time.tv_nsec << "ns" << endl);
        for(unsigned int dfn=time.tv_nsec/chunk_duration_ns, pos=0; !stop && pos<last; dfn++, pos+=dataframe_length) {
            block          b( pool->get() );
            unsigned char* bdata( (unsigned char*)b.iov_base );

            hdr.data_frame_num = (unsigned int)(dfn & 0x00ffffff);
//            DEBUG(3, "       chunk: dataframe_number " << dfn << " [" << pos << "]" << endl);
            // copy vdif header and data
            ::memcpy(bdata, &hdr, sizeof(vdif_header));
            ::memcpy(bdata + sizeof(vdif_header), data+pos, dataframe_length);

            stop = (outq->push(tagged<block>(tf.tag, b))==false);
        }
        done++;
    } while( !stop && inq->pop(tf) );
    DEBUG(1, "reframe_to_vdif: done " << done << " frames " << endl);
}



#if 0
template <typename ItemType, typename SyncType = void>
struct state_type {
    ::pthread_mutex_t    mtx;
    ::pthread_cond_t     cond;
    bqueue<ItemType>*    actual_q_ptr;
    inq_type<ItemType>*  iq_ptr;
    outq_type<ItemType>* oq_ptr;
    sync_type<SyncType>* st_ptr;

    state_type( unsigned int qd ) :
        actual_q_ptr( new bqueue<ItemType>(qd) ),
        iq_ptr( new inq_type<ItemType>(actual_q_ptr) ),
        oq_ptr( new outq_type<ItemType>(actual_q_ptr) ),
        st_ptr( new sync_type<SyncType>(&cond, &mtx) ) {
            st_ptr->userdata = new SyncType();
            PTHREAD_CALL( ::pthread_mutex_init(&mtx, 0) );
            PTHREAD_CALL( ::pthread_cond_init(&cond, 0) );
        }
    ~state_type() {
        delete st_ptr->userdata;
        delete st_ptr;
        delete iq_ptr;
        delete oq_ptr;
        delete actual_q_ptr;
        PTHREAD_CALL( ::pthread_cond_destroy(&cond) );
        PTHREAD_CALL( ::pthread_mutex_destroy(&mtx) );
    }
};

template <typename ItemType>
void* netwriterwrapper(void* state_ptr) {
    state_type<ItemType, fdreaderargs>*  dst_state = (state_type<ItemType, fdreaderargs>*)state_ptr;
    DEBUG(0, "netwriterwrapper[fd=" << dst_state->st_ptr->userdata->fd << "]" << endl);
    install_zig_for_this_thread(SIGUSR1);
    try {
        ::netwriter(dst_state->iq_ptr, dst_state->st_ptr);
    }
    catch( const std::exception& e ) {
        DEBUG(0, "netwriterwrapper: netwriter threw up - " << e.what() << endl);
    }
    catch( ... ) {
        DEBUG(0, "netwriterwrapper: netwriter threw unknown exception" << endl);
    }
    // If we're done, disable our queue such that upchain
    // get's informed that WE aren't lissning anymore
    dst_state->actual_q_ptr->disable();
    return (void*)0;
}

template <typename ItemType>
void multinetwriter( inq_type<tagged<ItemType> >* inq, sync_type<multifdargs>* args ) {
    typedef state_type<ItemType, fdreaderargs> dst_state_type;
    typedef std::map<int, dst_state_type*>          fd_state_map_type;
    typedef std::map<unsigned int, dst_state_type*> tag_state_map_type;

    ItemType                tb;
    const std::string       proto( args->userdata->rteptr->netparms.get_protocol() );
    fd_state_map_type       fd_state_map;
    tag_state_map_type      tag_state_map;
    const dest_fd_map_type& dst_fd_map( args->userdata->dstfdmap );

    // Make sure there are filedescriptors to write to
    ASSERT2_COND(dst_fd_map.size()>0, SCINFO("There are no destinations to send to"));

    DEBUG(2, "multinetwriter starting" << endl);

    // So, for each destination in the destination-to-filedescriptor mapping
    // we check if we already have a netwriterthread for that
    // filedescriptor. If not, add one.
    for( dest_fd_map_type::const_iterator cd=dst_fd_map.begin();
         cd!=dst_fd_map.end();
         cd++ ) {
            // Check if we have the fd for the current destination
            // (cd->second) already in our map
            //
            // In the end we must add an entry of current destination's tag
            // (cd->first) to the actual netwriter-state ('dst_state_type*')
            // so the code can put the data-to-send in the appropriate
            // queuek
            //
            // we have "tag -> fd"  (dstfdmap)
            // we must have "tag -> dst_stat_type*"   (tag_state_map)
            // with the constraint
            // "fd -> dst_stat_type*" == unique       (fd_state_map)

            // Look for "fd -> dst_stat_type*" for the current fd
            fd_state_map_type::iterator   fdstate = fd_state_map.find( cd->second );

            if( fdstate==fd_state_map.end() ) {
                // Bollox. Wasn't there yet! We must create a state + thread
                // for the current destination
                pair<fd_state_map_type::iterator, bool>    insres;

                insres = fd_state_map.insert( make_pair(cd->second, new dst_state_type(10)) );
                ASSERT2_COND(insres.second, SCINFO("Failed to insert fd->dst_state_type* entry into map"));

                // insres->first is 'pointer to fd_state_map iterator'
                // ie a pair<filedescriptor, dst_state_type*>. We need to
                // 'clobber' ("fill in") the details of the freshly inserted
                // dst_state_type
                dst_state_type*  stateptr = insres.first->second;

                // fill in the synctype (which is a "fdreaderargs")
                // We have all the necessary info here (eg the 'fd')
                stateptr->st_ptr->userdata->fd       = cd->second;
                stateptr->st_ptr->userdata->rteptr   = args->userdata->rteptr;
                stateptr->st_ptr->userdata->doaccept = (proto=="tcp");
                stateptr->st_ptr->setqdepth(10);
                stateptr->st_ptr->setstepid(args->stepid);

                // allocate a threadid
                stateptr->st_ptr->userdata->threadid = new pthread_t();
                // enable the queue
                stateptr->actual_q_ptr->enable();

                // Add the freshly constructed 'fdreaderargs' entry to the list-of-fdreaders so
                // 'multicloser()' can do its thing, when needed.
                // The 'fdreaderargs' is the "user_data" for the
                // fdreader/writer threadfunctions
                args->userdata->fdreaders.push_back( stateptr->st_ptr->userdata );

                // and start the thread
                PTHREAD_CALL( ::pthread_create(stateptr->st_ptr->userdata->threadid, 0, &netwriterwrapper<ItemType>, (void*)stateptr) );

                // Now the pointer to the entry is filled in and it can
                // double as if it was the searchresult in the first place,
                // as if the entry had existed
                fdstate = insres.first;
            }

            // We know that fdstat points at a pair <fd, dst_state_type*>
            // All we have to do is create an entry <tag, dst_state_type*>
            ASSERT_COND( tag_state_map.insert(make_pair(cd->first, fdstate->second)).second );
    }

    // We have now spawned a number of threads: one per destination
    // We pop everything from our inputqueue
    while( inq->pop(tb) ) {
        // for each tagged block find out where it has to go
        tag_state_map_type::const_iterator curdest = tag_state_map.find(tb.tag);

        // Unconfigured destination gets ignored silently
        if( curdest==tag_state_map.end() ) 
            continue;

        // Configured destination that we fail to send to = AAARGH!
        if( curdest->second->oq_ptr->push(tb.item)==false ) {
            DEBUG(-1, "multinetwriter: tag " << tb.tag << " failed to push block to it!" << endl);
            break;
        }
    }
    DEBUG(4, "multinetwriter closing down. Cancelling & disabling Qs" << endl);
    for( fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            DEBUG(4, "  fd[" << cd->first << "] cancel sync_type");
            // set cancel to true & condition signal
            cd->second->st_ptr->lock();
            cd->second->st_ptr->setcancel(true);
            PTHREAD_CALL( ::pthread_cond_broadcast(&cd->second->cond) );
            cd->second->st_ptr->unlock();

            DEBUG(4, "  disable queue");
            // disable queue
            cd->second->actual_q_ptr->delayed_disable();
            DEBUG(4, endl);
    }
    DEBUG(4, "multinetwriter: joining & cleaning up" << endl);
    for( fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            // now join
            DEBUG(4, "  fd[" << cd->first << "] join thread ");
            PTHREAD_CALL( ::pthread_join(*cd->second->st_ptr->userdata->threadid, 0) );

            // delete resources
            DEBUG(4, "  delete resources");
            delete cd->second;
            DEBUG(4, endl);
    }
    DEBUG(2, "multinetwriter: done" << endl);
}

#endif



// For each distinctive tag we keep a queue and a synctype
struct dst_state_type {
    ::pthread_mutex_t        mtx;
    ::pthread_cond_t         cond;
    bqueue<block>*           actual_q_ptr;
    inq_type<block>*         iq_ptr;
    outq_type<block>*        oq_ptr;
    sync_type<fdreaderargs>* st_ptr;

    dst_state_type( unsigned int qd ) :
        actual_q_ptr( new bqueue<block>(qd) ),
        iq_ptr( new inq_type<block>(actual_q_ptr) ),
        oq_ptr( new outq_type<block>(actual_q_ptr) ),
        st_ptr( new sync_type<fdreaderargs>(&cond, &mtx) ) {
            st_ptr->userdata = new fdreaderargs();
            PTHREAD_CALL( ::pthread_mutex_init(&mtx, 0) );
            PTHREAD_CALL( ::pthread_cond_init(&cond, 0) );
        }
    ~dst_state_type() {
        delete st_ptr->userdata;
        delete st_ptr;
        delete iq_ptr;
        delete oq_ptr;
        delete actual_q_ptr;
        PTHREAD_CALL( ::pthread_cond_destroy(&cond) );
        PTHREAD_CALL( ::pthread_mutex_destroy(&mtx) );
    }
};


void* netwriterwrapper(void* dst_state_ptr) {
    dst_state_type*  dst_state = (dst_state_type*)dst_state_ptr;
    DEBUG(0, "netwriterwrapper[fd=" << dst_state->st_ptr->userdata->fd << "]" << endl);
    install_zig_for_this_thread(SIGUSR1);
    try {
        ::netwriter(dst_state->iq_ptr, dst_state->st_ptr);
    }
    catch( const std::exception& e ) {
        DEBUG(0, "netwriterwrapper: netwriter threw up - " << e.what() << endl);
    }
    catch( ... ) {
        DEBUG(0, "netwriterwrapper: netwriter threw unknown exception" << endl);
    }
    // If we're done, disable our queue such that upchain
    // get's informed that WE aren't lissning anymore
    dst_state->actual_q_ptr->disable();
    return (void*)0;
}

//void multinetwriter( inq_type<tagged_block>* inq, sync_type<multifdargs>* args ) {
void multinetwriter( inq_type<tagged<block> >* inq, sync_type<multifdargs>* args ) {
    typedef std::map<int, dst_state_type*>          fd_state_map_type;
    typedef std::map<unsigned int, dst_state_type*> tag_state_map_type;

    tagged<block>           tb;
    const std::string       proto( args->userdata->rteptr->netparms.get_protocol() );
    fd_state_map_type       fd_state_map;
    tag_state_map_type      tag_state_map;
    const dest_fd_map_type& dst_fd_map( args->userdata->dstfdmap );

    // Make sure there are filedescriptors to write to
    ASSERT2_COND(dst_fd_map.size()>0, SCINFO("There are no destinations to send to"));

    DEBUG(2, "multinetwriter starting" << endl);

    // So, for each destination in the destination-to-filedescriptor mapping
    // we check if we already have a netwriterthread for that
    // filedescriptor. If not, add one.
    for( dest_fd_map_type::const_iterator cd=dst_fd_map.begin();
         cd!=dst_fd_map.end();
         cd++ ) {
            // Check if we have the fd for the current destination
            // (cd->second) already in our map
            //
            // In the end we must add an entry of current destination's tag
            // (cd->first) to the actual netwriter-state ('dst_state_type*')
            // so the code can put the data-to-send in the appropriate
            // queuek
            //
            // we have "tag -> fd"  (dstfdmap)
            // we must have "tag -> dst_stat_type*"   (tag_state_map)
            // with the constraint
            // "fd -> dst_stat_type*" == unique       (fd_state_map)

            // Look for "fd -> dst_stat_type*" for the current fd
            fd_state_map_type::iterator   fdstate = fd_state_map.find( cd->second );

            if( fdstate==fd_state_map.end() ) {
                // Bollox. Wasn't there yet! We must create a state + thread
                // for the current destination
                pair<fd_state_map_type::iterator, bool>    insres;

                insres = fd_state_map.insert( make_pair(cd->second, new dst_state_type(10)) );
                ASSERT2_COND(insres.second, SCINFO("Failed to insert fd->dst_state_type* entry into map"));

                // insres->first is 'pointer to fd_state_map iterator'
                // ie a pair<filedescriptor, dst_state_type*>. We need to
                // 'clobber' ("fill in") the details of the freshly inserted
                // dst_state_type
                dst_state_type*  stateptr = insres.first->second;

                // fill in the synctype (which is a "fdreaderargs")
                // We have all the necessary info here (eg the 'fd')
                stateptr->st_ptr->userdata->fd       = cd->second;
                stateptr->st_ptr->userdata->rteptr   = args->userdata->rteptr;
                stateptr->st_ptr->userdata->doaccept = false /* (proto=="rtcp")  - would require support in multiopener as well! */;
                stateptr->st_ptr->setqdepth(10);
                stateptr->st_ptr->setstepid(args->stepid);

                // allocate a threadid
                stateptr->st_ptr->userdata->threadid = new pthread_t();
                // enable the queue
                stateptr->actual_q_ptr->enable();

                // Add the freshly constructed 'fdreaderargs' entry to the list-of-fdreaders so
                // 'multicloser()' can do its thing, when needed.
                // The 'fdreaderargs' is the "user_data" for the
                // fdreader/writer threadfunctions
                args->userdata->fdreaders.push_back( stateptr->st_ptr->userdata );

                // and start the thread
                PTHREAD_CALL( ::pthread_create(stateptr->st_ptr->userdata->threadid, 0, &netwriterwrapper, (void*)stateptr) );

                // Now the pointer to the entry is filled in and it can
                // double as if it was the searchresult in the first place,
                // as if the entry had existed
                fdstate = insres.first;
            }

            // We know that fdstat points at a pair <fd, dst_state_type*>
            // All we have to do is create an entry <tag, dst_state_type*>
            ASSERT_COND( tag_state_map.insert(make_pair(cd->first, fdstate->second)).second );
    }

    // We have now spawned a number of threads: one per destination
    // We pop everything from our inputqueue
    while( inq->pop(tb) ) {
        // for each tagged block find out where it has to go
        tag_state_map_type::const_iterator curdest = tag_state_map.find(tb.tag);

        // Unconfigured destination gets ignored silently
        if( curdest==tag_state_map.end() )
            continue;

        // Configured destination that we fail to send to = AAARGH!
        if( curdest->second->oq_ptr->push(tb.item)==false ) {
            DEBUG(-1, "multinetwriter: tag " << tb.tag << " failed to push block to it!" << endl);
            break;
        }
    }
    DEBUG(4, "multinetwriter closing down. Cancelling & disabling Qs" << endl);
    for( fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            DEBUG(4, "  fd[" << cd->first << "] cancel sync_type");
            // set cancel to true & condition signal
            cd->second->st_ptr->lock();
            cd->second->st_ptr->setcancel(true);
            PTHREAD_CALL( ::pthread_cond_broadcast(&cd->second->cond) );
            cd->second->st_ptr->unlock();

            DEBUG(4, "  disable queue");
            // disable queue
            cd->second->actual_q_ptr->delayed_disable();
            DEBUG(4, endl);
    }
    DEBUG(4, "multinetwriter: joining & cleaning up" << endl);
    for( fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            // now join
            DEBUG(4, "  fd[" << cd->first << "] join thread ");
            PTHREAD_CALL( ::pthread_join(*cd->second->st_ptr->userdata->threadid, 0) );

            // delete resources
            DEBUG(4, "  delete resources");
            delete cd->second;
            DEBUG(4, endl);
    }
    DEBUG(2, "multinetwriter: done" << endl);
}

/// File writer

void* fdwriterwrapper(void* dst_state_ptr) {
    dst_state_type*  dst_state = (dst_state_type*)dst_state_ptr;
    DEBUG(0, "fdwriterwrapper[fd=" << dst_state->st_ptr->userdata->fd << "]" << endl);
    install_zig_for_this_thread(SIGUSR1);
    try {
        ::fdwriter(dst_state->iq_ptr, dst_state->st_ptr);
    }
    catch( const std::exception& e ) {
        DEBUG(0, "fdwriterwrapper: netwriter threw up - " << e.what() << endl);
    }
    catch( ... ) {
        DEBUG(0, "fdwriterwrapper: netwriter threw unknown exception" << endl);
    }
    // If we're done, disable our queue such that upchain
    // get's informed that WE aren't lissning anymore
    dst_state->actual_q_ptr->disable();
    return (void*)0;
}

void multifilewriter( inq_type<tagged<block> >* inq, sync_type<multifdargs>* args ) {
    typedef std::map<int, dst_state_type*>          fd_state_map_type;
    typedef std::map<unsigned int, dst_state_type*> tag_state_map_type;

    tagged<block>           tb;
    const std::string       proto( args->userdata->rteptr->netparms.get_protocol() );
    fd_state_map_type       fd_state_map;
    tag_state_map_type      tag_state_map;
    const dest_fd_map_type& dst_fd_map( args->userdata->dstfdmap );

    // Make sure there are filedescriptors to write to
    ASSERT2_COND(dst_fd_map.size()>0, SCINFO("There are no destinations to send to"));

    DEBUG(2, "multifilewriter starting" << endl);

    // So, for each destination in the destination-to-filedescriptor mapping
    // we check if we already have a netwriterthread for that
    // filedescriptor. If not, add one.
    for( dest_fd_map_type::const_iterator cd=dst_fd_map.begin();
         cd!=dst_fd_map.end();
         cd++ ) {
            // Check if we have the fd for the current destination
            // (cd->second) already in our map
            //
            // In the end we must add an entry of current destination's tag
            // (cd->first) to the actual netwriter-state ('dst_state_type*')
            // so the code can put the data-to-send in the appropriate
            // queuek
            //
            // we have "tag -> fd"  (dstfdmap)
            // we must have "tag -> dst_stat_type*"   (tag_state_map)
            // with the constraint
            // "fd -> dst_stat_type*" == unique       (fd_state_map)

            // Look for "fd -> dst_stat_type*" for the current fd
            fd_state_map_type::iterator   fdstate = fd_state_map.find( cd->second );

            if( fdstate==fd_state_map.end() ) {
                // Bollox. Wasn't there yet! We must create a state + thread
                // for the current destination
                pair<fd_state_map_type::iterator, bool>    insres;

                insres = fd_state_map.insert( make_pair(cd->second, new dst_state_type(10)) );
                ASSERT2_COND(insres.second, SCINFO("Failed to insert fd->dst_state_type* entry into map"));

                // insres->first is 'pointer to fd_state_map iterator'
                // ie a pair<filedescriptor, dst_state_type*>. We need to
                // 'clobber' ("fill in") the details of the freshly inserted
                // dst_state_type
                dst_state_type*  stateptr = insres.first->second;

                // fill in the synctype (which is a "fdreaderargs")
                // We have all the necessary info here (eg the 'fd')
                stateptr->st_ptr->userdata->fd       = cd->second;
                stateptr->st_ptr->userdata->rteptr   = args->userdata->rteptr;
                stateptr->st_ptr->userdata->doaccept = false /* (proto=="rtcp")  - would require support in multiopener as well! */;
                stateptr->st_ptr->setqdepth(10);
                stateptr->st_ptr->setstepid(args->stepid);

                // allocate a threadid
                stateptr->st_ptr->userdata->threadid = new pthread_t();
                // enable the queue
                stateptr->actual_q_ptr->enable();

                // Add the freshly constructed 'fdreaderargs' entry to the list-of-fdreaders so
                // 'multicloser()' can do its thing, when needed.
                // The 'fdreaderargs' is the "user_data" for the
                // fdreader/writer threadfunctions
                args->userdata->fdreaders.push_back( stateptr->st_ptr->userdata );

                // and start the thread
                PTHREAD_CALL( ::pthread_create(stateptr->st_ptr->userdata->threadid, 0, &fdwriterwrapper, (void*)stateptr) );

                // Now the pointer to the entry is filled in and it can
                // double as if it was the searchresult in the first place,
                // as if the entry had existed
                fdstate = insres.first;
            }

            // We know that fdstat points at a pair <fd, dst_state_type*>
            // All we have to do is create an entry <tag, dst_state_type*>
            ASSERT_COND( tag_state_map.insert(make_pair(cd->first, fdstate->second)).second );
    }

    // We have now spawned a number of threads: one per destination
    // We pop everything from our inputqueue
    while( inq->pop(tb) ) {
        // for each tagged block find out where it has to go
        tag_state_map_type::const_iterator curdest = tag_state_map.find(tb.tag);

        // Unconfigured destination gets ignored silently
        if( curdest==tag_state_map.end() )
            continue;

        // Configured destination that we fail to send to = AAARGH!
        if( curdest->second->oq_ptr->push(tb.item)==false ) {
            DEBUG(-1, "multifilewriter: tag " << tb.tag << " failed to push block to it!" << endl);
            break;
        }
    }
    DEBUG(4, "multifilewriter closing down. Cancelling & disabling Qs" << endl);
    for( fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            DEBUG(4, "  fd[" << cd->first << "] cancel sync_type");
            // set cancel to true & condition signal
            cd->second->st_ptr->lock();
            cd->second->st_ptr->setcancel(true);
            PTHREAD_CALL( ::pthread_cond_broadcast(&cd->second->cond) );
            cd->second->st_ptr->unlock();

            DEBUG(4, "  disable queue");
            // disable queue
            cd->second->actual_q_ptr->delayed_disable();
            DEBUG(4, endl);
    }
    DEBUG(4, "multifilewriter: joining & cleaning up" << endl);
    for( fd_state_map_type::const_iterator cd=fd_state_map.begin();
         cd!=fd_state_map.end();
         cd++ ) {
            // now join
            DEBUG(4, "  fd[" << cd->first << "] join thread ");
            PTHREAD_CALL( ::pthread_join(*cd->second->st_ptr->userdata->threadid, 0) );

            // delete resources
            DEBUG(4, "  delete resources");
            delete cd->second;
            DEBUG(4, endl);
    }
    DEBUG(2, "multifilewriter: done" << endl);
}

void multicloser( multifdargs* mfd ) {
    fdreaderlist_type::iterator curfd;

    for( curfd=mfd->fdreaders.begin();
         curfd!=mfd->fdreaders.end();
         curfd++ )
           ::close_filedescriptor( *curfd ); 
}
