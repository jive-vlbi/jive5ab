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
#include <circular_buffer.h>
#include <sciprint.h>
#include <boyer_moore.h>

#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm> // for std::min 

#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>
#include <stdlib.h> // for ::llabs()
#include <limits.h>


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

void fillpatterngenerator(outq_type<block>* outq, sync_type<fillpatargs>* args) {
    bool               stop;
    runtime*           rteptr;
    fillpatargs*       fpargs = args->userdata;
    unsigned int       bidx;
    unsigned int       wordcount;

    // Assert we do have a runtime pointer!
    ASSERT2_COND(rteptr = fpargs->rteptr, SCINFO("OH NOES! No runtime pointer!"));

    const unsigned int bs = fpargs->rteptr->sizes[constraints::blocksize];
    const unsigned int nb = 2*args->qdepth+1;
    const unsigned int nfill_per_block = (bs/sizeof(fpargs->fill));

    // do the allocation outside the lock. if new() decides to throw whilst
    // we have the lock ... that's no good
    unsigned char*    buf = new unsigned char[nb*bs];

    // wait for the "GO" signal
    args->lock();

    // transfer the pointer to the args object
    fpargs->buffer = buf;
    buf            = 0;
    while( !args->cancelled && !fpargs->run )
        args->cond_wait();
    // whilst we have the lock, do copy important values across
    stop = args->cancelled;
    const unsigned int nword = args->userdata->nword;
    args->unlock();

    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "Fill", 0));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    if( stop ) {
        DEBUG(0, "fillpatterngenerator: cancelled before starting" << endl);
        return;
    }
    RTEEXEC(*rteptr, rteptr->transfersubmode.clr(wait_flag).set(run_flag));

    DEBUG(0, "fillpatterngenerator: starting [buf="
             << format("%p", fpargs->buffer) << " + "<< sciprintd(nb*bs,"byte") << "]" << endl);
    bidx      = 0;
    wordcount = nword;
    while( wordcount>=nfill_per_block ) {
        uint64_t* bptr = (uint64_t*)(fpargs->buffer + bidx*bs);
        for(unsigned int i=0; i<nfill_per_block; i++)
            bptr[i] = fpargs->fill;
        if( outq->push(block((void*)bptr, bs))==false )
            break;
        bidx          = (bidx+1)%nb;
        wordcount    -= nfill_per_block;
        fpargs->fill += fpargs->inc;

        // update global statistics
        counter += bs;
    }
    DEBUG(0, "fillpatterngenerator: done." << endl);
    DEBUG(0, "   req:" << nword << ", leftover:" << wordcount
             << " (blocksize:" << nfill_per_block << "words/" << bs << "bytes)." << endl);
    return;
}

void framepatterngenerator(outq_type<block>* outq, sync_type<fillpatargs>* args) {
    bool               stop;
    runtime*           rteptr;
    uint64_t           framecount = 0;
    fillpatargs*       fpargs = args->userdata;
    unsigned int       bidx;
    unsigned int       wordcount;

    // Assert we do have a runtime pointer!
    ASSERT2_COND(rteptr = fpargs->rteptr, SCINFO("OH NOES! No runtime pointer!"));

    // construct a headersearchtype. it must not evaluate to 'false' when
    // casting to bool
    const headersearch_type header(rteptr->trackformat(), rteptr->ntrack());
    ASSERT2_COND(header, SCINFO("Request to generate frames of fillpattern of unknown format"));

    // Now we can safely compute these 
    const unsigned int      bs = fpargs->rteptr->sizes[constraints::blocksize];
    const unsigned int      nb = 2*args->qdepth+1;
    // Assume: payload of a dataframe follows after the header, which
    // containst the syncword which starts at some offset into to frame
    const unsigned int      n_ull_p_frame = header.framesize / sizeof(uint64_t);
    const unsigned int      n_ull_p_block = bs / sizeof(uint64_t);

    // Allocate room for the blocks and allocate space for a frame at the
    // end of that.
    SYNCEXEC(args,
             fpargs->buffer = new unsigned char[ nb*bs + header.framesize ]);

    // Pointers we use
    unsigned char* const    buffer   = fpargs->buffer;
    unsigned char* const    frame    = buffer + nb*bs;
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
    const unsigned int nword = args->userdata->nword;
    args->unlock();

    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    if( stop ) {
        DEBUG(0, "framepatterngenerator: cancelled before starting" << endl);
        return;
    }
    RTEEXEC(*rteptr, rteptr->transfersubmode.clr(wait_flag).set(run_flag));

    DEBUG(0, "framepatterngenerator: start generating " << nword << " words, formatted as " << header << " frames" << endl)
    bidx      = 0;
    frameptr  = frame;
    wordcount = nword;
    while( wordcount>n_ull_p_block ) {
        // produce a new block's worth of frames
        unsigned char*        bptr  = buffer + bidx*bs;
        unsigned char* const  bsptr = bptr;
        unsigned char* const  beptr = bptr + bs;

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
            const unsigned int byte_needed = (beptr-bptr);
            const unsigned int byte_avail  = (frameend-frameptr);
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
        if( outq->push(block((void*)bsptr, bs))==false )
            break;

        bidx       = (bidx+1)%nb;
        wordcount -= n_ull_p_block;

        // update global statistics
        counter += bs;
    }
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
    // allocate bufferspace.
    // Use twice the queuedepth as seen from here. Each step downstream
    // needs at least one element in their queue so the queuedepth is
    // always larger than the number of steps.
    //   n steps => (n-1) queues => at least (n-1) elements are downstream
    //   each step can "own" two blocks (max) 1 from the pop, the other for
    //   the push
    //   n steps => queue depth should be 2n elements min
    //   2(n-1) > 2n   n>0
    //   2n - 2 > 2n  always false however queues typically have >1 element
    //                so we should be safe
    const DWORDLONG    hiwater = (512*1024*1024)/2;
    const unsigned int num_unsignedlongs = 256000; 
    const unsigned int emergency_bytes = (num_unsignedlongs * sizeof(READTYPE));


    // automatic variables
    bool               stop;
    runtime*           rteptr;
    SSHANDLE           sshandle;
    READTYPE*          emergency_block = 0;
    READTYPE*          ptr;
    DWORDLONG          fifolen;
    unsigned int       idx;
    fiforeaderargs*    ffargs = args->userdata;

    rteptr = ffargs->rteptr;
    // better be a tad safe than sorry
    ASSERT_NZERO( rteptr );

    RTEEXEC(*rteptr, rteptr->sizes.validate());


    const unsigned int blocksize = ffargs->rteptr->sizes[constraints::blocksize];
    const unsigned int nblock    = 2*args->qdepth;

    // allocate enough working space.
    // we include an emergency blob of size num_unsigned
    // long ints at the end. the read loop implements a circular buffer
    // of nblock entries of size blocksize so it will never use/overwrite
    // any bytes of the emergency block.
    ffargs->buffer = new unsigned char[(nblock * blocksize) + emergency_bytes];

    // For emptying the fifo if downstream isn't fast enough
    emergency_block = (READTYPE*)(&ffargs->buffer[nblock*blocksize]);

    // indicate we're doing disk2mem
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "Fifo", 0));

    // Provide unconditional and unlocked access to A counter,
    // which, if everything goes to plan, might even be THE counter
    counter_type& counter( rteptr->statistics.counter(args->stepid) );

    // Can fill in parts of the readdesc that will not change
    // across invocations.
    // NOTE: WE RELY ON THE SYSTEM TO ENSURE THAT BLOCKSIZE IS
    //       A MULTIPLE OF 8!
    // Note: we get (at least) one more block than what will
    //       fit in the queue; the blocks that *are* in the queue
    //       we should NOT mess with; we don't know if they're
    //       being accessed or not

    // Wait for 'start' or possibly a cancelled 
    args->lock();
    while( !ffargs->run && !args->cancelled )
        args->cond_wait();

    // Ah. At least one of the conditions was met.
    // Copy shared state variable whilst be still have the mutex.
    // Only have to get 'cancelled' since if it's 'true' the value of
    // run is insignificant and if it's 'false' then run MUST be
    // true [see while() condition...]
    stop     = args->cancelled;
    args->unlock();

    if( stop ) {
        DEBUG(0, "fiforeader: cancelled before start" << endl);
        return;
    }

    RTEEXEC(*rteptr,
            rteptr->transfersubmode.clr(wait_flag).set(run_flag);
            sshandle = rteptr->xlrdev.sshandle());

    // Let's sleep for, say, 100u sec 
    // we don't care if we do not fully sleep the requested amount
    DEBUG(0, "fiforeader: starting" << endl);

    // Now, enter main thread loop.
    idx    = 0;
    while( !stop ) {
        unsigned long  nread = 0;

        // read a bit'o data into memry
        ptr = (READTYPE*)(ffargs->buffer + idx * blocksize);

        // Make sure the FIFO is not too full
        // Use a (relatively) large block for this so it will work
        // regardless of the network settings
        do_xlr_lock();
//        std::cerr << "::XLRGetFIFOLength(sshandle)" << std::endl;
        while( (fifolen=::XLRGetFIFOLength(sshandle))>hiwater ) {
            // Note: do not use "XLR_CALL*" macros since we've 
            //       manually locked access to the streamstor.
            //       Invoking an XLR_CALL macro would make us 
            //       deadlock since those macros will first
            //       try to lock access ...
            //       so only direct ::XLR* API calls in here!
//            std::cerr << "::XLRReadFifo(sshandle, emergency_block, emergency_bytes)" << std::endl;
            if( ::XLRReadFifo(sshandle, emergency_block, emergency_bytes, 0)!=XLR_SUCCESS ) {
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
        } else {
            // ok, enough data available. Read and stick in queue
            XLRCALL( ::XLRReadFifo(sshandle, ptr, blocksize, 0) );

            // and push it on da queue. push() always succeeds [will block
            // until it *can* push] UNLESS the queue was disabled before or
            // whilst waiting for the queue to become push()-able.
            if( outq->push(block((unsigned char*)ptr, blocksize))==false )
                break;

            // indicate we've read another 'blocksize' amount of
            // bytes into mem
            nread = blocksize;

            // and move on to next block
            idx = (idx+1)%nblock;            
        }
        counter += nread;
        stop     = args->cancelled;
    }
    DEBUG(0, "fiforeader: stopping" << endl);
}

// read straight from disk
void diskreader(outq_type<block>* outq, sync_type<diskreaderargs>* args) {
    bool               stop = false;
    runtime*           rteptr;
    SSHANDLE           sshandle;
    S_READDESC         readdesc;
    playpointer        cur_pp( 0 );
    unsigned int       idx = 0;
    diskreaderargs*    disk = args->userdata;
    const unsigned int nblock = args->qdepth*2;

    rteptr = disk->rteptr;
    // make rilly sure the values in the constrained sizes set make sense.
    // an exception is thrown if not all of the constraints imposed are met.
    RTEEXEC(*rteptr, rteptr->sizes.validate());

    // note: the d'tor of "diskreaderargs" takes care of delete[]'ing buffer -
    //       this makes sure that the memory is available until after all
    //       threads have finished, ie it is not deleted before no-one
    //       references it anymore.
    readdesc.XferLength = rteptr->sizes[constraints::blocksize];
    disk->buffer        = new unsigned char[nblock*readdesc.XferLength];

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
        // Read a block from disk into position idx
        readdesc.AddrHi     = cur_pp.AddrHi;
        readdesc.AddrLo     = cur_pp.AddrLo;
        readdesc.BufferAddr = (READTYPE*)(disk->buffer + idx*readdesc.XferLength);

        XLRCALL( ::XLRRead(sshandle, &readdesc) );

        // If we fail to push it onto our output queue that's a hint for
        // us to call it a day
        if( outq->push(block(readdesc.BufferAddr, readdesc.XferLength))==false )
            break;

        // weehee. Done a block. Update our local loop variables
        cur_pp += readdesc.XferLength;
        idx     = (idx+1)%nblock;

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
    unsigned char                dummybuf[ 65536 ]; // max size of a datagram 2^16 bytes
    const unsigned int           readahead = 4;
    const unsigned int           nblock    = args->qdepth*2+(unsigned int)readahead + 1;
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];

    // Cache ANYTHING that is known & constant.
    // If a value MUST be constant, then MAKE IT SO.
    const unsigned int           n_dg_p_block  = blocksize/wr_size;
    const unsigned int           n_dg_p_buf    = n_dg_p_block * nblock;
    const unsigned int           n_ull_p_dg    = wr_size/sizeof(uint64_t);
    const unsigned int           n_ull_p_rd    = rd_size/sizeof(uint64_t);

    // Get sufficient bufferspace. Alloc one more block. We'll use the
    // unknown block at the end to fill it with fillpattern so we just
    // memcpy() it to a fresh block if needed
    SYNCEXEC(args,
             network->buffer = new unsigned char[(nblock+1) * blocksize];);

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

    // Blockpointers and their fillpattern status and the
    // datagrampositionflags (wether or not a datagramposition
    // is written to or not)
    bool*                   first = new bool[ nblock ];
    bool*                   dgflag = new bool[ n_dg_p_buf ];
    unsigned char* const    buffer  = network->buffer;
    unsigned char* const    end     = buffer + nblock*blocksize;
    uint64_t* const         fpblock = (uint64_t*)end;
    const uint64_t          fillpat = ((uint64_t)0x11223344 << 32) + 0x11223344;

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
    for(uint64_t* dgptr=fpblock; dgptr<fpblock+(n_dg_p_block * n_ull_p_dg); dgptr += n_ull_p_dg) {
        unsigned int ull = 0;
        // fillpattern up until the size of the datagram we read from the network
        for( ; ull<n_ull_p_rd; ull++ )
            dgptr[ull] = fillpat;
        // the rest (if any) is zeroes
        for( ; ull<n_ull_p_dg; ull++ )
            dgptr[ull] = 0;
    }

    // Initialize the blocks with the template block and indicate
    // it's NOT the first write into the block.
    // After a block has been sent off downstream, then
    // the flag will be reset to true such that, when
    // a datagram is to be written to that block, then
    // it is re-initialized with fillpattern
    for(unsigned char* p=buffer, idx=0; p<end; p+=blocksize, idx++) {
        ::memcpy(p, fpblock, blocksize);
        first[(unsigned int)idx] = false;
    }

    // mark all datagram positions as 'free'
    for( unsigned int i=0; i<n_dg_p_buf; i++ )
        dgflag[i] = false;

    // reset statistics/chain and statistics/evlbi
    RTE3EXEC(*rteptr,
             rteptr->evlbi_stats = evlbi_stats_type();
             rteptr->statistics.init(args->stepid, "UdpsReadv3"),
             delete [] first; delete [] dgflag);

    // Great. We're done setting up. Now let's see if we weren't cancelled
    // by any chance
    SYNC3EXEC(args, stop = args->cancelled,
              delete [] first; delete [] dgflag );

    if( stop ) {
        delete [] first;
        delete [] dgflag;
        DEBUG(0, "udpsreader: cancelled before actual start" << endl);
        return;
    }

    // No, we weren't. Now go into our mainloop!
    DEBUG(0, "udpsreader: fd=" << network->fd << " data:" << iov[1].iov_len
             << " total:" << waitallread << " #datagrambufs:" << n_dg_p_buf
             << " #blocks: " << nblock << endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type     delta;
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    counter_type&    dsum( rteptr->evlbi_stats.deltasum );
    ucounter_type&   ooosum( rteptr->evlbi_stats.ooosum );
    ucounter_type&   pktcnt( rteptr->evlbi_stats.pkt_total );
    ucounter_type&   ooocnt( rteptr->evlbi_stats.pkt_ooo );
    ucounter_type&   rptcnt( rteptr->evlbi_stats.pkt_rpt );
    ucounter_type&   disccnt( rteptr->evlbi_stats.pkt_disc );

    // inner loop variables
    bool                    discard;
    unsigned int            dgpos;
    unsigned int            n_dg_disc;
    unsigned int            curblock;
    unsigned int            lastblock;
    unsigned int            old_lastblock;
    unsigned int            new_lastblock;

    // Our loop can be much cleaner if we wait here to receive the 
    // very first datagram. Once we have one, our sequencenumber ->
    // bufferposition is fixed and it makes our lives wonderfull.
    // By using an assert we'll leave the thread before falling
    // into a while loop in case of cancellation.
    // Start reading at position 0 in the buffer
    msg.msg_iovlen  = nwaitall;
    iov[1].iov_base = buffer;
    ASSERT_COND( ::recvmsg(network->fd, &msg, MSG_WAITALL)==waitallread );

    // Having received the first datagram we
    // can initialize our initial state
    lastblock    = 0;
    dgflag[0]    = true;
    pktcnt       = 1;
    dsum         = 0;
    firstseqnr   = seqnr;
    expectseqnr  = firstseqnr+1;

    DEBUG(0, "udps_reader: first sequencenr# " << firstseqnr << endl);

    // Drop into our tight inner loop
    do {
        // Wait for another pakkit to come in. 
        // When it does, take a peak at the sequencenr
        msg.msg_iovlen = npeek;
        if( ::recvmsg(network->fd, &msg, MSG_PEEK)!=peekread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            oss << "::recvmsg(network->fd, &msg, MSG_PEEK) fails - " << lse;
            throw syscallexception(oss.str());
        }

        // Great! A packet came it. Now ... before we actually read it
        // completely, we must decide where it should go. Only if
        // the packes seems to overwrite a location we discard it
        //
        //       (**) "dgpos" can always be _used_ w/o problems because
        //            it is truncated to a valid index into our datagram
        //            array(s). Wether it's _appropriate_ to do so may
        //            be subject to discussion.
        //            See the other (**) note below
        //
        // It is very important to realize that the datagramflag for the
        // just received packet is left unmodified for as long as possible.
        // The reason being that its status is an indicator of whether the
        // packet was discarded or not.
        //   * if the packet-we-are-going-to-read is about to overwrite an
        //     already filled position (dgflag[dgpos]==true) we're going to
        //     discard it
        //   * if the datagramslot is empty (dgflag[dgpos]==false) we accept
        //     the datagram
        dgpos     = (seqnr-firstseqnr)%n_dg_p_buf;
        curblock  = dgpos/n_dg_p_block;
        delta     = (int64_t)(expectseqnr - seqnr);
        dsum     += delta;
        ooosum   += ::llabs(delta);
        discard   = dgflag[dgpos];
        if( discard )
            rptcnt++;
        if( delta )
            ooocnt++;

        // *IF* the pakkit is about to be written to a block we've not
        // written into yet, we must fill the block with fillpattern
        if( first[curblock] ) {
            ::memcpy(buffer + curblock*blocksize, fpblock, blocksize);
            first[curblock] = false;
        }
        // Our primary computations have been done and, what's most
        // important, a location for the packet has been decided upon
        // Read the pakkit into our mem'ry space before we do anything else
        msg.msg_iovlen  = nwaitall;
        iov[1].iov_base = (discard?(&dummybuf[0]):(buffer + dgpos*wr_size));
        if( ::recvmsg(network->fd, &msg, MSG_WAITALL)!=waitallread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - " << lse;
            throw syscallexception(oss.str());
        }

        // Update statistics and change our expectation
        pktcnt++;
        counter       += waitallread;
        dgflag[dgpos]  = true;
        expectseqnr    = seqnr+1;

        // If the circular distance>readahead, release a block,
        // otherwise go back to readin' pakkits
        if( CIRCDIST(lastblock, curblock, nblock)<=readahead )
            continue;

        // If the actual push fails, we know we should cancel
        // so we break out of the loop in that case
        if( outq->push(block(buffer+lastblock*blocksize, blocksize))==false )
            break;

        // Compute, based on the block we've just written to (curblock)
        // what the *actual* new lastblock should be. Ideally it should
        // be (lastblock+1) [modulo circular buffersize!] but major dataloss
        // or other unfavourable things might happen with the traffic
        // [restarted transfer => potentially hugely different sequencenr
        // sequence started] causing this to be not true.
        // What we do is to always move the new lastblock to be the current
        // block - readahead. Any blocks skipped over will be made available
        // for reuse and their data content dropped
        old_lastblock = lastblock;
        new_lastblock = ((curblock>=readahead)?(curblock-readahead):(nblock-readahead+curblock));

        // Loop over all blocks from lastblock -> actual_lastblock:
        //   * for blocks that we skip over, count the 
        //     datagrampositions that ARE filled in: this data will be
        //     DISCARDED (2)
        //   * all datagrampositions in these blocks must be cleared -
        //     they can be (re)written to (3)
        //   * all the blocks must be set to 're-initialize when
        //     first written to next (4)
        n_dg_disc = 0;
        while( CIRCDIST(lastblock, new_lastblock, nblock)>0 ) {
            for(unsigned int i=lastblock*n_dg_p_block, j=0; j<n_dg_p_block; ++i, ++j) {
                if( lastblock!=old_lastblock && dgflag[i] )
                    n_dg_disc++; // (2)
                dgflag[ i ] = false; // (3)
            }
            first[lastblock] = true; // (4)
            lastblock = CIRCNEXT(lastblock, nblock);
        }
        disccnt   += n_dg_disc;
        lastblock  = new_lastblock;
    } while( 1 );
    DEBUG(0, "udpsreader: stopping" << endl);
}

// This threadfunction *JUST* reads UDPs packets:
// A payload preceded by a 64bit sequence number
void udps_pktreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    runtime*                     rteptr;
    fdreaderargs*                network = args->userdata;
 
    // These must all NOT be null
    ASSERT_COND(args && network && network->rteptr); 
    rteptr = network->rteptr; 

    // set up infrastructure for accepting only SIGUSR1
    install_zig_for_this_thread(SIGUSR1);

    // Before diving in too deep  ...
    // this asserts that all sizes make sense and meet certain constraints.
    // Reset statistics/chain and statistics/evlbi
    RTEEXEC(*rteptr,
            rteptr->sizes.validate();
            rteptr->transfersubmode.clr( wait_flag ).set( connected_flag ).set( run_flag );
            rteptr->evlbi_stats = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdpsPktRead"));

    // an (optionally compressed) block of <blocksize> is chopped up in
    // chunks of <read_size>, optionally compressed into <write_size> and
    // then put on the network.
    // We reverse this by reading <write_size> from the network into blocks
    // of size <read_size>
    // [note: no compression => write_size==read_size, ie this scheme will always work]
    const unsigned int           app       = rteptr->sizes[constraints::application_overhead];
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           npkt      = 128;
    // we should be safe for datagrams up to 2G i hope
    // (app, rd_size == unsigned; systemcalls expect int)
    const unsigned int           pkt_size  = app + rd_size;

    // get some bufferspace and register our threadid so we can
    // get cancelled when we're inna blocking read on 'fd'
    // if the network (if 'fd' refers to network that is) is to be closed
    // and we don't know about it because we're in a blocking syscall.
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).
    SYNCEXEC(args,
             args->userdata->threadid = new pthread_t(::pthread_self());
             args->userdata->buffer   = new unsigned char[npkt * pkt_size];
            );

    // Now we can build up the message structure
    // Make sure that one recvmsg reads at least 4kB
    struct iovec                 iov[1];
    struct msghdr                msg;
    // set up the message - a lot of these fields have known & constant values
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;
    // Only whole packets 
    msg.msg_iov        = &iov[0];
    msg.msg_iovlen     = 1;
    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // The size of the parts of the message is known 
    iov[0].iov_len = (int)pkt_size;


    // Initialize starting values
    counter_type&           cntr( rteptr->statistics.counter(args->stepid) );
    ucounter_type&          total( rteptr->evlbi_stats.pkt_total );
    unsigned char*          location;
    unsigned char* const    start    = args->userdata->buffer;
    unsigned char* const    end      = start + (npkt*pkt_size);

    // now go into our mainloop
    DEBUG(0, "udps_pktreader: fd=" << network->fd
              << " app:" << app << " + data:" << rd_size << "=" << pkt_size 
              << " npkt:" << npkt
              << endl);

    location = start;
    do {
        // tell the O/S where the packet should go
        iov[0].iov_base = (void*)location;

        // Attempt to read a datagram
        if( ::recvmsg(network->fd, &msg, MSG_WAITALL)!=(int)(pkt_size) )
            throw syscallexception("recvmsg fails");

        // update counters without locking ...
        total++;
        cntr += pkt_size;

        // compute location of next packet
        location = (((location+=pkt_size)>=end)?start:location);
    } while( outq->push(block(iov[0])) );
    DEBUG(0, "udps_pktreader: stopping" << endl);
}

// Reorderer version 2
//
// Input:  UDPs packets (8byte seq nr + payload)
// Output: blocks of size constraints[blocksize]
void udpspacket_reorderer(inq_type<block>* inq, outq_type<block>* outq, sync_type<reorderargs>* args) {
    uint64_t       firstseqnr  = 0;
    uint64_t       expectseqnr = 0;
    reorderargs*   reorder = args->userdata;
    runtime*       rteptr = reorder->rteptr;
  
    // Before diving in too deep  ...
    // this asserts that all sizes make sense and meet certain constraints
    RTEEXEC(*rteptr,
            rteptr->sizes.validate();
            rteptr->statistics.init(args->stepid, "UdpsReorderv2"));

    // an (optionally compressed) block of <blocksize> is chopped up in
    // chunks of <read_size>, optionally compressed into <write_size> and
    // then put on the network.
    // We reverse this by reading <write_size> from the network into blocks
    // of size <read_size> and fill up a block of size <blocksize> before
    // handing it down the processing chain.
    // [note: no compression => write_size==read_size, ie this scheme will always work]
    const unsigned int           readahead = 3;
    const constraintset_type&    sizes     = rteptr->sizes;
    const unsigned int           nblock    = args->qdepth*2 + readahead + 1;
    const unsigned int           rd_size   = sizes[constraints::read_size];
    const unsigned int           wr_size   = sizes[constraints::write_size];
    const unsigned int           expect    = 8 + wr_size;
    const unsigned int           blocksize = sizes[constraints::blocksize];
    const unsigned long int      fp        = 0x11223344;
    const uint64_t               fill      = (((uint64_t)fp << 32) + fp);

    // Cache ANYTHING that is known & constant.
    // If a value MUST be constant, then MAKE IT SO.
    const unsigned int           n_dg_p_block  = blocksize/rd_size;
    const unsigned int           n_dg_p_buf    = n_dg_p_block * nblock;
    const unsigned int           n_ull_p_block = blocksize/sizeof(uint64_t);

    // Alloc bufferspaces
    // Do the allocs with the lock held. The SYNCEXEC makes sure
    // that upon throwage, the lock will be first released before
    // the exception is re-thrown
    SYNCEXEC(args,
             reorder->buffer = new unsigned char[nblock * blocksize];
             reorder->dgflag = new bool[ n_dg_p_buf ];
             reorder->first  = new bool[ nblock ];
            );

    // We have do a bit of bookkeeping.
    // * per block: is this the first time we 
    //   write a datagram into it? If yes,
    //   we init it with fillpattern.
    // * We keep an array of datagramflags.
    //   Our buffer can be viewed as an array of blocks
    //   and also as an array of datagrams (each block
    //   is made out of an integral amount of datagrams)
    // We also keep track of which sequencenumber maps 
    // to the first datagram in our buffer.
    // Then, it can be computed, based on incoming
    // sequencenumber, where the datagram should go 
    // in the buffer. If that position was already
    // taken we can detect that by looking at the
    // datagramflag.
    bool*                   first  = reorder->first;
    bool*                   dgflag = reorder->dgflag;
    block                   b;
    unsigned int            lastblock;

    // Initialize the blocks with fillpattern and indicate
    // it's NOT the first write into the block.
    // After a block has been sent off downstream, *then*
    // the flag will be reset to true such that, when
    // a datagram is to be written to that block, *then*
    // it is re-initialized with fillpattern
    for( unsigned int i=0; i<nblock; ++i ) {
        uint64_t* ullptr = (uint64_t*)(reorder->buffer + i * blocksize); 
        for(unsigned int j=0; j<n_ull_p_block; j++)
            ullptr[j] = fill;
        first[i] = false;
    }
    for( unsigned int i=0; i<n_dg_p_buf; i++ )
        dgflag[i] = false;

    // now go into our mainloop
    DEBUG(0, "udps_pktreorderer: " << nblock << " blocks = " << n_dg_p_buf << " dg @" << expect << " byte" << endl);

    // Explicitly wait for the first packet to come in. This will allow
    // us to initialize and make the tight loop slightly more efficient
    // since we don't have to test for "have we initialized yet?" at
    // each and every packet.
    if( inq->pop(b)==false || b.iov_len<sizeof(uint64_t) ) {
        DEBUG(0, "udps_pktreorderer: cancelled before actual start, or" << endl <<
                 "                   first packet=" << b.iov_len << " bytes, expected " << expect << endl);
        return;
    }

    // keep those out of the tight inner loop
    uint64_t          snr, diff;
    unsigned int      dgpos, curblock, dist;
    counter_type&     counter( rteptr->statistics.counter(args->stepid) );

    // Act as if the first packet had been received by the inner loop
    dgflag[0]   = true;
    firstseqnr  = *((uint64_t*)b.iov_base);
    expectseqnr = firstseqnr+1;
    lastblock   = 0;

    DEBUG(0, "udps_pktreorderer: first sequence# " << firstseqnr << endl);
    // Now drop into our tight main loop
    do {
        if( inq->pop(b)==false )
            break;
        if( b.iov_len!=expect ) {
            DEBUG(0, "udps_reorderer: got block of wrong size (" << b.iov_len << " in stead of " << expect << ")");
            break;
        }
        snr   = *((uint64_t*)b.iov_base);
        diff  = (snr-firstseqnr);
        dgpos = diff % n_dg_p_buf;

        rteptr->evlbi_stats.deltasum += (int64_t)(expectseqnr - snr) /*((int64_t)expectseqnr-(int64_t)snr)*/;
        expectseqnr = snr+1;

        // overwrite => drop on the floor
        if( dgflag[dgpos] ) {
            rteptr->evlbi_stats.pkt_rpt++;
            continue;
        }
        // not dropped:
        //   -> which block does this pakkit go into?
        //   -> update our expectation
        curblock    = dgpos/n_dg_p_block;

        // *IF* the pakkit is about to be written to a block we've not
        // written into yet, we must fill the block with fillpattern
        if( first[curblock] ) {
            uint64_t* ullptr = (uint64_t*)(reorder->buffer + curblock * blocksize); 
            for(unsigned int i=0; i<n_ull_p_block; ++i)
                ullptr[i] = fill;
            first[ curblock ] = false;
        }

        // copy the packet to its real destination
        ::memcpy(reorder->buffer + dgpos*rd_size, (unsigned char*)b.iov_base+8, wr_size); 

        dgflag[dgpos] = true;

        // If we're not far enough ahead, do nothing
        if( (dist=CIRCDIST(lastblock, curblock, nblock))<=readahead )
            continue;

        // Now we can (try to) push the block at 'lastblock'
        // push only fails when the queue is 'cancelled' (disabled)
        if( outq->push(block(reorder->buffer+lastblock*blocksize, blocksize))==false )
            break;

        // released another block
        counter += blocksize;

        unsigned int n_dg_lost = 0;
        unsigned int n_dg_disc = 0;
        unsigned int prev_lastblock   = lastblock;
        unsigned int actual_lastblock = ((curblock>=readahead)?(curblock-readahead):(nblock-readahead+curblock));

        // Loop over all blocks from lastblock -> actual_lastblock:
        //   * for the block we released count all datagrampositions
        //     that were NOT filled in:
        //     these packets are presumed LOST (1)
        //   * for any other blocks that we skip over, count the 
        //     datagrampositions that ARE filled in: this data will be
        //     DISCARDED (2)
        //   * all datagrampositions in these blocks must be cleared -
        //     they can be (re)written to (3)
        //   * all the blocks must be set to 're-initialize when
        //     first written to next (4)
        while( CIRCDIST(lastblock, actual_lastblock, nblock)>0 ) {
            const bool  count_set   = (lastblock!=prev_lastblock);

            // Loop over the datagramflags for this block
            //   branching = expensive so we just always add to
            //   both n_dg_lost and n_dg_disc only sometimes we add
            //   zeroes ...
            //   (that would be:
            //      if(lastblock==prevlastblock) n_dg_lost += !dgflag[i];
            //      else                         n_dg_disc +=  dgflag[i];
            for(unsigned int i=lastblock*n_dg_p_block, j=0; j<n_dg_p_block; ++i, ++j) {
                n_dg_lost += ((!(count_set || dgflag[i]))?1:0); // (1) (!count_set && !dgflag[i]) == DeMorgan
                n_dg_disc +=  ((count_set && dgflag[i])?1:0);   // (2)
                dgflag[ i ] = false;                            // (3)
            }
            first[ lastblock ] = true;                          // (4)

            // Move on the next block
            lastblock = CIRCNEXT(lastblock, nblock);
        }
        rteptr->evlbi_stats.pkt_lost += n_dg_lost;
        rteptr->evlbi_stats.pkt_disc += n_dg_disc;

        // Move lastblock to exactly <readahead> blocks before curblock.
        // If everything is going according to plan this should be
        // exactly 1 block further than the previous block we released.
        lastblock = actual_lastblock;
    } while( 1 );
    DEBUG(0, "udpsreordererv2: stopping." << endl);
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
    unsigned int           idx = 0;
    fdreaderargs*          network = args->userdata;
    const unsigned int     nblock = args->qdepth*2;

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
    unsigned char*       tmpbuf = new unsigned char[nblock*bl_size];

    args->lock();
    stop                       = args->cancelled;
    network->buffer            = tmpbuf;
    args->unlock();

    if( stop ) {
        DEBUG(0, "socketreader: stop signalled before we actually started" << endl);
        return;
    }
    DEBUG(0, "socketreader: read fd=" << network->fd << " rd:" << rd_size
             << " wr:" << wr_size <<  " bs:" << bl_size << endl);
    bytesread = 0;
    while( !stop ) {
        int                  r;
        unsigned char*       ptr = network->buffer+(idx*bl_size);
        const unsigned char* eptr = (ptr + bl_size);

        // set everything to 0
        ::memset(ptr, 0x00, bl_size);

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
        if( outq->push(block(network->buffer+(idx*bl_size), bl_size))==false )
            break;
        // update loopvariables
        idx = (idx+1)%nblock;
    }
    DEBUG(0, "socketreader: stopping. read " << bytesread << " (" <<
             byteprint(bytesread,"byte") << ")" << endl);
}

// read from filedescriptor
void fdreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    bool                   stop;
    runtime*               rteptr;
    unsigned int           idx = 0;
    fdreaderargs*          file = args->userdata;

    rteptr = file->rteptr;
    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int     nblock    = args->qdepth*2;
    const unsigned int     blocksize = rteptr->sizes[constraints::blocksize];

    // do the malloc/new outside the critical section. operator new()
    // may throw. if that happens whilst we hold the lock we get
    // a deadlock. we no like.
    unsigned char* tmpbuf = new unsigned char[nblock*blocksize];

    args->lock();
    stop                 = args->cancelled;
    file->buffer         = tmpbuf;
    //file->threadid       = 0;
    args->unlock();

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
        unsigned char*  ptr = file->buffer+(idx*blocksize);

        // do read data orf the network
        if( ::read(file->fd, ptr, blocksize)!=(int)blocksize ) {
            DEBUG(-1, "fdreader: READ FAILURE - " << ::strerror(errno) << endl);
            break;
        }
        // update statistics counter
        counter += blocksize;

        // push it downstream
        if( outq->push(block(ptr, blocksize))==false )
            break;
        // update loopcounters
        idx = (idx+1)%nblock;
        // and update statistics
    }
    DEBUG(0, "fdreader: done" << endl);
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
        // Attempt to accept. "do_accept_incoming" throws on wonky!
        RTEEXEC(*network->rteptr,
                network->rteptr->transfersubmode.set( wait_flag ));
        DEBUG(0, "netreader: waiting for incoming connection" << endl);
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
            DEBUG(0, "netreader: stopsignal before actual start " << endl);
            return;
        }
        // as we are not stopping yet, inform user whom we've accepted from
        DEBUG(0, "netreader: incoming dataconnection from " << incoming.second << endl);
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
    block               b;
    runtime*            rteptr;
    framerargs*         framer = args->userdata;
    headersearch_type   header         = framer->hdr;
    const unsigned int  nframe         = args->qdepth * 2;
    const unsigned int  seek_start     = header.syncwordoffset + header.syncwordsize;
    const unsigned int  seek_length    = header.framesize - seek_start;
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
             framer->buffer = new unsigned char[nframe * header.framesize];
             stop           = args->cancelled;);

    RTEEXEC(*rteptr, rteptr->statistics.init(args->stepid, "Framer"));
    counter_type&  counter( rteptr->statistics.counter(args->stepid) );

    DEBUG(0, "framer: start looking for " << header << " dataframes" << endl);

    // off we go! 
    while( !stop && inq->pop(b) ) {
        unsigned char*       ptr   = (unsigned char*)b.iov_base;
        unsigned char* const e_ptr = ptr + b.iov_len;

        // (attempt to) process all bytes in the current block.
        while( ptr<e_ptr ) {
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
            unsigned char* const sof  = framer->buffer + (nFrame%nframe * header.framesize);

            // Step 1: copy bytes
            // copy as many databytes as we can or need
            const unsigned int ncpy = min(bytes_to_next, (unsigned int)(e_ptr-ptr));

            ::memcpy(sof + (header.framesize-bytes_to_next), ptr, ncpy);
            ptr           += ncpy;
            bytes_to_next -= ncpy;

            // If, after the copy action, bytes-to-next is NOT zero (yet)
            // we might as well break since it indicates that the block we 
            // just popped from the input queue did not contain enough bytes
            // to fill the current frame. So we MUST go back to our input
            // queue and wait for moar data
            if( bytes_to_next )
                break;

            // Ok, we've filled our prospective framebuffer with enough
            // data. 
            // Now there's two possibilities:
            //  1) we are in sync and the syncword appears *exactly*
            //     where we expect it
            //  2) we are NOT in sync and we should, possibly, scan
            //     the whole framebuffer for said syncword
            //
            // We expect/assume that once we are in sync we'll stay in sync
            // so we optimize for the sync'ed case.
            // To that effect we *just* look at the position where we expect
            // the syncword and test wether it actually *is* there.

            // case 1: short & quick test to see wether the syncword
            //         appears where it should
            unsigned char*             actual_sync_start;
            const unsigned char* const expected_sync_start = sof + header.syncwordoffset;

            // if no immediate match then widen the search to the rest
            // of the frame. we can start the search immediately *after*
            // the syncwordoffset+syncwordsize position on account of even
            // IF we find a match between postion 0 and syncwordoffset
            // (if syncwordoffset != 0) this implies we're missing 
            // one or more of the presyncwordbytes, which condition would
            // trigger a "throw away the whole frame and wait for a new
            // one"-cycle anyway.
            // postcondition is that actual_sync_start either points
            // at a syncword or nowhere at all. then it is a question of
            // what to do next.
            if( (actual_sync_start = (unsigned char*)syncwordsearch(expected_sync_start, header.syncwordsize))==0 )
                actual_sync_start = (unsigned char*)syncwordsearch(sof + seek_start, seek_length);

            // actual_sync_start == 0 means no syncword at all.
            // keep the last (hdr.syncwordsize + hdr.syncwordoffset - 1)
            // bytes; hypothetically, we could be missing just one byte of
            // the syncword.
            if( actual_sync_start==0 ) {
                // Keep potential (optional)presyncbytes plus syncwordbytes.
                // you never know if there was a partial match at the end of
                // the buffer
                const unsigned int num_keep = (header.syncwordoffset+header.syncwordsize-1);
                ::memcpy( (unsigned char*)sof,
                          (unsigned char*)(sof + header.framesize - num_keep),
                          num_keep );
                // update bytes-to-next 
                bytes_to_next = header.framesize - num_keep;
                // we can immediately bail out and continue the next cycle 
                continue;
            }

            // actual_sync_start != expected_sync_start means we found it
            // elsewhere. if it turns out that we seem to miss
            // pre-headerbytes [syncword starts at offset < where we expect
            // it] we have no choice to discard the whole frame and wait
            // for the next (complete) header to appear in our buffer ...
            if( actual_sync_start!=expected_sync_start ) {
                // decide wether to move data and/or discard completely.
                // assume the syncwordoffset can never equal the framesize
                // (something would be terribly wrong if it did)
                ptrdiff_t          actual_offset = actual_sync_start - sof;
                const unsigned int num_keep = (((unsigned int)actual_offset<header.syncwordoffset)?(0):(header.framesize - actual_offset + header.syncwordoffset));

                ::memcpy( (unsigned char*)sof,
                          (unsigned char*)(sof + header.framesize - num_keep),
                          num_keep );

                bytes_to_next = header.framesize - num_keep;
                continue;
            }

            // There's only one option left: the syncword is where it should
            // be!

#if 0
            // For now leave out this check. It is expensive.
            // It checks the syncword (again) AND extracts a track
            // and does a CRC check on that
            if( header.check(sof)==false ) {
                // invalid frame! restart from scratch
                bytes_to_next = header.framesize;
                continue;
            }
#endif
            // Valid frame!
            frame  f(header.frameformat, header.ntrack, block(sof, header.framesize));

            // If we fail to push the new frame on our output queue
            // we break from this loop and set the stopcondition
            if( (stop=(outq->push(f)==false))==true )
                break;
            // update statistics!
            counter += header.framesize;
            // chalk up one more frame.
            nFrame++;
            // re-init search/check
            bytes_to_next = header.framesize;
        }
        // update accounting
        nBytes += (uint64_t)(ptr - (unsigned char*)b.iov_base);
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

void framer_old(inq_type<block>* inq, outq_type<frame>* outq, sync_type<framerargs>* args) {
    bool                stop;
    block               b;
    runtime*            rteptr;
    framerargs*         framer = args->userdata;
    headersearch_type   header         = framer->hdr;
    const unsigned int  nframe         = args->qdepth * 2;
    // Searchvariables must be kept out of mainloop as we may need to
    // aggregate multiple blocks from our inputqueue before we find a 
    // complete frame
    uint64_t            nFrame        = 0;
    uint64_t            nBytes        = 0;
    unsigned int        bytes_to_next = 0;
    circular_buffer     headerbytes( header.headersize );

    rteptr = framer->rteptr;

    // Basic assertions: we require some elements downstream (qdepth)
    // AND we require that the supplied sizes make sense:
    //     framesize  >= headersize [a header fits in a frame]
    ASSERT2_COND( args->qdepth>0, SCINFO("there is no downstream Queue?") );
    ASSERT_COND( header.framesize  >= header.headersize );

    // allocate a buffer for <nframe> frames
    SYNCEXEC(args,
             framer->buffer = new unsigned char[nframe * header.framesize];
             stop           = args->cancelled;);

    RTEEXEC(*rteptr, rteptr->statistics.init(args->stepid, "Framer"));
    counter_type&  counter( rteptr->statistics.counter(args->stepid) );

    DEBUG(0, "framer: start looking for " << header << " dataframes" << endl);

    // off we go! 
    while( !stop && inq->pop(b) ) {
        unsigned char*       ptr   = (unsigned char*)b.iov_base;
        unsigned char* const e_ptr = ptr + b.iov_len;

        // (attempt to) process all bytes in the current block.
        while( ptr<e_ptr ) {
            // keep a pointer to where the current frame should start in memory
            // (ie pointing at the first 'p' or 'S')
            //    * sof  == start-of-frame
            unsigned char* const sof  = framer->buffer + (nFrame%nframe * header.framesize);

            // 'bytes_to_next' != 0 => we're copying the datapart of the frame!
            if( bytes_to_next ) {
                // copy as many databytes as we can or need
                unsigned int ncpy = min(bytes_to_next, (unsigned int)(e_ptr-ptr));

                ::memcpy(sof + (header.framesize-bytes_to_next), ptr, ncpy);
                ptr += ncpy;
                // we could have just finalized a full frame
                if( (bytes_to_next-=ncpy)==0 ) {
                    // w00t. yes. indeed.
                    block data(sof, header.framesize);
                    frame f(header.frameformat, header.ntrack, data);

                    // If we fail to push the new frame on our output queue
                    // we break from this loop and set the stopcondition
                    if( (stop=(outq->push(f)==false))==true )
                        break;
                    // update statistics!
                    counter += header.framesize;
                    // chalk up one more frame.
                    nFrame++;
                    // re-init search/check
                    headerbytes.clear();
                    bytes_to_next = 0;
                }
                // carry on
                continue;
            }
            // we did process datapart. if we end up here we're looking
            // for a (next) header.

            // we MUST have a full header's worth of data before we can actually check
            if( headerbytes.size()<header.headersize ) {
                // copy as many presyncbytes as we can or need
                const unsigned int need = header.headersize - headerbytes.size();
                const unsigned int ncpy = min(need, (unsigned int)(e_ptr-ptr));

                headerbytes.push(ptr, ncpy);

                // do NOT forget to update loop variable
                ptr += ncpy;
                // if we did not find all the headerbytes we must wait for more
                // (the continue will stop the loop as ncpy!=need also implies
                //  ptr==e_ptr, ie the block-processing-loop stops. however
                //  it is a better condition than "ptr==e_ptr" since "need==ncpy" does NOT
                //  imply ptr==e_ptr, but MAY imply that. however, if need==ncpy
                //  we just found a whole header's worth of bytes anyway so we can
                //  just go on checking it w/o waiting for more
                if( ncpy!=need )
                    continue;
            }
            // now that we HAVE a header's worth of bytes we can check
            // if it is a *valid* header
            if( header.check(headerbytes)==false ) {
                // pop a byte from the headerbytesbuffer - restart the
                // search from there
                headerbytes.pop();
                continue;
            }
            // w00t! Found a valid header!
            // DEBUG(4, "framer: Found a " << header.frameformat << " header" << endl);
            // now we can copy the full headerbytes from the circularbuffer 
            // into the current frame and start collecting databytes
            headerbytes.pop(sof, header.headersize);
            // we can do this unconditionally since we've asserted, upon
            // entering, that the framesize is actually larger than the
            // headersize
            bytes_to_next = header.framesize - header.headersize;
        }
        // update accounting
        nBytes += (uint64_t)(ptr - (unsigned char*)b.iov_base);
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
            if( outq->push(block(ptr, wr))==false )
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
        const unsigned char* eptr = ptr + bs;

        // compress rd bytes into wr bytes and send those wr bytes off
        // downstream
        while( (ptr+rd)<=eptr )  {
            // ec = end-of-compress, so ecptr == end-of-compress-pointer
            ecptr = compressor.compress((data_type*)(ptr+co));
            if( (ptrdiff_t)(ecptr - (data_type*)ptr)!=(ptrdiff_t)nw) {
                DEBUG(-1, "blockcompressor: compress yields " << (ecptr-(data_type*)ptr) << " expect " << nw+co);
            }
            if( outq->push(block(ptr, wr))==false )
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
    block              b;
    buffererargs*      buffargs = args->userdata;
    runtime*           rteptr = buffargs->rte;

    // Make sure we *have* a runtime environment
    ASSERT_NZERO( rteptr );

    // Now we can safely set up our buffers.
    // Total # of blocks to alloc = how many requested + qdepth downstream
    //   We round off the number of requested bytes to an integral amount of
    //   blocks.
    const unsigned int bs = rteptr->sizes[constraints::blocksize];
    const unsigned int blockstobuffer = (buffargs->bytestobuffer/bs);
    const unsigned int buffersize = (blockstobuffer + args->qdepth + 1) * bs;
    
    // Update in our argument
    SYNCEXEC(args,
             buffargs->buffer = new circular_buffer( buffersize );
             buffargs->bytestobuffer = blockstobuffer * bs; );

    // Add a statisticscounter
    RTEEXEC(*rteptr,
             rteptr->statistics.init(args->stepid, "Bufferer"));
    counter_type&   counter = rteptr->statistics.counter(args->stepid);

    DEBUG(0, "bufferer: starting, buffering " << byteprint(buffargs->bytestobuffer, "B") << endl);

    while( inq->pop(b) ) {
        // Before o'erwriting in our circularbuffer,
        // check if we need to push downstream
        // (which is if the circular buffers contains
        //  more bytes than our current threshold
        while( buffargs->buffer->size()>buffargs->bytestobuffer )
            if( outq->push( block(buffargs->buffer->pop(bs), bs) )==false )
                break;
        // insert the block we just popped
        buffargs->buffer->push( (const unsigned char*)b.iov_base, b.iov_len );
        counter += bs;
    }
    DEBUG(0, "bufferer: stopping." << endl);
}

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
    ::srandom( (unsigned int)time(0) );
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
             << nbyte << " (" << byteprint(nbyte, "byte") << ")"
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
             << nbyte << " (" << byteprint(nbyte,"byte") << ")"
             << endl);
}

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
             << nbyte << " (" << byteprint(nbyte,"byte") << ")"
             << endl);
}

// Highlevel networkwriter interface. Does the accepting if necessary
// and delegates to either the generic filedescriptorwriter or the udp-smart
// writer, depending on the actual protocol
void netwriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
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
    //network->rteptr->statistics.init(args->stepid, "NetWriter");
    args->unlock();

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
    const headersearch_type hdr(rteptr->trackformat(), rteptr->ntrack());
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
                msg << "framechecker[" << nbyte << "]: BLK" << blockcnt << "[" << i << "] "
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
    const headersearch_type hdr(rteptr->trackformat(), rteptr->ntrack());

    // Data is sent in frames if the framesize is constrained
    if( hdr )
        framechecker(inq, args);
    else
        blockchecker(inq, args);
    return;
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
    rv->doaccept  = (proto=="tcp");
    rv->blocksize = net.rteptr->netparms.get_blocksize();

    // and do our thang
    if( proto=="rtcp" )
        rv->fd = getsok(np.host, np.get_port(), "tcp");
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



// * close the filedescriptor
// * set the value to "-1"
// * if the threadid is not-null, signal the thread so it will
//   fall out of any blocking systemcall
void close_filedescriptor(fdreaderargs* fdreader) {
    ASSERT_COND(fdreader);

    if( fdreader->fd!=-1 ) {
        ASSERT_ZERO( ::close(fdreader->fd) );
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
{}
frame::frame(format_type tp, unsigned int n, block data):
    frametype( tp ), ntrack( n ), framedata( data )
{}



tagged_block::tagged_block():
    tag( (unsigned int)-1 )
{}

tagged_block::tagged_block(unsigned int t, block b):
    blk( b ), tag( t )
{}



framerargs::framerargs(headersearch_type h, runtime* rte) :
    rteptr(rte), buffer(0), hdr(h)
{ ASSERT_NZERO(rteptr); }
framerargs::~framerargs() {
    delete [] buffer;
}

fillpatargs::fillpatargs():
    run( false ), fill( ((uint64_t)0x11223344 << 32) + 0x11223344 ),
    inc( 0 ), rteptr( 0 ), nword( (unsigned int)-1), buffer( 0 )
{}

fillpatargs::fillpatargs(runtime* r):
    run( false ), fill( ((uint64_t)0x11223344 << 32) + 0x11223344 ),
    inc( 0 ), rteptr( r ), nword( (unsigned int)-1), buffer( 0 )
{ ASSERT_NZERO(rteptr); }

void fillpatargs::set_run(bool newval) {
    run = newval;
}
void fillpatargs::set_nword(unsigned int n) {
    nword = n;
}

fillpatargs::~fillpatargs() {
    delete [] buffer;
}


compressorargs::compressorargs(): rteptr(0) {}
compressorargs::compressorargs(runtime* p):
    rteptr(p)
{ ASSERT_NZERO(rteptr); }


fiforeaderargs::fiforeaderargs() :
    run( false ), rteptr( 0 ), buffer( 0 )
{}
fiforeaderargs::fiforeaderargs(runtime* r) :
    run( false ), rteptr( r ), buffer( 0 )
{ ASSERT_NZERO(rteptr); }

void fiforeaderargs::set_run( bool newrunval ) {
    run = newrunval;
}

fiforeaderargs::~fiforeaderargs() {
    delete [] buffer;
}

diskreaderargs::diskreaderargs() :
    run( false ), repeat( false ), rteptr( 0 ), buffer( 0 )
{}
diskreaderargs::diskreaderargs(runtime* r) :
    run( false ), repeat( false ), rteptr( r ), buffer( 0 )
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
    delete [] buffer;
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
    blocksize( 0 ), buffer( 0 )
{}
fdreaderargs::~fdreaderargs() {
    delete [] buffer;
    delete threadid;
}

buffererargs::buffererargs() :
    rte(0), bytestobuffer(0), buffer(0)
{}
buffererargs::buffererargs(runtime* rteptr, unsigned int n) :
    rte(rteptr), bytestobuffer(n), buffer(0)
{ ASSERT_NZERO(rteptr); }

unsigned int buffererargs::get_bufsize( void ) {
    return bytestobuffer;
}

buffererargs::~buffererargs() {
    delete buffer;
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

            ASSERT2_COND( parts.size()<=2, SCINFO("Invalid formatted address " << curchunk->second) );
            if( parts.size()>1 ) {
                long int v = -1;
                
                v = ::strtol(parts[1].c_str(), 0, 0);

                ASSERT2_COND(v>0 && v<=USHRT_MAX, SCINFO("invalid portnumber " << parts[1] ) );
                port = (unsigned short)v;
            }
            insres = destfdmap.insert( make_pair(curchunk->second, ::getsok(parts[0], port, proto)) );
            ASSERT2_COND(insres.second, SCINFO(" Arg! Failed to insert entry into map!"));
            chunkdestfdptr = insres.first;
        }
        // Now add it to our outputmap
        rv->dstfdmap.insert( make_pair(curchunk->first, chunkdestfdptr->second) );
    }
    return rv;
}

// take in frames and spit out tagged blocks -
// the dataframe will be split into chunks (by channel? or just in n equal
// sized pieces) and reframed to be VDIF frames, tagged with a tag based
// on whence the chunk came from (usually: channel)
void splitter( inq_type<frame>* inq, outq_type<tagged_block>* outq ) {
    frame              f;
    unsigned int       d = 0;
    const unsigned int num_dests = 2;
    DEBUG(2, "splitter starting" << endl);
    while( inq->pop(f) ) {
        if( outq->push( tagged_block((d++)%num_dests, f.framedata) )==false )
            break;
    }
    DEBUG(2, "splitter done" << endl);
}

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

void multinetwriter( inq_type<tagged_block>* inq, sync_type<multifdargs>* args ) {
    typedef std::map<unsigned int, dst_state_type*> dst_state_map_type;

    tagged_block            tb;
    const std::string       proto( args->userdata->rteptr->netparms.get_protocol() );
    fdreaderlist_type&      fdreaders( args->userdata->fdreaders );
    dst_state_map_type      dst_state_map;
    const dest_fd_map_type& dst_fd_map( args->userdata->dstfdmap );

    // Make sure there are filedescriptors to write to
    ASSERT2_COND(dst_fd_map.size()>0, SCINFO("There are no destinations to send to"));

    DEBUG(2, "multinetwriter starting" << endl);

    for( dest_fd_map_type::const_iterator cd=dst_fd_map.begin();
         cd!=dst_fd_map.end();
         cd++ ) {
            pair<dst_state_map_type::iterator, bool>  insres;
            insres = dst_state_map.insert( make_pair(cd->first, new dst_state_type(10)) );
            ASSERT2_COND(insres.second, SCINFO("Failed to insert entry into map"));

            dst_state_type*  stateptr = insres.first->second;
            // fill in the synctype (which is a "fdreaderargs")
            stateptr->st_ptr->userdata->fd       = cd->second;
            stateptr->st_ptr->userdata->rteptr   = args->userdata->rteptr;
            stateptr->st_ptr->userdata->doaccept = (proto=="tcp");
            stateptr->st_ptr->setqdepth(10);
            stateptr->st_ptr->setstepid(args->stepid);

            // allocate a threadid
            stateptr->st_ptr->userdata->threadid = new pthread_t();
            // enable the queue
            stateptr->actual_q_ptr->enable();
            // and start the thread
            PTHREAD_CALL( ::pthread_create(stateptr->st_ptr->userdata->threadid, 0, &netwriterwrapper, (void*)stateptr) );
            // Now that the thread is created, add its fdreaderargs* to 
            // our synctype
            fdreaders.push_back( stateptr->st_ptr->userdata );
    }

    // Ok, we have now spawned a number of threads - one for each
    // destination
    while( inq->pop(tb) ) {
        // for each tagged block find out the filedescriptor where it has to
        // go
        dst_state_map_type::const_iterator curdest = dst_state_map.find(tb.tag);

        // Unconfigured destination gets ignored silently
        if( curdest==dst_state_map.end() ) 
            continue;

        // configured destination that we fail to send to:
        // AARGH!
        if( curdest->second->oq_ptr->push(tb.blk)==false ) {
            DEBUG(-1, "multinetwriter: tag " << tb.tag << " failed to push block to it!" << endl);
            break;
        }
    }
    DEBUG(2, "multinetwriter closing down" << endl);
    for( dst_state_map_type::const_iterator cd=dst_state_map.begin();
         cd!=dst_state_map.end();
         cd++ ) {
            DEBUG(4, "  cancel sync_type");
            // set cancel to true & condition signal
            cd->second->st_ptr->lock();
            cd->second->st_ptr->setcancel(true);
            PTHREAD_CALL( ::pthread_cond_broadcast(&cd->second->cond) );
            cd->second->st_ptr->unlock();

            DEBUG(4, "  disable queue");
            // disable queue
            cd->second->actual_q_ptr->delayed_disable();

            // now join
            DEBUG(4, "  join thread ");
            PTHREAD_CALL( ::pthread_join(*cd->second->st_ptr->userdata->threadid, 0) );

            // delete resources
            DEBUG(4, "  delete resources");
            delete cd->second;
            DEBUG(4, endl);
    }
    DEBUG(2, "multinetwriter done" << endl);
}


void multicloser( multifdargs* mfd ) {
    fdreaderlist_type::iterator curfd;

    for( curfd=mfd->fdreaders.begin();
         curfd!=mfd->fdreaders.end();
         curfd++ )
           ::close_filedescriptor( *curfd ); 
}
