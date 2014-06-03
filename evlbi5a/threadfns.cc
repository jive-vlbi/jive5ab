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
#include <ioboard.h>
#include <pthreadcall.h>
#include <playpointer.h>
#include <dosyscall.h>
#include <streamutil.h>
#include <evlbidebug.h>
#include <getsok.h>
#include <getsok_udt.h>
#include <headersearch.h>
#include <busywait.h>
#include <timewrap.h>
#include <stringutil.h>
#include <sciprint.h>
#include <boyer_moore.h>
#include <sse_dechannelizer.h>
#include <hex.h>
#include <libudt5ab/udt.h>
#include <timezooi.h>
#include <threadutil.h> // for install_zig_for_this_thread()

#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm> // for std::min 
#include <memory>    //     std::auto_ptr
#include <queue>
#include <list>

#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>
#include <stdlib.h> // for ::llabs()
#include <limits.h>
#include <stdarg.h>
#include <time.h>   // for ::clock_gettime
#include <unistd.h>


using namespace std;

DEFINE_EZEXCEPT(fakerexception)
DEFINE_EZEXCEPT(itcpexception)

void pvdif(void const* ptr) {
    char                       sid[3];
    struct vdif_header const*  vdh = (struct vdif_header const*)ptr;

    sid[0] = (char)(vdh->station_id & 0xff);
    sid[1] = (char)((vdh->station_id & 0xff00)>>8);  
    sid[2] = '\0';
    DEBUG(-1, "  VDIF legacy:" << vdh->legacy << " thread_id:" << vdh->thread_id <<  
              " station:" << sid << endl);
}


// When dealing with circular buffers these macro's give you the
// wrap-around-safe next and previous index given the current index and the
// size of the circular buffer.
// CIRCDIST: always returns a positive distance between your b(egin) and
// e(nd) point
#define CIRCNEXT(cur, sz)   ((cur+1)%sz)
#define CIRCPREV(cur, sz)   CIRCNEXT((cur+sz-2), sz)
#define CIRCDIST(b , e, sz) (((b>e)?(sz-b+e):(e-b))%sz)


// thread arguments struct(s)

dplay_args::dplay_args():
    rot( 0.0 ), rteptr( 0 )
{}



// delayed play thread function.
// will wait until ROT for argptr->rteptr->current_taskid reaches
// argptr->rot [if >0.0]. If argptr->rot NOT >0.0 it will
// act as an immediate play.
// We assume that we *own* the pointer to dplay_args_ptr and as
// such will call "delete" on it!
void* delayed_play_fn( void* dplay_args_ptr ) {
    dplay_args*  dpaptr = (dplay_args*)dplay_args_ptr;
    if( !dpaptr ) {
        DEBUG(-1, "delayed_play_fn: passed a NULL-pointer as argument?!" << endl);
        return (void*)0;
    }
    if( !dpaptr->rteptr ) {
        delete dpaptr;
        DEBUG(-1, "delayed_play_fn: passed a NULL-pointer for runtime?!" << endl);
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
    double      rot( dpaptr->rot );
    runtime*    rteptr( dpaptr->rteptr );
    playpointer pp_start( dpaptr->pp_start );

    // We can already discard the pointer then
    delete dpaptr;
    dpaptr = 0;

    try {
        pcint::timediff                  tdiff;

        // during the sleep/wait we may be cancellable
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate) );

        if ( rot > 0.0 ) {
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
        }
        
        // now disable cancellability
        PTHREAD_CALL( ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) );
        
        // if we used arming, use PlayTrigger i.o. Playback
        BOOLEAN option_on;
        XLRCALL( ::XLRGetOption(rteptr->xlrdev.sshandle(), SS_OPT_PLAYARM, &option_on) );
        if ( option_on ) {
            XLRCALL( ::XLRPlayTrigger(rteptr->xlrdev.sshandle()) );
        }
        else {
            XLRCALL( ::XLRPlayback(rteptr->xlrdev.sshandle(),
                                   pp_start.AddrHi, pp_start.AddrLo) );
        }

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
    bool              stop;
    runtime*          rteptr;
    uint64_t          framecount = 0;
    uint64_t          wordcount;
    fillpatargs*      fpargs = args->userdata;
    struct ::timespec ts;

    // Assert we do have a runtime pointer!
    ASSERT2_COND(rteptr = fpargs->rteptr, SCINFO("OH NOES! No runtime pointer!"));

    // construct a headersearchtype. it must not evaluate to 'false' when
    // casting to bool
    const headersearch_type header(rteptr->trackformat(), rteptr->ntrack(),
                                   (unsigned int)rteptr->trackbitrate(),
                                   rteptr->vdifframesize());
    ASSERT2_COND(header.valid(), SCINFO("Request to generate frames of fillpattern of unknown format"));

    // Now we can safely compute these 
    const uint64_t     bs = fpargs->rteptr->sizes[constraints::blocksize];
    // Assume: payload of a dataframe follows after the header, which
    // containst the syncword which starts at some offset into to frame
    const uint64_t      n_ull_p_frame    = header.framesize / sizeof(uint64_t);
    const uint64_t      n_ull_p_block    = bs / sizeof(uint64_t);
    const double        trackbitrate = rteptr->trackbitrate();
    const unsigned int  ntrack       = rteptr->ntrack();
    const long int      frameduration_ns = ((ntrack>0 && trackbitrate>0)?
                                            (long int)((((double)header.payloadsize*8)/(ntrack*trackbitrate))*1.0e9):
                                            (long int)500000000);

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

    DEBUG(0, "framepatterngenerator: generating " << nword << " words, formatted as " << header << " frames" << endl <<
             "                       frameduration " << sciprintd((((double)frameduration_ns)/1.0e9), "s") << 
             " realtime " << fpargs->realtime << endl);
    ts         = ts_now();
    ts.tv_nsec = 0;
    frameptr   = frame;
    wordcount  = nword;

    while( wordcount>n_ull_p_block ) {
        // produce a new block's worth of frames
        block                 b     = fpargs->pool->get(); 
        unsigned char*        bptr  = (unsigned char*)b.iov_base;
        unsigned char* const  beptr = bptr + b.iov_len;

        // Keep on copying/generating frames until the block's filled up
        while( bptr<beptr ) {
            // must we generate a new frame?
            if( frameptr==frame ) {
                uint64_t*       ull = (uint64_t*)frameptr;

                // fill whole block with (current) fillpattern
                for( unsigned int i=0; i<n_ull_p_frame; i++ )
                    ull[i] = fpargs->fill;

                // write the syncword at the correct position
                // HV: 3 Jul 2012 - update: that is, IF there is a 
                //     syncword. In VDIF there isn't. 
                //     The ISO C99 standard says that memcpy should
                //     receive valid pointers even when copying 0
                //     zero bytes so we must "cornercase" this one.
                //     Even though for VDIF the syncwordsize==0, the
                //     syncwordptr is NULL and hence we're not allowed
                //     to put it into memcpy
                if( header.syncword && header.syncwordsize ) 
                    ::memcpy( (void*)(frameptr + header.syncwordoffset), (void const*)header.syncword, header.syncwordsize );

                // Stick a timestamp in it
                header.encode_timestamp(frameptr, ts);

                // Update frame variables
                fpargs->fill += fpargs->inc;
                ts.tv_nsec   += frameduration_ns;
                if( ts.tv_nsec>999999999 ) {
                    ts.tv_sec++;
                    ts.tv_nsec = 0;
                    if( fpargs->realtime )
                        ::clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);
                }
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
    typedef emergency_type<256000>  em_block_type;
    // If we must empty the FIFO we must read the data to somewhere so
    // we allocate an emergency block

    // clearing the FIFO at 50% turned out to be too close to the edge
    // (in particular during forking), so start clearing it at +-40% now
    // if the FIFO reaches this level or it jitters from 40% to 50%,
    // we have a problem anyway
    const DWORDLONG    hiwater = (512ull*1024*1024)*4/10;

    // Make sure we're not 'made out of 0-pointers'
    ASSERT_COND( args && args->userdata && args->userdata->rteptr );

    // automatic variables
    bool                     stop;
    runtime*                 rteptr = args->userdata->rteptr;
    SSHANDLE                 sshandle;
    DWORDLONG                fifolen;
    fiforeaderargs*          ffargs = args->userdata;
    auto_ptr<em_block_type>  emergency_block( new em_block_type );

    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int blocksize = ffargs->rteptr->sizes[constraints::blocksize];

    // allocate enough working space.
    // we include an emergency blob of size num_unsigned
    // long ints at the end. the read loop implements a circular buffer
    // of nblock entries of size blocksize so it will never use/overwrite
    // any bytes of the emergency block.
    SYNCEXEC( args,
              ffargs->pool = new blockpool_type(blocksize, 16) );

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
            if( ::XLRReadFifo(sshandle, emergency_block->buf, (emergency_block->nrElements * sizeof(READTYPE)), 0)!=XLR_SUCCESS ) {
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
    // Make the I/O board stop transferring data!
    DEBUG(2, "fiforeader: pausing I/O board transfer " << rteptr->ioboard.hardware() << endl);
    if( rteptr->ioboard.hardware()&ioboard_type::mk5a_flag ) 
        rteptr->ioboard[ mk5areg::notClock ] = 1;
    else if( rteptr->ioboard.hardware()&ioboard_type::dim_flag ) {
        if ( *(rteptr->ioboard[ mk5breg::DIM_STARTSTOP ]) == 1) {
            // only allowed to set pause if the board is still in running state,
            
            // race condition here, the register could be set to 0 right now
            // however, this would typically happen by a user given a
            // "record=off" command, so the chance of that happening while
            // the transfer shuts down on itself (because of an error)
            // is quite unlikely
            rteptr->ioboard[ mk5breg::DIM_PAUSE ] = 1;
        }
    }
    rteptr->transfersubmode.set( pause_flag );

    // clean up
    DEBUG(0, "fiforeader: stopping" << endl);
}

// read straight from disk
void diskreader(outq_type<block>* outq, sync_type<diskreaderargs>* args) {
    bool               stop = false;
    runtime*           rteptr;
    S_READDESC         readdesc;
    playpointer        cur_pp( 0 );
    diskreaderargs*    disk = args->userdata;
    XLRCODE( SSHANDLE  sshandle;)

    rteptr = disk->rteptr;
    // make rilly sure the values in the constrained sizes set make sense.
    // an exception is thrown if not all of the constraints imposed are met.
    RTEEXEC(*rteptr, rteptr->sizes.validate());

    // note: the d'tor of "diskreaderargs" takes care of delete[]'ing buffer -
    //       this makes sure that the memory is available until after all
    //       threads have finished, ie it is not deleted before no-one
    //       references it anymore.
    const unsigned int blocksize = rteptr->sizes[constraints::blocksize];
    readdesc.XferLength = blocksize;
    SYNCEXEC(args,
             disk->pool = new blockpool_type(readdesc.XferLength, 16));

    // Wait for "run" or "cancel".
    args->lock();
    while( !disk->run && !args->cancelled )
        args->cond_wait();
    cur_pp                         = disk->pp_start;
    unsigned int minimum_read_size = disk->allow_variable_block_size ?
        8 : blocksize; // need to read in chunks of 8 byte from StreamStor
    stop                           = args->cancelled || 
        (cur_pp.Addr + minimum_read_size > disk->pp_end.Addr);
    args->unlock();

    if( stop ) {
        DEBUG(0, "diskreader: cancelled before start" << endl);
        return;
    }

    RTEEXEC(*rteptr,
            XLRCODE(sshandle = rteptr->xlrdev.sshandle());
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

        int64_t bytes_left = disk->pp_end - cur_pp;
        if ( bytes_left < (int64_t)readdesc.XferLength ) {
            readdesc.XferLength = disk->pp_end - cur_pp;
            b.iov_len = bytes_left;
        }

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
        minimum_read_size = disk->allow_variable_block_size ? 8 : blocksize;
        // If we didn't receive an explicit stop signal,
        // do check if we need to repeat when we've reached
        // the end of our playable region
        if( !stop && (cur_pp.Addr + minimum_read_size > disk->pp_end.Addr) && (stop=!disk->repeat)==false ) {
            cur_pp = disk->pp_start;
            readdesc.XferLength = blocksize;
        }
        args->unlock();

        // update stats
        counter += b.iov_len;
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
//
//  HV: 27 Apr 2012 
//                 Attempt at doing packet statistics as per RFC4737
//                 We do not implement all metrics from that RFC.
//                 The most important ones are:
//                 counting sequency discontinuities, reorderings (and
//                 determine the reordering extent(*)) and the "gap"
//                 between succesive discontinuities.
//                 Packet loss and discardage are non-RFC but very
//                 relevant to e-VLBI and are separate counters.
//
//                 (*) in order to compute the reordering extent of a
//                 single reordering event you SHOULD remember all the
//                 sequencenumbers. However, that is way too much for us
//                 so we remember the last 100. So if a reordering event
//                 of >100 packets occurs our statistics are off.
void udpsreaderv4(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    bool                      stop;
    time_t                    lastack;
    ssize_t                   r;
    uint64_t                  seqnr;
    uint64_t                  firstseqnr  = 0;
    uint64_t                  expectseqnr = 0;
    runtime*                  rteptr = 0;
    socklen_t                 slen( sizeof(struct sockaddr_in) );
    unsigned int              ack = 0;
    fdreaderargs*             network = args->userdata;
    struct sockaddr_in        sender;
    static string             acks[] = {"xhg", "xybbgmnx",
                                        "xyreryvwre", "tbqireqbzzr",
                                        "obxxryhy", "rvxryovwgre",
                                        "qebrsgbrgre", "" /* leave empty string last!*/};
#if 1
    circular_buffer<uint64_t> psn( 32 ); // keep the last 32 sequence numbers
#endif
    if( network )
        rteptr = network->rteptr; 
    ASSERT_COND( rteptr && network );

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
    // HV: 13-11-2013 Blocks larger than this size we call "insensible" and we 
    //                change the readahead + blockpool allocation scheme
    //                because we might be doing vlbi_streamer style
    const unsigned int           sensible_blocksize( 32*1024*1024 );
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];
    // HV: 13-11-2013 Support for vlbi streamer mode means that blocksize
    //                could be 256MB - 512MB / block. In such cases the
    //                readahead should be 1.
    //                Below we should take care of the blockpool allocation
    //                too - cannot ask for 16 blocks at a time :D
    const unsigned int           readahead = (blocksize>=sensible_blocksize)?2:network->netparms.nblock;

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
    // HV: 13-11-2013 If blocksize seems too large, do not allocate
    //                more than ~2GB/turn [which would be ~4 chunks
    //                for vlbi_streamer mode in 512MB/chunk] or 32 blocks.
    //                Only go for 2GB per allocation if we 
    //                *really* have to!
    const unsigned int  onegig = 1024*1024*1024;
    const unsigned int  twogig = 2*onegig;
    const unsigned int  nb = ((onegig/blocksize)<4?(twogig/blocksize):
                              (blocksize<sensible_blocksize?32:onegig/blocksize));
    SYNCEXEC(args,
             network->pool = new blockpool_type(blocksize, nb));


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
    //
    //  HV: 13-11-2013 Having fillpattern block only makes sense if
    //                 rd_size != wr_size
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
            << " total:" << waitallread << " readahead:" << readahead
            << " pkts:" << n_dg_p_workbuf << endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&   loscnt( rteptr->evlbi_stats.pkt_lost );
    ucounter_type&   pktcnt( rteptr->evlbi_stats.pkt_in );
    ucounter_type&   ooocnt( rteptr->evlbi_stats.pkt_ooo );
    ucounter_type&   disccnt( rteptr->evlbi_stats.pkt_disc );
#if 0
    ucounter_type&   discont( rteptr->evlbi_stats.discont );
    ucounter_type&   discont_sz( rteptr->evlbi_stats.discont_sz );
    ucounter_type&   gapsum( rteptr->evlbi_stats.gap_sum );
#endif
#if 1
    ucounter_type&   ooosum( rteptr->evlbi_stats.ooosum );
#endif
    // inner loop variables
    bool         discard;
    void*        location;
    uint64_t     blockidx;
    uint64_t     maxseq, minseq;
#if 0
    uint64_t     lastdiscontinuity = 0;
#endif
    unsigned int shiftcount;

    // Our loop can be much cleaner if we wait here to receive the 
    // very first sequencenumber. We make that our current
    // first sequencenumber and then we can _finally_ drop
    // into our real readloop
    //
    // HV: 9 Jul 2012 - in order to prevent flooding we will
    //     have to send UDP backtraffic every now and then
    //     (such that network equipment between the scope and
    //     us does not forget our ARP entry). So we peek at the
    //     sequencenumber and at the same time record who's sending
    //     to us
    if( ::recvfrom(network->fd, &seqnr, sizeof(seqnr), MSG_PEEK, (struct sockaddr*)&sender, &slen)!=sizeof(seqnr) ) {
        delete [] dummybuf;
        delete [] workbuf;
        delete [] fpblock;
        DEBUG(-1, "udpsreader: cancelled before beginning" << endl);
        return;
    }
    lastack = 0; // trigger immediate ack send

#ifdef FILA
// FiLa10G only sends 32bits of sequence number
seqnr = (uint64_t)(*((uint32_t*)(((unsigned char*)iov[0].iov_base)+4)));
#endif

    maxseq = minseq = expectseqnr = firstseqnr = seqnr;

    DEBUG(0, "udpsreader: first sequencenr# " << firstseqnr << " from " <<
              inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << endl);


    // Drop into our tight inner loop
    do {
        discard   = (seqnr<firstseqnr);
        location  = (discard?dummybuf:0);

        // Ok, we have read another sequencenumber.
        // First up: some statistics?
        pktcnt++;
#if 1
        // Statistics as per RFC4737. Not all of them,
        // and one or two slighty adapted.
        // In order to do the accounting as per the RFC
        // we should remember all sequence numbers.
        // We could keep, say, the last 100 but a lot of
        // linear searching is required to do the statistics
        // correctly. For now skip that.
        psn.push( seqnr );
#endif
        // Count sequence discontinuity (RFC/3.4) and
        // an approximation of the reordering extent (RFC/4.2.2).
        // The actual definition in 4.2.2 is more complex than
        // what we do but we save a linear search this way.
        // Also sum the gap between discontinuities (4.5.4).
        // The gap is the distance, in units of packets,
        // since the last seen discontinuity.
        if( seqnr>=expectseqnr ) {
#if 0
            if( seqnr>expectseqnr ) {
                // this is a discontinuity
                discont++;
                discont_sz += (seqnr - expectseqnr);
            }
#endif
            // update next expected seqnr
            expectseqnr = seqnr+1;
        } else {
#if 1
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
#else
            ooocnt++;
#endif
#if 0
            // and record the gap 
            gapsum += (pktcnt - lastdiscontinuity);
            // update the packetnumber when we saw the last
            // discontinuity (ie this packet!)
            lastdiscontinuity = pktcnt;
#else
            ooocnt++;
#endif
        }
        if( discard )
            disccnt++;
        if( seqnr>maxseq )
            maxseq = seqnr;
        else if( seqnr<minseq )
            minseq = seqnr;
        loscnt = (maxseq - minseq + 1 - pktcnt);

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
            workbuf[(readahead-1)] = block();

            // Update loopvariables
            firstseqnr += n_dg_p_block;
            if( ++shiftcount==readahead ) {
                DEBUG(0, "udpsreader: detected jump > readahead, " << (seqnr - firstseqnr) << " datagrams" << endl);
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
        if( (r=::recvmsg(network->fd, &msg, MSG_WAITALL))!=(ssize_t)waitallread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            delete [] dummybuf;
            delete [] workbuf;
            delete [] fpblock;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - [" << lse << "] (ask:" << waitallread << " got:" << r << ")";
            throw syscallexception(oss.str());
        }

        // Now that we've *really* read the pakkit we may update our
        // read statistics 
        counter       += waitallread;

        time_t   nowack = ::time(NULL);
        // Send out an ack before we go into infinite wait
        if( (nowack - lastack) > 59 ) {
            if( acks[ack].empty() )
                ack = 0;
            // Only warn if we fail to send. Try again in two minutes
            if( ::sendto(network->fd, acks[ack].c_str(), acks[ack].size(), 0,
                         (const struct sockaddr*)&sender, sizeof(struct sockaddr_in))==-1 )
                DEBUG(-1, "udpsreader: WARN failed to send ACK back to sender" << endl);
            lastack = nowack;
            ack++;
        }

        // Wait for another pakkit to come in. 
        // When it does, take a peak at the sequencenr
        msg.msg_iovlen = npeek;
        if( (r=::recvmsg(network->fd, &msg, MSG_PEEK))!=peekread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            delete [] dummybuf;
            delete [] workbuf;
            delete [] fpblock;
            oss << "::recvmsg(network->fd, &msg, MSG_PEEK) fails - [" << lse << "] (ask:" << peekread << " got:" << r << ")";;
            throw syscallexception(oss.str());
        }
#ifdef FILA
// FiLa10G only sends 32bits of sequence number
seqnr = (uint64_t)(*((uint32_t*)(((unsigned char*)iov[0].iov_base)+4)));
#endif
    } while( true );

    // Clean up
    delete [] dummybuf;
    delete [] workbuf;
    delete [] fpblock;
    DEBUG(0, "udpsreader: stopping" << endl);
    network->finished = true;
}

/////////
///// Two-step UDPs reader. Makes sure that memory is touched only once
////  wether or not a packet is received or not
////
////  The bottom half grabs memory and puts packets in order and sets
////  a flag if it has written a position.
////  The top half goes over the array of flags and writes fill pattern +
////  zeroes in the packet positions that have no data
////
////  On that last subject: if we're doing compressed UDP traffic,
////  we must pre-expand each read packet from "wr_size" => "rd_size",
////  filling up the difference with zeroes.
////
////  This step needs to be done always. The fill pattern now, that is
////  different, that only needs to be done when the packet didn't arrive.
////  
////  Theoretically it would be nicer to do the zeroeing in the
////  "bottom half" but I _really_ want that routine to be as optimal
////  as can be, given the work it _already_ has to do. 
////  Rather, in the "top half". There's two top halves - one with
////  zeroeing and one without. There is some duplicated code but
////  the result is optimal for each case - within the high-performance
////  loop (looping over each packet) not a single decision needs to be
////  made. [in vlbi_streamer mode we have block sizes of 256/512 MByte,
////  i.e. ~30,000 to 60,000 packets and thus an equal amount
////  of decisions can be skipped.
//// 
////////


// The bottom half
void udpsreader_bh(outq_type<block>* outq, sync_type<fdreaderargs*>* args) {
    bool                      stop;
    time_t                    lastack;
    ssize_t                   r;
    uint64_t                  seqnr, seqoff, pktidx;
    uint64_t                  firstseqnr  = 0;
    uint64_t                  expectseqnr = 0;
    runtime*                  rteptr = 0;
    socklen_t                 slen( sizeof(struct sockaddr_in) );
    unsigned int              ack = 0;
    fdreaderargs*             network = *args->userdata;
    struct sockaddr_in        sender;
    static string             acks[] = {"xhg", "xybbgmnx",
                                        "xyreryvwre", "tbqireqbzzr",
                                        "obxxryhy", "rvxryovwgre",
                                        "qebrsgbrgre", "" /* leave empty string last!*/};
    circular_buffer<uint64_t> psn( 32 ); // keep the last 32 sequence numbers

    rteptr = network->rteptr; 

    // an (optionally compressed) block of <blocksize> is chopped up in
    // chunks of <read_size>, optionally compressed into <write_size> and
    // then put on the network.
    // We reverse this by reading <write_size> from the network into blocks
    // of size <read_size> and fill up a block of size <blocksize> before
    // handing it down the processing chain.
    // [note: no compression => write_size==read_size, ie this scheme will always work]
    unsigned char*               dummybuf = new unsigned char[ 65536 ]; // max size of a datagram 2^16 bytes

    // HV: 13-11-2013 Blocks larger than this size we call "insensible" and we 
    //                change the readahead + blockpool allocation scheme
    //                because we might be doing vlbi_streamer style
    const unsigned int           sensible_blocksize( 32*1024*1024 );
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];

    // HV: 13-11-2013 Support for vlbi streamer mode means that blocksize
    //                could be 256MB - 512MB / block. In such cases the
    //                readahead should be 1.
    //                Below we should take care of the blockpool allocation
    //                too - cannot ask for 16 blocks at a time :D
    const unsigned int           readahead = (blocksize>=sensible_blocksize)?2:network->netparms.nblock;

    // We tag the flags at the end of the block, one unsigned char/datagram
    unsigned char                dummyflag;
    unsigned char*               flagptr;
    const unsigned int           n_dg_p_block = blocksize/wr_size;
    
    // We need some temporary blocks:
    //   * an array of blocks, our workbuf. we keep writing packets
    //     in there until we receive a sequencenumber that would 
    //     force us to write outside the readahead buffer. then
    //     we start to release blocks until the packet CAN be
    //     written into the workbuf
    block*                       workbuf = new block[ readahead ];

    // Create a blockpool. If we need blocks we take'm from there
    // HV: 13-11-2013 If blocksize seems too large, do not allocate
    //                more than two (2) chunks in one go
    const unsigned int  nb = (blocksize<sensible_blocksize?32:2);

    // Before doing anything, make sure that *we* are the one being woken
    // if something happens on the file descriptor that we're supposed to
    // be sucking empty!
    //
    // 21-Jan-2014 HV: Note to self ... this whole thread signalling thing
    //                 only works if one actually installs a signal handler
    //                 for the thread one desires to be woken up should it be
    //                 signalled!
    install_zig_for_this_thread(SIGUSR1);

    SYNCEXEC(args,
             delete network->threadid;
             delete network->pool;
             network->threadid = new pthread_t( ::pthread_self() );
             network->pool = new blockpool_type(blocksize + n_dg_p_block*sizeof(unsigned char), nb));

    // If blocksize > sensible block size start to pre-allocate!
    if( blocksize>=sensible_blocksize ) {
        list<block>        bl;
        const unsigned int npre = network->netparms.nblock;
        DEBUG(4, "udpsreader_bh: start pre-allocating " << npre << " blocks" << endl);
        for(unsigned int i=0; i<npre; i++) {
            block tmpb = network->pool->get();
            // we have to actually *do* something with the memory orelse the
            // kernel will give us the memory like "yeah here it is" and not
            // *actually* prepare all the pages!
            ::memset(tmpb.iov_base, 0x0, tmpb.iov_len);
            bl.push_back( tmpb );
        }
        DEBUG(4, "udpreader_bh: ok, done that!" << endl);
    }

    // Set up the message - a lot of these fields have known & constant values
    struct iovec    iov[2];
    struct msghdr   msg;

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

    // reset statistics/chain and statistics/evlbi
    RTE3EXEC(*rteptr,
            rteptr->evlbi_stats = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdpsReadBH"),
            delete [] dummybuf; delete [] workbuf);

    // Great. We're done setting up. Now let's see if we weren't cancelled
    // by any chance
    SYNCEXEC(args, stop = args->cancelled);

    if( stop ) {
        delete [] dummybuf;
        delete [] workbuf;
        DEBUG(0, "udpsreader_bh: cancelled before actual start" << endl);
        return;
    }

    // No, we weren't. Now go into our mainloop!
    DEBUG(0, "udpsreader_bh: fd=" << network->fd << " data:" << iov[1].iov_len
            << " total:" << waitallread << " readahead:" << readahead
            << " pkts:" << n_dg_p_block * readahead << endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&   loscnt( rteptr->evlbi_stats.pkt_lost );
    ucounter_type&   pktcnt( rteptr->evlbi_stats.pkt_in );
    ucounter_type&   ooocnt( rteptr->evlbi_stats.pkt_ooo );
    ucounter_type&   disccnt( rteptr->evlbi_stats.pkt_disc );
    ucounter_type&   ooosum( rteptr->evlbi_stats.ooosum );

    // inner loop variables
    bool         discard;
    void*        location;
    uint64_t     blockidx;
    uint64_t     maxseq, minseq;
    unsigned int shiftcount;

    // Our loop can be much cleaner if we wait here to receive the 
    // very first sequencenumber. We make that our current
    // first sequencenumber and then we can _finally_ drop
    // into our real readloop
    //
    // HV: 9 Jul 2012 - in order to prevent flooding we will
    //     have to send UDP backtraffic every now and then
    //     (such that network equipment between the scope and
    //     us does not forget our ARP entry). So we peek at the
    //     sequencenumber and at the same time record who's sending
    //     to us
    if( ::recvfrom(network->fd, &seqnr, sizeof(seqnr), MSG_PEEK, (struct sockaddr*)&sender, &slen)!=sizeof(seqnr) ) {
        delete [] dummybuf;
        delete [] workbuf;
        DEBUG(-1, "udpsreader_bh: cancelled before beginning" << endl);
        return;
    }
    lastack = 0; // trigger immediate ack send

#ifdef FILA
// FiLa10G only sends 32bits of sequence number
seqnr = (uint64_t)(*((uint32_t*)(((unsigned char*)iov[0].iov_base)+4)));
#endif

    maxseq = minseq = expectseqnr = firstseqnr = seqnr;

    DEBUG(0, "udpsreader_bh: first sequencenr# " << firstseqnr << " from " <<
              inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << endl);

    // Drop into our tight inner loop
    do {
        discard   = (seqnr<firstseqnr);
        location  = (discard?dummybuf:0);
        flagptr   = (discard?&dummyflag:0);

        // Ok, we have read another sequencenumber.
        // First up: some statistics?
        pktcnt++;

        // Statistics as per RFC4737. Not all of them,
        // and one or two slighty adapted.
        // In order to do the accounting as per the RFC
        // we should remember all sequence numbers.
        // We could keep, say, the last 100 but a lot of
        // linear searching is required to do the statistics
        // correctly. For now skip that.
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
            ooocnt++;
        }

        // More statistics ...
        if( discard )
            disccnt++;
        if( seqnr>maxseq )
            maxseq = seqnr;
        else if( seqnr<minseq )
            minseq = seqnr;
        loscnt = (maxseq - minseq + 1 - pktcnt);

        // Now we need to find out where to put the data for it!
        // that is, if the packet is not to be discarded
        // [if location is already set it is the dummybuf, ie discardage]
        // NOTE: blockidx can never become <0 since we've already
        //       checked that seqnr >= firstseqnr
        //       (a condition signalled by location==0 since
        //        location!=0 => seqnr < firstseqnr)
        shiftcount = 0;
        while( location==0 ) {
            seqoff   = seqnr - firstseqnr;
            blockidx = seqoff/n_dg_p_block;

            if( blockidx<readahead ) {
                pktidx = seqoff%n_dg_p_block;

                // ok we know in which block to put our datagram
                // make sure the block is non-empty
                if( workbuf[blockidx].empty() ) {
                    workbuf[blockidx] = network->pool->get();
                    // set all flags to 0 - no pkts in buffer yet
                    ::memset((unsigned char*)workbuf[blockidx].iov_base + blocksize, 0x0, n_dg_p_block);
                }
                // compute location inside block
                location = (unsigned char*)workbuf[blockidx].iov_base + pktidx*wr_size;
                flagptr  = (unsigned char*)workbuf[blockidx].iov_base + blocksize + pktidx;
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
            workbuf[(readahead-1)] = block();

            // Update loopvariables
            firstseqnr += n_dg_p_block;
            if( ++shiftcount==readahead ) {
                DEBUG(0, "udpsreader_bh: detected jump > readahead, " << (seqnr - firstseqnr) << " datagrams" << endl);
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
        if( (r=::recvmsg(network->fd, &msg, MSG_WAITALL))!=(ssize_t)waitallread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            delete [] dummybuf;
            delete [] workbuf;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - [" << lse << "] (ask:" << waitallread << " got:" << r << ")";
            throw syscallexception(oss.str());
        }

        // Now that we've *really* read the pakkit we may update our
        // read statistics 
        *flagptr       = 1;
        counter       += waitallread;

        time_t   nowack = ::time(NULL);
        // Send out an ack before we go into infinite wait
        if( (nowack - lastack) > 59 ) {
            if( acks[ack].empty() )
                ack = 0;
            // Only warn if we fail to send. Try again in two minutes
            if( ::sendto(network->fd, acks[ack].c_str(), acks[ack].size(), 0,
                         (const struct sockaddr*)&sender, sizeof(struct sockaddr_in))==-1 )
                DEBUG(-1, "udpsreader: WARN failed to send ACK back to sender" << endl);
            lastack = nowack;
            ack++;
        }

        // Wait for another pakkit to come in. 
        // When it does, take a peak at the sequencenr
        msg.msg_iovlen = npeek;
        if( (r=::recvmsg(network->fd, &msg, MSG_PEEK))!=peekread ) {
            lastsyserror_type lse;
            ostringstream     oss;
            delete [] dummybuf;
            delete [] workbuf;
            oss << "::recvmsg(network->fd, &msg, MSG_PEEK) fails - [" << lse << "] (ask:" << peekread << " got:" << r << ")";;
            throw syscallexception(oss.str());
        }
#ifdef FILA
// FiLa10G only sends 32bits of sequence number
seqnr = (uint64_t)(*((uint32_t*)(((unsigned char*)iov[0].iov_base)+4)));
#endif
    } while( true );

    // Clean up
    delete [] dummybuf;
    delete [] workbuf;
    DEBUG(0, "udpsreader_bh: stopping" << endl);
    network->finished = true;
}

////////////////////////////////////////////////////
//                The top half
////////////////////////////////////////////////////
struct th_type {
    fdreaderargs*       network;
    outq_type<block>*   outq;

    th_type(fdreaderargs* fdr, outq_type<block>* oq):
        network( fdr ), outq( oq )
    {}
};

// In this top half there will be no zeroes; read_size == write_size
void udpsreader_th_nonzeroeing(inq_type<block>* inq, sync_type<th_type>* args) {
    // All pointers have already been validated by udpsreader (the manager)
    th_type*           th_args = args->userdata;
    runtime*           rteptr  = th_args->network->rteptr;
    outq_type<block>*  outq    = th_args->outq;

    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];

    // Cache ANYTHING that is known & constant.
    // If a value MUST be constant, then MAKE IT SO.
    const unsigned int           n_dg_p_block   = blocksize/wr_size;

    //   * one prototype block with fillpattern + zeroes etc
    //     in the right places to initialize a freshly allocated
    //     block with
    block                   b;
    unsigned char*          fpblock = new unsigned char[ wr_size ];
    unsigned char*          flagptr;
    unsigned char*          dataptr;
    
    // Fill the fillpattern block with fillpattern
    {
        uint64_t*      dgptr   = (uint64_t*)fpblock;
        const uint64_t fillpat = ((uint64_t)0x11223344 << 32) + 0x11223344;

        for(unsigned int i=wr_size, j=0; i>=sizeof(uint64_t); i-=sizeof(uint64_t), j++)
            dgptr[j] = fillpat;
    }

    // Ok, fall into our main loop
    DEBUG(0, "udpsreader_th_nonzeroeing/starting " << endl);
    while( inq->pop(b) ) {
        dataptr = (unsigned char*)b.iov_base;
        flagptr = dataptr + blocksize;

        for(unsigned int i=0; i<n_dg_p_block; i++, dataptr+=wr_size)
            if( flagptr[i]==0 )
                ::memcpy(dataptr, fpblock, wr_size);
        if( outq->push(b.sub(0, blocksize))==false )
            break;
    }
    DEBUG(0, "udpsreader_th_nonzeroeing/done " << endl);
    delete [] fpblock;
}

// Deal with the case where rd_size != wrsize.
//
// This is the top half which deals with correctly writing fill pattern in
// the part of the packet that was read from the network and appends zeroes
// (there are zeroes to be added or else you wouldn't be in THIS top half
// d'oh) in the remainder.
// The reason that there should be zeroes there, is because a following
// decompression step(*), which expands the compressed packet back to
// full "write_size" size, can only move bits around based on the 
// assumption they are initialized with zero; the bit movement is done
// by bitwise OR'ing "bit" with the destination bit: bit OR dest.
// If "dest" == 0 this reads: "bit" OR 0, which => result = "bit"
// (i.e. the value of "bit" has been moved to "dest").
// If "dest" == 1 (which could be the case if there's fill pattern there)
// the OR would read: "bit" OR 1 which would _always_ evaluate to "1",
// i.e. if "bit" happened to be zero, it would not be "moved" succesfully.
//
// (*) the fact that wr_size != rd_size is _because_ compression was in
//     effect on the sending side!
void udpsreader_th_zeroeing(inq_type<block>* inq, sync_type<th_type>* args) {
    // All pointers have already been validated by udpsreader (the manager)
    th_type*           th_args = args->userdata;
    runtime*           rteptr  = th_args->network->rteptr;
    outq_type<block>*  outq    = th_args->outq;

    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];

    // Cache ANYTHING that is known & constant.
    // If a value MUST be constant, then MAKE IT SO.
    const unsigned int           n_dg_p_block   = blocksize/wr_size;

    //   * one prototype block with fillpattern + zeroes etc
    //     in the right places to initialize a freshly allocated
    //     block with
    block                   b;
    const size_t            nzeroes = (wr_size - rd_size);
    unsigned char*          fpblock = new unsigned char[ rd_size ];
    unsigned char*          zeroes  = (unsigned char*)::calloc(nzeroes, 1);
    unsigned char*          flagptr;
    unsigned char*          dataptr;

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

        for(unsigned int i=rd_size, j=0; i>=sizeof(uint64_t); i-=sizeof(uint64_t), j++)
            dgptr[j] = fillpat;
    }

    // Ok, fall into our main loop
    DEBUG(0, "udpsreader_th_zeroeing/starting " << endl);
    while( inq->pop(b) ) {
        dataptr = (unsigned char*)b.iov_base;
        flagptr = dataptr + blocksize;

        for(unsigned int i=0; i<n_dg_p_block; i++, dataptr+=wr_size) {
            // do we need to insert fill pattern?
            if( flagptr[i]==0 )
                ::memcpy(dataptr, fpblock, rd_size);
            // append the zeroes
            ::memcpy(dataptr+rd_size, zeroes, nzeroes);
        }
        if( outq->push(b.sub(0, blocksize))==false )
            break;
    }
    delete [] fpblock;
    delete [] zeroes;
    DEBUG(0, "udpsreader_th_zeroeing/done " << endl);
}

void udpsreader_th(inq_type<block>* inq, sync_type<th_type>* args) {
    // All pointers have already been validated by udpsreader (the manager)
    th_type*           th_args = args->userdata;
    runtime*           rteptr  = th_args->network->rteptr;

    const unsigned int rd_size = rteptr->sizes[constraints::write_size];
    const unsigned int wr_size = rteptr->sizes[constraints::read_size];

    // Make the decision once which top half to fall into
    if( rd_size==wr_size ) 
        udpsreader_th_nonzeroeing(inq, args);
    else
        udpsreader_th_zeroeing(inq, args);
}

// The actual udpsreader does nothing but build up a local processing chain
void udpsreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    // Here we do the pre-check that everything points at something
    chain         c;
    runtime*      rteptr = 0;
    fdreaderargs* network = args->userdata;

    if( network )
        rteptr = network->rteptr; 
    ASSERT_COND( rteptr && network );

    // Before diving in too deep  ...
    // this asserts that all sizes make sense and meet certain constraints
    RTEEXEC(*rteptr, rteptr->sizes.validate());

    DEBUG(2, "udpsreader/manager starting" << endl);
    // Build local processing chain
    c.add(&udpsreader_bh, 2, args->userdata);
    c.add(&udpsreader_th, th_type(args->userdata, outq));
    c.run();
    // and wait until it's done ...
    c.wait();
    DEBUG(2, "udpsreader/manager done" << endl);
}




// Straight through UDP reader - no sequence number but with
// backtraffic every minute
void udpreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    bool                      stop;
    time_t                    lastack;
    ssize_t                   r;
    runtime*                  rteptr = 0;
    socklen_t                 slen( sizeof(struct sockaddr_in) );
    unsigned int              ack = 0;
    fdreaderargs*             network = args->userdata;
    struct sockaddr_in        sender;
    static string             acks[] = {"xhg", "xybbgmnx",
                                        "xyreryvwre", "tbqireqbzzr",
                                        "obxxryhy", "rvxryovwgre",
                                        "qebrsgbrgre", "" /* leave empty string last!*/};

    if( network )
        rteptr = network->rteptr; 
    ASSERT_COND( rteptr && network );

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
    //
    // In order for decompression to work we have to fill up the difference
    // between read_size and write_size with zeroes
    block                        b;
    const unsigned int           rd_size   = rteptr->sizes[constraints::write_size];
    const unsigned int           wr_size   = rteptr->sizes[constraints::read_size];
    const unsigned int           blocksize = rteptr->sizes[constraints::blocksize];

    // HV: 13-11-2013 Blocks larger than this size we call "insensible" and we 
    //                change the readahead + blockpool allocation scheme
    //                because we might be doing vlbi_streamer style
    const unsigned int           sensible_blocksize( 32*1024*1024 );

    // Note: cannot use std::auto_ptr here because we need 
    //       "operator delete []" and not "operator delete" -
    //       std::auto_ptr manages only a single object, not an array
    // Note: must remember how many zeroes we allocated, for
    //       the memcpy ...
    const size_t                 nzeroes        = wr_size - rd_size;
    const unsigned char*         zeroes         = (unsigned char*)(nzeroes?(::calloc(nzeroes, 1)):0);

    // Create a blockpool. If we need blocks we take'm from there
    // HV: 13-11-2013 If blocksize seems too large, do not allocate
    //                more than ~2GB/turn [which would be ~4 chunks
    //                for vlbi_streamer mode in 512MB/chunk] or 32 blocks.
    //                Only go for 2GB per allocation if we 
    //                *really* have to!
    const unsigned int  nb = (blocksize<sensible_blocksize?32:2);

    SYNC3EXEC(args,
              network->pool = new blockpool_type(blocksize, nb),
              delete [] zeroes);

    // If blocksize > sensible block size start to pre-allocate!
    if( blocksize>=sensible_blocksize ) {
        list<block>        bl;
        const unsigned int npre = network->netparms.nblock;
        DEBUG(2, "udpreader: start pre-allocating " << npre << " blocks" << endl);
        for(unsigned int i=0; i<npre; i++) {
            block tmpb = network->pool->get();
            ::memset(tmpb.iov_base, 0x0, tmpb.iov_len);
            bl.push_back( tmpb );
        }
        DEBUG(2, "udpreader: ok, done that!" << endl);
    }

    // Set up the message - a lot of these fields have known & constant values
    struct iovec                 iov[1];
    struct msghdr                msg;
    msg.msg_name       = 0;
    msg.msg_namelen    = 0;

    // no control stuff, nor flags
    msg.msg_control    = 0;
    msg.msg_controllen = 0;
    msg.msg_flags      = 0;

    // message 'msg': one fragment, the datagram
    msg.msg_iov        = &iov[0];
    msg.msg_iovlen     = 1;

    // The size of the datapart of the message are known
    iov[0].iov_len     = rd_size;

    // Here we fix the amount of iov's and lengths of the
    // messages for the two phases: PEEK and WAITALL
    // We should be safe for datagrams up to 2G i hope
    //   (the casts to 'int' from iov[..].iov_len because
    //    the .iov_len is-an unsigned)
    const int               waitallread = (int)(iov[0].iov_len);

    // reset statistics/chain and statistics/evlbi
    RTE3EXEC(*rteptr,
            rteptr->evlbi_stats = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdpRead") ,
            delete [] zeroes );

    // Great. We're done setting up. Now let's see if we weren't cancelled
    // by any chance
    SYNCEXEC(args, stop = args->cancelled);

    if( stop ) {
        delete [] zeroes;
        DEBUG(0, "udpreader: cancelled before actual start" << endl);
        return;
    }

    // No, we weren't. Now go into our mainloop!
    DEBUG(0, "udpreader: fd=" << network->fd << " datagramlength=" << iov[0].iov_len << endl);

    // create references to the statisticscounters -
    // at least one of the memoryaccesses has been 
    // removed then (if you do it via pointer
    // then there's two)
    counter_type&    counter( rteptr->statistics.counter(args->stepid) );
    ucounter_type&   pktcnt( rteptr->evlbi_stats.pkt_in );

    // inner loop variables
    unsigned char* location;
    unsigned char* endptr;

    // Before actually starting to receive get a block and initialize
    b = network->pool->get();

    pktcnt++;
    location = (unsigned char*)b.iov_base;
    endptr   = (unsigned char*)b.iov_base + blocksize;

    // HV: 9 Jul 2012 - in order to prevent flooding we will
    //     have to send UDP backtraffic every now and then
    //     (such that network equipment between the scope and
    //     us does not forget our ARP entry). 
    //     We read the first packet and record who sent it to us.
    if( ::recvfrom(network->fd, location, rd_size, MSG_WAITALL, (struct sockaddr*)&sender, &slen)!=(ssize_t)rd_size ) {
        delete [] zeroes;
        DEBUG(-1, "udpreader: cancelled before beginning" << endl);
        return;
    }
    location += wr_size; // next packet will be put at write size
    lastack   = 0;       // trigger immediate ack send

    DEBUG(0, "udpreader: incoming data from " <<
              inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << endl);

    // Drop into our tight inner loop
    do {
        // First check if we filled up a block
        if( location>=endptr ) {
            if( outq->push(b)==false )
                break;
            // get a new block to write data in
            b = network->pool->get();
            location = (unsigned char*)b.iov_base;
            endptr   = (unsigned char*)b.iov_base + blocksize;
        }

        // Our primary computations have been done and, what's most
        // important, a location for the packet has been decided upon
        // Read the pakkit into our mem'ry space before we do anything else
        iov[0].iov_base = location;
        if( (r=::recvmsg(network->fd, &msg, MSG_WAITALL))!=(ssize_t)waitallread ) {
            lastsyserror_type lse;
            ostringstream     oss;

            delete [] zeroes;
            oss << "::recvmsg(network->fd, &msg, MSG_WAITALL) fails - [" << lse << "] (ask:" << waitallread << " got:" << r << ")";
            throw syscallexception(oss.str());
        }

        // Now that we've *really* read the pakkit we may update our
        // read statistics, but not before we've appended the zeroes
        if( zeroes )
            ::memcpy(location + rd_size, zeroes, nzeroes);

        counter  += waitallread;
        location += wr_size;
        pktcnt++;

        time_t   nowack = ::time(NULL);
        // Send out an ack before we go into infinite wait
        if( (nowack - lastack) > 59 ) {
            if( acks[ack].empty() )
                ack = 0;
            // Only warn if we fail to send. Try again in two minutes
            if( ::sendto(network->fd, acks[ack].c_str(), acks[ack].size(), 0,
                         (const struct sockaddr*)&sender, sizeof(struct sockaddr_in))==-1 )
                DEBUG(-1, "udpreader: WARN failed to send ACK back to sender" << endl);
            lastack = nowack;
            ack++;
        }
    } while( true );

    // Clean up
    delete [] zeroes;
    DEBUG(0, "udpreader: stopping" << endl);
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
            rteptr->statistics.init(args->stepid, "SocketReadv2"));

    counter_type&        counter( rteptr->statistics.counter(args->stepid) );
    const unsigned int   rd_size = rteptr->sizes[constraints::write_size];
    const unsigned int   wr_size = rteptr->sizes[constraints::read_size];
    const unsigned int   bl_size = rteptr->sizes[constraints::blocksize];
    const unsigned int   n_blank = (wr_size - rd_size); 

    SYNCEXEC(args,
            stop = args->cancelled;
            if( !stop ) network->pool = new blockpool_type(bl_size, rteptr->netparms.nblock););

    if( stop ) {
        DEBUG(0, "socketreader: stop signalled before we actually started" <<
                endl);
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
                    DEBUG(0, "socketreader: remote side closed connection" << endl);
                } else {
                    DEBUG(0, "socketreader: read failure " << lse << endl);
                }
                // No use in going on, but signal that we're not going to
                // read anymore
                if( ::write(network->fd, &r, 1)==0 )
                    if( ::shutdown(network->fd, SHUT_RD) ) {}
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
                DEBUG(-1, "socketreader: skip blok because of constraint error. blocksize not integral multiple of write_size" << endl);
                continue;
            }
            // Ok, variable block size allowed, update size of current block
            // The expected length of the block is up to 'eptr' (==b.iov_len)
            // so the bit that wasn't filled in can be subtracted
            b.iov_len -= (size_t)(eptr - ptr);
            DEBUG(3, "socketreader: partial block; adjusting block size by -" << (size_t)(eptr - ptr) << endl);
        }
        // push it downstream. note: compute the actual start of the block
        // since the
        // original value ("ptr") has potentially been ge-overwritten; it's
        // been
        // used as a temp
        if( outq->push(b)==false )
            break;
    }
    DEBUG(0, "socketreader: stopping. read " << bytesread << " (" <<
            byteprint((double)bytesread,"byte") << ")" << endl);
    network->finished = true;
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

    // wait for the "GO" signal
    args->lock();
    while( !args->cancelled && !file->run ) {
        args->cond_wait();
    }
    stop = args->cancelled;
    args->unlock();

    if( stop ) {
        DEBUG(0, "fdreader: stopsignal caught before actual start" << endl);
        return;
    }

    SYNCEXEC(args,
             file->pool = new blockpool_type(blocksize, 16); );
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "FdRead"));

    counter_type&   counter( rteptr->statistics.counter(args->stepid) );

    // update submode flags
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.set(connected_flag).set(run_flag));

    DEBUG(0, "fdreader: start reading from fd=" << file->fd << endl);

    ASSERT_POS( ::lseek(file->fd, file->start, SEEK_SET) );
    while( !stop && ((file->end == 0) || (counter < file->end)) ) {
        block           b = file->pool->get();

        // do read data orf the network
        if( (r=::read(file->fd, b.iov_base, b.iov_len))!=(int)b.iov_len ) {
            // first check if we have less data than we expect AND
            // are allowed to push that
            bool partial_read = false;
            if ( r>0 ) {
                SYNCEXEC( args,
                          partial_read = file->allow_variable_block_size );
                if ( partial_read ) {
                    b.iov_len = r;
                }
            }
            
            if ( !partial_read ) {
                if( r==0 ) {
                    DEBUG(-1, "fdreader: EOF read" << endl);
                } else if( r==-1 ) {
                    DEBUG(-1, "fdreader: READ FAILURE - " << ::strerror(errno) << endl);
                } else {
                    DEBUG(-1, "fdreader: unexpected EOF - want " << b.iov_len << " bytes, got " << r << endl);
                }
                break;
            }
        }
        // push it downstream
        if( outq->push(b)==false )
            break;

        // update statistics counter
        counter += b.iov_len;
    }
    DEBUG(0, "fdreader: done " << byteprint((double)counter, "byte") << endl);
    file->finished = true;
}

void udtreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
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
            rteptr->evlbi_stats = evlbi_stats_type();
            rteptr->statistics.init(args->stepid, "UdtReadv2"));

    counter_type&        counter( rteptr->statistics.counter(args->stepid) );
    const unsigned int   rd_size = rteptr->sizes[constraints::write_size];
    const unsigned int   wr_size = rteptr->sizes[constraints::read_size];
    const unsigned int   bl_size = rteptr->sizes[constraints::blocksize];
    const unsigned int   n_blank = (wr_size - rd_size);

    ucounter_type&       loscnt( rteptr->evlbi_stats.pkt_lost );
    ucounter_type&       pktcnt( rteptr->evlbi_stats.pkt_in );
    SYNCEXEC(args,
             stop = args->cancelled;
             if(!stop) network->pool = new blockpool_type(bl_size,16););

    if( stop ) {
        DEBUG(0, "udtreader: stop signalled before we actually started" << endl);
        return;
    }

    DEBUG(0, "udtreader: read fd=" << network->fd << " rd:" << rd_size
             << " wr:" << wr_size <<  " bs:" << bl_size << endl);
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
                              << udterror.getErrorCode() << ")" << endl);
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
                DEBUG(-1, "udtreader: skip blok because of constraint error. blocksize not integral multiple of write_size" << endl);
                continue;
            }
            // Ok, variable block size allowed, update size of current block
            // The expected length of the block is up to 'eptr' (==b.iov_len)
            // so the bit that wasn't filled in can be subtracted
            b.iov_len -= (size_t)(eptr - ptr);
            DEBUG(3, "udtreader: partial block; adjusting block size by -" << (size_t)(eptr - ptr) << endl);
        }
        // push it downstream
        if( outq->push(b)==false )
            break;
    }
    DEBUG(0, "udtreader: stopping. read " << bytesread << " (" <<
             byteprint((double)bytesread,"byte") << ")" << endl);
}

void netreader(outq_type<block>* outq, sync_type<fdreaderargs>* args) {
    // deal with generic networkstuff
    bool                   stop;
    fdreaderargs*          network = args->userdata;
    const string           proto = network->netparms.get_protocol();

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

        // dispatch based on actual protocol
        if( proto=="unix" )
            incoming = new fdprops_type::value_type(do_accept_incoming_ux(network->fd));
        else if( proto=="udt" )
            incoming = new fdprops_type::value_type(do_accept_incoming_udt(network->fd));
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
            if( proto=="udt" )
                UDT::close(network->fd);
            else
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
    if( proto=="udps" )
        udpsreader(outq, args);
    else if( proto=="udp" )
        udpreader(outq, args);
    else if( proto=="udt" )
        udtreader(outq, args);
    else if( proto=="itcp") {
        // read the itcp id from the stream before falling to the normal
        // tcp reader
        char c;
        unsigned int num_zero_bytes = 0;
        ostringstream os;
        while ( num_zero_bytes < 2 ) {
            ASSERT_COND( ::read(network->fd, &c, 1) == 1 );
            if ( c == '\0' ) {
                num_zero_bytes++;
            }
            else {
                num_zero_bytes = 0;
            }
            os << c;
        }
        vector<string> identifiers = split( os.str(), '\0', false );
        
        // make key/value pairs from the identifiers
        map<string, string> id_values;
        const string separator(": ");
        // the last identifiers 2 will be empty, so don't bother
        for (size_t i = 0; i < identifiers.size() - 2; i++) { 
            size_t separator_index = identifiers[i].find(separator);
            if ( separator_index == string::npos ) {
                THROW_EZEXCEPT(itcpexception, "Failed to find separator in itcp stream line: '" << identifiers[i] << "'" << endl);
            }
            id_values[ identifiers[i].substr(0, separator_index) ] = 
                identifiers[i].substr(separator_index + separator.size());
        }

        // only start reading if the itcp is as expected or no expectation is set
        if ( network->rteptr->itcp_id.empty() ) {
            socketreader(outq, args);
        }
        else {
            map<string, string>::const_iterator id_iter =  id_values.find("id");
            if ( id_iter == id_values.end() ) {
                DEBUG(-1, "No TCP id received, but expected '" << network->rteptr->itcp_id << "', will NOT continue reading" << endl);
            }
            else if ( id_iter->second != network->rteptr->itcp_id ) {
                DEBUG(-1, "Received TCP id '" << id_iter->second << "', but expected '" << network->rteptr->itcp_id << "', will NOT continue reading" << endl);
            }
            else {
                socketreader(outq, args);
            }
        }
    }
    else
        socketreader(outq, args);

    network->finished = true;
}


// Compress a tapeframe.
void framecompressor(inq_type<frame>* inq, outq_type<block>* outq, sync_type<compressorargs>* args) {
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

    while( true ) {
        frame f;
        if ( !inq->pop(f) ) {
            break;
        }
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
            if ( buffargs->bytestodrop < topush.iov_len ) {
                outq->push( topush );
            }
            else {
                buffargs->bytestodrop -= topush.iov_len;
            }
        }
        blockqueue.push( b );
        bytesbuffered += b.iov_len;
        counter       += b.iov_len;
    }
    DEBUG(0, "bufferer: stopping." << endl);
}


// duplicator functions


// duplicate the incoming data of type T by a factor duplicatorargs->factor
template <typename T>
void duplicatorstepX(inq_type<block>* iq, outq_type<block>* oq, sync_type<duplicatorargs>* args) {
    block              ib;
    block              ob;
    T*                 iptr = 0;
    T*                 optr = 0;
    duplicatorargs*    fargs = args->userdata;
    const unsigned int factor = fargs->factor;
    const unsigned int ibs = fargs->rteptr->sizes[constraints::blocksize];
    const unsigned int nTs = ibs / sizeof(T);
    const unsigned int obs = factor*ibs;

    SYNCEXEC( args, fargs->pool = new blockpool_type(obs, 16) );

    while( iq->pop(ib) ) {
    	ob   = fargs->pool->get();
    	optr = (T*)ob.iov_base;

        // make sure that what we get is what we expect
        ASSERT2_COND( ib.iov_len>=ibs, SCINFO("Expected block with size " << ibs << " got " << ib.iov_len));
        iptr  = (T*)ib.iov_base;

        // expand
        for(T *ptr = optr, *eptr=ptr+factor*nTs; ptr<eptr; iptr++)
            for(unsigned int i=0; i<factor; i++)
                *ptr++ = *iptr;
	if( oq->push(ob)==false )
		break;
        ib = block();
    }
}

void duplicatorstep(inq_type<block>* iq, outq_type<block>* oq, sync_type<duplicatorargs>* args) {
    duplicatorargs* dargs = args->userdata;

    switch( dargs->itemsz ) {
        case 8:
            duplicatorstepX<uint8_t>(iq, oq, args);
            break;
        case 16:
            duplicatorstepX<uint16_t>(iq, oq, args);
            break;
        case 32:
            duplicatorstepX<uint32_t>(iq, oq, args);
            break;
        default:
            ASSERT2_COND(false, SCINFO("Invalid duplicatorargs->itemsz of " << dargs->itemsz << " (8, 16, 32 supported)"));
    }
}

// Filedescriptor writer for SFXC.
// wait for a rendezvous message from SFXC
// whatever gets popped from the inq gets written to the filedescriptor.
// leave intelligence up to other steps.
void sfxcwriter(inq_type<block>* inq, sync_type<fdreaderargs>* args) {
    bool           stop = false;
    runtime*       rteptr;
    uint64_t       nbyte = 0;
    fdreaderargs*  network = args->userdata;
    char msg[20];
    struct sockaddr_storage addr;
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

    len = sizeof(addr);
    ASSERT_COND( (network->fd=::accept(s, (struct sockaddr *)&addr, &len))!=-1 );

    ASSERT_COND( ::close(s)!= -1 );

    ASSERT_COND( ::read(network->fd, &msg, sizeof(msg))==sizeof(msg) );

    DEBUG(0, "sfxcwriter: writing to fd=" << network->fd << endl);

    // blind copy of incoming data to outgoing filedescriptor
    while( true ) {
        block b;
        if ( !inq->pop(b) ) {
            break;
        }
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
    network->finished = true;
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
    const headersearch_type hdr(rteptr->trackformat(), rteptr->ntrack(),
                                (unsigned int)rteptr->trackbitrate(),
                                rteptr->vdifframesize());
    const uint64_t          fp = ((uint64_t)0x11223344 << 32) + 0x11223344;
    const uint64_t          m  = ((rteptr->solution)?(rteptr->solution.mask()):(~(uint64_t)0));
    const unsigned int      fs = hdr.framesize;
    const unsigned int      co = rteptr->sizes[constraints::compress_offset];
    const unsigned int      rd = rteptr->sizes[constraints::read_size];
    const unsigned int      n_ull_p_rd = rd/sizeof(uint64_t);
    const unsigned int      n_ull_p_fr = fs/sizeof(uint64_t);

    ASSERT2_COND( hdr.valid(), SCINFO("framecheck requested but no frameformat known?"));
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
    const headersearch_type hdr(rteptr->trackformat(), rteptr->ntrack(),
                                (unsigned int)rteptr->trackbitrate(),
                                rteptr->vdifframesize());

    // Data is sent in frames if the framesize is constrained
    if( hdr.valid() )
        framechecker(inq, args);
    else
        blockchecker(inq, args);
    return;
}

// Decode + print all timestamps
void timeprinter(inq_type<frame>* inq, sync_type<headersearch_type>* args) {
    char                            buf[64];
    frame                           f;
    size_t                          l;
    uint64_t                        nframe = 0;
    struct tm                       frametime_tm;
    struct timespec                 frametime;
    headersearch_type               header = *args->userdata;
    const headersearch::strict_type chk = headersearch::strict_type(headersearch::chk_default)|headersearch::chk_verbose|headersearch::chk_allow_dbe;

    DEBUG(2,"timeprinter: starting - " << header.frameformat << " " << header.ntrack << endl);
    while( inq->pop(f) ) {
        if( f.frametype!=header.frameformat ||
            f.ntrack!=header.ntrack ) {
            DEBUG(-1, "timeprinter: expect " << header.ntrack << " track " << header.frameformat
                      << ", got " << f.ntrack << " track " << f.frametype << endl);
            continue;
        }
        try {
            frametime = header.decode_timestamp((unsigned char const*)f.framedata.iov_base, chk);
            ::gmtime_r(&frametime.tv_sec, &frametime_tm);
            // Format the data + hours & minutes. Seconds will be dealt with
            // separately
            l = ::strftime(&buf[0], sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm", &frametime_tm);
            ::snprintf(&buf[l], sizeof(buf)-l, "%08.5fs", frametime_tm.tm_sec + ((double)frametime.tv_nsec * 1.0e-9));
            DEBUG(1, "[" << header.frameformat << " " << header.ntrack << "] " << buf << endl);
        }
        catch( ... ) {
        }
        nframe++;
    }
    DEBUG(2,"timeprinter: stopping" << endl);
}

void timechecker(inq_type<frame>* inq, sync_type<headersearch_type>* args) {
    char                            buf[64];
    long                            last_tvsec = 0;
    frame                           f;
    size_t                          l;
    struct tm                       frametime_tm;
    struct timespec                 frametime;
    headersearch_type               header = *args->userdata;
    const headersearch::strict_type chk = headersearch::strict_type(headersearch::chk_default)|headersearch::chk_verbose;

    DEBUG(-1, "timechecker: starting - " << header.frameformat << " " << header.ntrack << endl);

    while( inq->pop(f) ) {
        bool tsfail = false;

        if( f.frametype!=header.frameformat ||
            f.ntrack!=header.ntrack ) {
            DEBUG(-1, "timeprinter: expect " << header.ntrack << " track " << header.frameformat
                      << ", got " << f.ntrack << " track " << f.frametype << endl);
            break;
        }
        try {
            frametime = header.decode_timestamp((unsigned char const*)f.framedata.iov_base, chk);
            if( frametime.tv_sec!=last_tvsec ) {
                ::gmtime_r(&frametime.tv_sec, &frametime_tm);
                // Format the data + hours & minutes. Seconds will be dealt with
                // separately
                l = ::strftime(&buf[0], sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm", &frametime_tm);
                ::snprintf(&buf[l], sizeof(buf)-l, "%08.5fs", frametime_tm.tm_sec + ((double)frametime.tv_nsec * 1.0e-9));
                DEBUG(-1, "timechecker: " << buf << endl);

                if( ::labs(frametime.tv_sec-last_tvsec)!=1 )
                    tsfail = true;
            }
            last_tvsec = frametime.tv_sec; 
        }
        catch( ... ) {
            tsfail = true;
        }
        if( tsfail ) {
            char            buff[16];
            ostringstream   oss;
            uint32_t const* bytes = (uint32_t const*)f.framedata.iov_base;

            oss.str( string() );
            for( unsigned int i=0; i<4; i++ ) {
                ::sprintf(buff, "0x%08x ", bytes[i]);
                oss << buff;
            }
            DEBUG(-1, "  " << oss.str() << endl);
        }
    }
    DEBUG(2,"timeprinter: stopping" << endl);
}

// Print anomalies in time sequence
void timechecker2(inq_type<frame>* inq, sync_type<headersearch_type>* args) {
    char                            buf[64];
    frame                           f;
    size_t                          l;
    double                          framedelta;
    double                          delta;
    uint64_t                        nframe = 0;
    struct tm                       frametime_tm;
    struct timespec                 frametime;
    struct timespec                 lasttime;
    headersearch_type               header = *args->userdata;
    pcint::timeval_type             tt;
    const headersearch::strict_type chk = headersearch::strict_type(headersearch::chk_default)|headersearch::chk_verbose;

    DEBUG(-1, "timechecker: starting - " << header.frameformat << " " << header.ntrack << endl);

    // Get first frame in
    if( inq->pop(f)==false ) {
        DEBUG(-1, "timechecker: cancelled before start" << endl);
        return;
    }
    DEBUG(-1, "timechecker[" << pcint::timeval_type::now() << "] first data arrives" << endl);
    if( f.frametype!=header.frameformat ||
        f.ntrack!=header.ntrack ) {
        DEBUG(-1, "timeprinter: frame#0 expect " << header.ntrack << " track " << header.frameformat
                << ", got " << f.ntrack << " track " << f.frametype << endl);
        return;
    }
    if( is_vdif(header.frameformat) )
        pvdif(f.framedata.iov_base);
    lasttime   = header.decode_timestamp((unsigned char const*)f.framedata.iov_base, chk);

    // Get second frame in
    if( inq->pop(f)==false ) {
        DEBUG(-1, "timechecker: cancelled before start (waiting for 2nd frame)" << endl);
        return;
    }
    if( f.frametype!=header.frameformat ||
        f.ntrack!=header.ntrack ) {
        DEBUG(-1, "timeprinter: frame #1 expect " << header.ntrack << " track " << header.frameformat
                << ", got " << f.ntrack << " track " << f.frametype << endl);
        return;
    }
    frametime  = header.decode_timestamp((unsigned char const*)f.framedata.iov_base, chk);
    framedelta = (frametime.tv_sec - lasttime.tv_sec)*1.0e9 + frametime.tv_nsec - lasttime.tv_nsec;
    DEBUG(-1, "timechecker[" << pcint::timeval_type::now() << "] framedelta is " << framedelta << "ns" << endl);

    if( ::fabs(framedelta) < 10 ) {
        DEBUG(-1, " ...??? framedelta < 10ns:" << endl <<
                 "   frame #2: " << frametime.tv_sec << "s " << frametime.tv_nsec << "ns" << endl <<
                 "   frame #1: " << lasttime.tv_sec << "s " << lasttime.tv_nsec << "ns" << endl);
        DEBUG(-1, "this is too small, quitting" << endl);
        return;
    }

    nframe = 2;
    lasttime = frametime;

    while( inq->pop(f) ) {
        if( f.frametype!=header.frameformat ||
            f.ntrack!=header.ntrack ) {
            DEBUG(-1, "timeprinter: expect " << header.ntrack << " track " << header.frameformat
                      << ", got " << f.ntrack << " track " << f.frametype << endl);
            break;
        }
        try {
            frametime = header.decode_timestamp((unsigned char const*)f.framedata.iov_base, chk);
        }
        catch( ... ) {
            continue;
        }
        delta = (frametime.tv_sec - lasttime.tv_sec)*1.0e9 + frametime.tv_nsec - lasttime.tv_nsec;

        // More than 10ns difference triggers 'error'
        if( ::fabs(delta - framedelta)>10 ) {
            DEBUG(-1, "timechecker[" << pcint::timeval_type::now() << "] frame #" << nframe << " framedelta is " << delta << "ns, should be " << framedelta << endl);
            ::gmtime_r(&frametime.tv_sec, &frametime_tm);
            // Format the data + hours & minutes. Seconds will be dealt with
            // separately
            l = ::strftime(&buf[0], sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm", &frametime_tm);
            ::snprintf(&buf[l], sizeof(buf)-l, "%08.5fs", frametime_tm.tm_sec + ((double)frametime.tv_nsec * 1.0e-9));
            DEBUG(-1, "  ts now: " << buf << endl);
            ::gmtime_r(&lasttime.tv_sec, &frametime_tm);
            // Format the data + hours & minutes. Seconds will be dealt with
            // separately
            l = ::strftime(&buf[0], sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm", &frametime_tm);
            ::snprintf(&buf[l], sizeof(buf)-l, "%08.5fs", frametime_tm.tm_sec + ((double)lasttime.tv_nsec * 1.0e-9));
            DEBUG(-1, "  ts last: " << buf << endl);

            if( is_vdif(header.frameformat) )
                pvdif(f.framedata.iov_base);
        }
        if( nframe++ > 20 )
            break;
        lasttime = frametime;
    }
    DEBUG(2,"timeprinter: stopping" << endl);
}

// Grab the timestamps and update the time when the UT second of the
// timestamps change
void timegrabber(inq_type<frame>* inq, sync_type<timegrabber_type>* args) {
    long           last_tvsec = 0;
    frame          f;
    uint64_t       nFrame = 0, nSec = 0;
    struct timeval tv;

    DEBUG(2, "timegrabber: starting ..." << endl);
    while( inq->pop(f) ) {
        nFrame++;
        if( f.frametime.tv_sec!=last_tvsec ) {
            nSec++;
            // get the current time asap. locking is done later
            ::gettimeofday(&tv, 0);
            SYNCEXEC(args, args->userdata->data_time       = f.frametime; \
                           args->userdata->os_time.tv_sec  = tv.tv_sec; \
                           args->userdata->os_time.tv_nsec = tv.tv_usec*1000;)
        }
        last_tvsec = f.frametime.tv_sec;
    }
    DEBUG(2, "timegrabber: done " << nFrame << " frames, " << nSec << "s" << endl);
}

void timemanipulator(inq_type<tagged<frame> >* inq,
                     outq_type<tagged<frame> >* outq,
                     sync_type<timemanipulator_type>* args) {
    const struct timespec   dt = args->userdata->dt;

    while( true ) {
        tagged<frame>    tf;
        if( inq->pop(tf)==false )
            break;

        struct timespec& ts = tf.item.frametime;

        ts.tv_sec  += dt.tv_sec;
        ts.tv_nsec += dt.tv_nsec;
        // end up in next second?
        while( ts.tv_nsec>999999999 ) {
            ts.tv_sec  += 1;
            ts.tv_nsec -= 1000000000;
        }
        // end up in previous second?
        while( ts.tv_nsec<0 ) {
            ts.tv_sec  -= 1;
            ts.tv_nsec += 1000000000;
        }

        if( outq->push(tf)==false )
            break;
    }
}


void timedecoder(inq_type<frame>* inq, outq_type<frame>* oq, sync_type<headersearch_type>* args) {
    frame                           f;
    headersearch_type               header = *args->userdata;
    const struct ::timespec         zero = {0, 0};
    const headersearch::strict_type chk = headersearch::strict_type(headersearch::chk_default)|headersearch::chk_verbose;

    DEBUG(2,"timedecoder: starting - " << header.frameformat << " " << header.ntrack << endl);
    while( inq->pop(f) ) {
        if( f.frametype!=header.frameformat ||
            f.ntrack!=header.ntrack ) {
            DEBUG(-1, "timedecoder: expect " << header.ntrack << " track " << header.frameformat
                      << ", got " << f.ntrack << " track " << f.frametype << endl);
            continue;
        }
        if( f.frametime!=zero )
            f.frametime = header.decode_timestamp((unsigned char const*)f.framedata.iov_base, chk);
        if( oq->push(f)==false )
            break;
    }
    DEBUG(2,"timedecodeer: stopping" << endl);
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

void
fakerargs::init_vdif_frame()
{
    uint32_t *frame32;
    uint8_t ref_epoch;
    uint8_t log2nchans;
    struct tm tm;
    time_t clock;

    size = rteptr->vdifframesize();
    if (rteptr->trackformat() == fmt_vdif_legacy)
        size += 16;
    else
        size += 32;
    buffer = new unsigned char[size];
    frame32 = (uint32_t *)buffer;

    time(&clock);
    gmtime_r(&clock, &tm);
    tm.tm_mon = (tm.tm_mon < 6 ? 0 : 6);
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;

    ref_epoch = (tm.tm_year - 100) * 2 + tm.tm_mon / 6;
    ref_time = ::my_timegm(&tm);

    log2nchans = ((ffs(rteptr->ntrack() / 2) - 1) & 0x1f);

    frame32[0] = 0x80000000;
    if (rteptr->trackformat() == fmt_vdif_legacy)
        frame32[0] |= 0x40000000;
    frame32[1] = (ref_epoch << 24);
    frame32[2] = (log2nchans << 24) | (size / 8);
    frame32[3] = (1 << 26);
}

void
fakerargs::update_vdif_frame(time_t clock)
{
    uint32_t *frame32 = (uint32_t *)buffer;

    frame32[0] &= ~0x3fffffff;
    frame32[0] |= ((clock - ref_time) & 0x3fffffff);
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
    case fmt_vdif:
    case fmt_vdif_legacy:
        init_vdif_frame();
	break;
    default:
        THROW_EZEXCEPT(fakerexception, "Unknown track format");
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
    case fmt_vdif:
    case fmt_vdif_legacy:
        update_vdif_frame(clock);
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

    // Assert we do have a runtime pointer!
    ASSERT2_COND(rteptr = fakeargs->rteptr, SCINFO("OH NOES! No runtime pointer!"));

    // Do the initializing of the fakerargs with the lock held
    SYNCEXEC(args,
             fakeargs->init_frame();
             if( fakeargs->framepool==0 )
                fakeargs->framepool = new blockpool_type(fakeargs->size, 4));

    while( true ) {
        block b;
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
    const netparms_type&  np = net.netparms;
    const string          proto = np.get_protocol();
    unsigned int          olen( sizeof(np.sndbufsize) );
    // we're supposed to deliver a fresh instance of one of these
    fdreaderargs*         rv = new fdreaderargs();

    // copy over the runtime pointer
    rv->rteptr    = net.rteptr;
    rv->doaccept  = (proto=="tcp" || proto=="unix" || proto=="udt" || proto == "itcp");
    rv->blocksize = np.get_blocksize();
    rv->netparms  = np;

    // and do our thang
    if( proto=="rtcp" )
        rv->fd = getsok(np.host, np.get_port(), "tcp");
    else if( proto=="unix" )
        rv->fd = getsok_unix_server(np.host);
    else if( proto=="udt" )
        rv->fd = getsok_udt(np.get_port(), proto, np.get_mtu(), np.host);
    else if( proto=="itcp" )
        rv->fd = getsok(np.get_port(), "tcp", np.host);
    else
        rv->fd = getsok(np.get_port(), proto, np.host);

    // set send/receive bufsize on the sokkit
    if( proto!="udt" ) {
        if( np.sndbufsize>0 ) {
            ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_SNDBUF, &np.sndbufsize, olen) );
        }
        if( np.rcvbufsize>0 ) {
            ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_RCVBUF, &np.rcvbufsize, olen) );
        }
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
    const netparms_type&  np = net.netparms;
    const string          proto = np.get_protocol();
    // we're supposed to deliver a fresh instance of one of these
    unsigned int          olen( sizeof(np.rcvbufsize) );
    fdreaderargs*         rv = new fdreaderargs();

    // copy over the runtime pointer
    rv->rteptr   = net.rteptr;
    rv->doaccept = (proto=="rtcp");
    rv->netparms = net.netparms;

    // and do our thang
    if( proto=="rtcp" )
        rv->fd = getsok(np.get_port(), "tcp");
    else if( proto=="unix" )
        rv->fd = getsok_unix_client(np.host);
    else if( proto=="udt" )
        rv->fd = getsok_udt(np.host, np.get_port(), proto, np.get_mtu());
    else if( proto=="itcp" )
        rv->fd = getsok(np.host, np.get_port(), "tcp");
    else
        rv->fd = getsok(np.host, np.get_port(), proto);

    // set send/receive bufsize on the sokkit
    if( proto!="udt" ) {
        if( np.sndbufsize>0 ) {
            ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_SNDBUF, &np.sndbufsize, olen) );
        }
        if( np.rcvbufsize>0 ) {
            ASSERT_ZERO( ::setsockopt(rv->fd, SOL_SOCKET, SO_RCVBUF, &np.rcvbufsize, olen) );
        }
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
//    "n"   => create file for writing, fail if already exist
// the file will be opened in binary 
fdreaderargs* open_file(string filename, runtime* r) {
    int               flag = LARGEFILEFLAG; // smoke'm if you got'em
    mode_t            mode = 0; // only used when creating
    string            openmode;
    string            actualfilename;
    fdreaderargs*     rv = new fdreaderargs(); // FIX: memory leak if throws
    string::size_type openmodeptr;

    openmodeptr = filename.find(",");
    ASSERT2_COND( openmodeptr!=string::npos,
                  SCINFO(" add ',<mode>' to the filename (r,w,a,n)") );

    openmode       = tolower(filename.substr(openmodeptr+1));
    actualfilename = filename.substr(0, openmodeptr);

    ASSERT2_COND( filename.size()>0,
                  SCINFO(" no actual filename given") );
    ASSERT2_COND( openmode.size()>0,
                  SCINFO(" no actual openmode (r,w,a) given") );
    
    if( openmode=="r" ) 
        flag |= O_RDONLY;
    else if( (openmode=="w") || (openmode=="n") ) {
        // for n we check first that the file doesn't exists
        if ( openmode == "n") {
            flag |= O_EXCL;
        }
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
    rv->netparms.set_protocol("file");
    rv->rteptr = r;
    return rv;
}

fdreaderargs* open_sfxc_socket(string filename, runtime* r) {
    fdreaderargs*     rv = new fdreaderargs();
    long port;
    char *p;
    int s;

    port = ::strtol(filename.c_str(), &p, 0);
    if( *p==0 && port>0 && port<65536) {
      s = getsok(port, "tcp");
      rv->netparms.set_protocol("tcp");
    } else {
      ::unlink(filename.c_str());
      s = getsok_unix_server(filename);
      rv->netparms.set_protocol("unix");
    }

    rv->fd = s;
    DEBUG(0, "open_sfxc_socket: opened " << filename << " as fd=" << rv->fd << endl);
    rv->rteptr = r;
    return rv;
}



// * close the filedescriptor
// * set the value to "-1"
// * if the threadid is not-null, signal the thread so it will
//   fall out of any blocking systemcall
void close_filedescriptor(fdreaderargs* fdreader) {
    const string proto = fdreader->netparms.get_protocol();

    ASSERT_COND(fdreader);
    int (*close_fn)(int) = &::close;

    if( proto=="udt" )
        close_fn = &UDT::close;

    if( fdreader->fd!=-1 ) {
        // This used to be an "ASSERT()" but then if we fail to close, the
        // ->fd member doesn't get set to '-1' so this keeps repeating
        // if "close_filedescriptor()" is called > once
        if( close_fn(fdreader->fd)!=0 ) {
            DEBUG(-1, "Failed to close fd#" << fdreader->fd << " (" << proto << ") - if UDT, may already be closed" << endl);
        } else {
            DEBUG(3, "close_filedescriptor: closed fd#" << fdreader->fd << endl);
        }
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



framerargs::framerargs(headersearch_type h, runtime* rte, bool s) :
    strict(s), rteptr(rte), pool(0), hdr(h)
{ ASSERT_NZERO(rteptr); }

void framerargs::set_strict(bool b) {
    strict = b;
}

framerargs::~framerargs() {
    delete pool;
}

fillpatargs::fillpatargs():
    run( false ), realtime( false ), fill( ((uint64_t)0x11223344 << 32) + 0x11223344 ),
    inc( 0 ), rteptr( 0 ), nword( (uint64_t)-1), pool( 0 )
{}

fillpatargs::fillpatargs(runtime* r):
    run( false ), realtime(false), fill( ((uint64_t)0x11223344 << 32) + 0x11223344 ),
    inc( 0 ), rteptr( r ), nword( (uint64_t)-1), pool( 0 )
{ ASSERT_NZERO(rteptr); }

void fillpatargs::set_realtime(bool newval) {
    realtime = newval;
}
void fillpatargs::set_run(bool newval) {
    run = newval;
}
void fillpatargs::set_nword(uint64_t n) {
    nword = n;
}
void fillpatargs::set_fill(uint64_t f) {
    fill = f;
}
void fillpatargs::set_inc(uint64_t i) {
    inc = i;
}

fillpatargs::~fillpatargs() {
    delete pool;
}


fakerargs::fakerargs():
    rteptr( 0 ), buffer( 0 ), framepool( 0 )
{}

fakerargs::fakerargs(runtime* rte):
    rteptr( rte ), buffer( 0 ), framepool( 0 )
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


duplicatorargs::duplicatorargs(runtime* rte, unsigned int sz, unsigned int dup):
    rteptr(rte), pool(0), factor(dup), itemsz(sz)
{ ASSERT_NZERO(rteptr); ASSERT_COND(factor>0); ASSERT_COND(itemsz>0) }

duplicatorargs::~duplicatorargs() {
    delete pool;
}



diskreaderargs::diskreaderargs() :
    run( false ), repeat( false ), rteptr( 0 ), pool( 0 ), 
    allow_variable_block_size( false )
{}
diskreaderargs::diskreaderargs(runtime* r) :
    run( false ), repeat( false ), rteptr( r ), pool( 0 ),
    allow_variable_block_size( false )
{ ASSERT_NZERO(rteptr); }

void diskreaderargs::set_start( playpointer s ) {
    pp_start = s;
}
playpointer diskreaderargs::get_start() {
    return pp_start;
}
void diskreaderargs::set_end( playpointer e ) {
    pp_end = e;
}
playpointer diskreaderargs::get_end() {
    return pp_end;
}
void diskreaderargs::set_repeat( bool b ) {
    repeat = b;
}
void diskreaderargs::set_run( bool b ) {
    run = b;
}
void diskreaderargs::set_variable_block_size( bool b ) {
    allow_variable_block_size = b;
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
{ ASSERT_NZERO(rteptr); netparms = rteptr->netparms; }
networkargs::networkargs(runtime* r, const netparms_type& np):
    rteptr( r ), netparms( np )
{ ASSERT_NZERO(rteptr); }

fdreaderargs::fdreaderargs():
    fd( -1 ), doaccept( false ), 
    rteptr( 0 ), threadid( 0 ),
    blocksize( 0 ), pool( 0 ),
    start( 0 ), end( 0 ), finished( false ), run( false ), 
    max_bytes_to_cache( numeric_limits<uint64_t>::max() ),
    allow_variable_block_size( false )
{}
fdreaderargs::~fdreaderargs() {
    delete pool;
    delete threadid;
}
off_t fdreaderargs::get_start() {
    return start;
}
off_t fdreaderargs::get_end() {
    return end;
}
void fdreaderargs::set_start(off_t s) {
    start = s;
}
void fdreaderargs::set_end(off_t e) {
    end = e;
}
bool fdreaderargs::is_finished() {
    return finished;
}
void fdreaderargs::set_run(bool newval) {
    run = newval;
}
uint64_t fdreaderargs::get_bytes_to_cache() {
    return max_bytes_to_cache;
}
void fdreaderargs::set_bytes_to_cache(uint64_t b) {
    max_bytes_to_cache = b;
}
void fdreaderargs::set_variable_block_size( bool b ) {
    allow_variable_block_size = b;
}

buffererargs::buffererargs() :
    rte(0), bytestobuffer(0), bytestodrop(0)
{}
buffererargs::buffererargs(runtime* rteptr, unsigned int n) :
    rte(rteptr), bytestobuffer(n), bytestodrop(0)
{ ASSERT_NZERO(rteptr); }

unsigned int buffererargs::get_bufsize( void ) {
    return bytestobuffer;
}
void buffererargs::add_bufsize( unsigned int bytes ) {
    uint64_t btb = (uint64_t)bytestobuffer + bytes;
    if ( btb > numeric_limits<unsigned int>::max() ) {
        bytestobuffer = numeric_limits<unsigned int>::max();
    }
    else {
        bytestobuffer = btb;
    }
}
void buffererargs::dec_bufsize( unsigned int bytes ) {
    if ( bytes > bytestobuffer ) {
        bytes = bytestobuffer;
    }
    uint64_t btd = (uint64_t)bytestodrop + bytes;
    if ( btd > numeric_limits<unsigned int>::max() ) {
        bytestobuffer -= numeric_limits<unsigned int>::max() - bytestodrop;
        bytestodrop = numeric_limits<unsigned int>::max();
    }
    else {
        bytestodrop = btd;
        bytestobuffer -= bytes;
    }    
}


buffererargs::~buffererargs() {
}

timegrabber_type::timegrabber_type() {
    ::memset(&os_time, 0, sizeof(struct timespec));
    ::memset(&data_time, 0, sizeof(struct timespec));
}

timegrabber_type timegrabber_type::get_times( void ) {
    return *this;
}

timemanipulator_type::timemanipulator_type() {
    ::memset(&dt, 0, sizeof(struct timespec));
}

timemanipulator_type::timemanipulator_type( const struct timespec& ts ):
    dt( ts )
{}

splitterargs::splitterargs(runtime* rteptr,
                           const splitproperties_type& sp,
                           const headersearch_type& inhdr,
                           unsigned int naccumulate):
    rte( rteptr ),
    pool( 0 ),
    inputhdr( inhdr ),
    outputhdr( sp.outheader(inputhdr, naccumulate) ),
    splitprops( sp )
{ ASSERT_NZERO(rteptr); }

splitterargs::~splitterargs() {
    delete pool;
}


reframe_args::reframe_args(uint16_t sid, unsigned int br,
                           unsigned int ip, unsigned int op, unsigned int bpc, unsigned int bps):
    station_id(sid), pool(0),
    bitrate(br), input_size(ip), output_size(op),
    bits_per_channel(bpc), bits_per_sample(bps)
{ ASSERT_NZERO(bits_per_channel); ASSERT_NZERO(bits_per_channel);
  ASSERT_NZERO(input_size); ASSERT_NZERO(output_size);
  ASSERT_NZERO(bitrate);}

reframe_args::~reframe_args() {
    delete pool;
}


//
//    multi-destination stuff
//



multidestparms::multidestparms(runtime* rte, const chunkdestmap_type& cdm) :
    rteptr( rte ), chunkdestmap( cdm )
{ ASSERT_NZERO(rteptr); netparms = rteptr->netparms; }
multidestparms::multidestparms(runtime* rte, const chunkdestmap_type& cdm, const netparms_type& np) :
    rteptr( rte ), netparms( np ), chunkdestmap( cdm )
{ ASSERT_NZERO(rteptr); }

multifdargs::multifdargs(runtime* rte, const netparms_type& np) :
    rteptr( rte ), netparms( np )
{ ASSERT_NZERO(rteptr); }

multifdargs::~multifdargs() {
    fdreaderlist_type::iterator curfd;

    for( curfd=fdreaders.begin();
         curfd!=fdreaders.end();
         curfd++ )
            delete *curfd;
}

// Chunkdest-Map: maps chunkid (uint) => destination (string)
// First create the set of unique destinations
//   (>1 chunk could go to 1 destination)
// Then create those filedescriptors
// Then go through chunkdest-map again and build 
// a new mapping from destination => filedescriptor
// Build a return value of chunkid => filedescriptor
multifdargs* multiopener( multidestparms mdp ) {
    typedef std::map<std::string, int>  destfdmap_type;
    multifdargs*                      rv = new multifdargs( mdp.rteptr, mdp.netparms );
    destfdmap_type                    destfdmap;
    const std::string                 proto( mdp.netparms.get_protocol() );
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

            // If proto == rtcp and only one argument: it's port number, not host
            if( parts.size()>1 || (parts.size()==1 && proto=="rtcp")) {
                long int  v = -1;
                
                v = ::strtol(parts[ parts.size()-1 ].c_str(), 0, 0);

                ASSERT2_COND(v>0 && v<=USHRT_MAX, SCINFO("invalid portnumber " << parts[ parts.size()-1 ] ) );
                port = (unsigned short)v;
            } 
            if( proto=="rtcp" ) {
                const string  bind_ifaddr = (parts.size()>1?parts[0]:"");
                insres = destfdmap.insert( make_pair(curchunk->second, ::getsok(port, proto, bind_ifaddr)) );
            } else
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
    multifdargs*                      rv = new multifdargs(mdp.rteptr, mdp.netparms);
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

void header_stripper( inq_type<tagged<frame> >* inq, outq_type<tagged<frame> >* outq, sync_type<headersearch_type>* args) {
    const headersearch_type& hdr = *args->userdata;
    
    while( true ) {
        tagged<frame>  tf;
        if( inq->pop(tf)==false )
            break;
        frame&  iframe( tf.item );

        ASSERT2_COND( iframe.frametype==hdr.frameformat && iframe.framedata.iov_len==hdr.framesize,
                      SCINFO("expected " << hdr << " got " << iframe.frametype << 
                             "/" << iframe.ntrack << " tracks" ));
        iframe.framedata = iframe.framedata.sub(hdr.payloadoffset, hdr.payloadsize);
        // pass on only the payload
        //tagged<frame> tfout(tf.tag, frame(iframe.frametype, iframe));
        if( outq->push(tf)==false )
            break;
    }
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

    bool                 cancel;
    splitterargs*        splitargs = args->userdata;
    runtime*             rteptr    = (splitargs?splitargs->rte:0);
    tagged<frame>        tf;
    tag_state_map_type   tagstatemap;
    splitproperties_type splitprops = splitargs->splitprops;

    // Assert we have arguments
    ASSERT_NZERO( splitargs && rteptr );

    // Input- and output headertypes. We assume the frame is split in nchunk
    // equal parts, effectively reducing the number-of-tracks by a factor of
    // nchunk. 
    blockpool_type*          blkpool  = 0;
    const headersearch_type& inputheader  = splitargs->inputhdr;
    const headersearch_type& outputheader = splitargs->outputhdr;
    const unsigned int       nchunk       = splitprops.nchunk();
    const unsigned int       ch_len       = inputheader.payloadsize*outputheader.ntrack / inputheader.ntrack;
    //const unsigned int       ch_len       = inputheader.payloadsize/nchunk;
    const unsigned int       outputsize   = outputheader.payloadsize;
    const unsigned int       naccumulate  = outputsize/ch_len;
    //const unsigned int       naccumulate  = (nchunk * outputsize) / inputheader.payloadsize;
    const splitfunction      splitfn      = splitprops.fnptr();

    // Before crashing, at least tell what we think we're doing
    DEBUG(-1, "coalescing_splitter: starting up" << endl <<
              "    expect " << inputheader << endl <<
              "    payload [" << inputheader.payloadsize << "bytes] split into " << nchunk << " pieces" << endl <<
              "    accumulating " << naccumulate << " frames" << endl <<
              "    producing " << outputheader << endl);

    // We must prepare our blockpool depending on which data we expect to be
    // getting.
    // Mark K's sse-dechannelizing routines access 16 bytes past the end.
    // So we add extra bytes to the end
    SYNCEXEC(args,
             blkpool = splitargs->pool = new blockpool_type(outputsize+16, nchunk));

    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, splitprops.name(), 0));
    counter_type&   counter( rteptr->statistics.counter(args->stepid) );


    // Wait for an integral second boundary (or cancel) - only if it's real
    // data! [we take the inputheader to be valid as a signal for that]
    // With fill pattern we just wait for the first block to arrive since
    // most likely there won't be a valid timestamp in there anyway
    while( (cancel=(inq->pop(tf)==false))==false && inputheader.valid() &&
           tf.item.frametime.tv_nsec!=0 ) { };

    if( cancel ) {
        DEBUG(-1, "coalescing_splitter: cancelled whilst waiting for integral second boundary" << endl);
        return;
    }

    // *now* we can start doing stuff!
    do {
        if( !(tf.item.frametype==inputheader.frameformat &&
              ((inputheader.framesize>0 && tf.item.framedata.iov_len==inputheader.framesize) ||
              (tf.item.framedata.iov_len==inputheader.payloadsize)) )  ) {
            DEBUG(-1, "coalescing_splitter: expect " << inputheader
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
        tag_state&      tagstate( curtag->second );

        chunk = tagstate.chunk;
        splitfn((unsigned char*)tf.item.framedata.iov_base + inputheader.payloadoffset,
                inputheader.payloadsize,
                chunk[0], chunk[1], chunk[2], chunk[3],
                chunk[4], chunk[5], chunk[6], chunk[7], 
                chunk[8], chunk[9], chunk[10], chunk[11], 
                chunk[12], chunk[13], chunk[14], chunk[15]);

        // If we need to do another iteration, increment the pointers
        if( (++tagstate.fcount)<naccumulate ) {
            for(unsigned int tmpt=0; tmpt<nchunk; tmpt++)
                chunk[tmpt] += ch_len;
            continue;
        }

        // Ok now push the dechannelized frames onward
        block*       tagblock = tagstate.tagblock;
        unsigned int j;
        for(j=0; j<nchunk; j++)
            if( outq->push( tagged<frame>(curtag->first*nchunk + j,
                                          frame(outputheader.frameformat, outputheader.ntrack, tagstate.out_ts,
                                                tagblock[j].sub(0, outputsize))) )==false )
                break;

        if( j<nchunk ) {
            DEBUG(-1, "coalescing_splitter: failed to push channelized data. stopping." << endl);
            break;
        }
        counter += nchunk*outputsize;

        // And reset for the next iteration - ie erase the integration for
        // the current tag
        tagstatemap.erase(curtag);
    } while( inq->pop(tf) );
    DEBUG(2, "coalescing_splitter: done " << endl);
}


// Reframe to vdif - output the new frame as a blocklist:
// first the new header (VDIF) and then the datablock
void reframe_to_vdif(inq_type<tagged<frame> >* inq, outq_type<tagged<miniblocklist_type> >* outq, sync_type<reframe_args>* args) {
    typedef std::map<unsigned int, vdif_header>  tagheadermap_type;
    bool                    stop              = false;
    uint64_t                done              = 0;
    unsigned int            dataframe_length  = 0;
    unsigned int            chunk_duration_ns = 0;
    reframe_args*           reframe = args->userdata;
    tagged<frame>           tf;
    tagheadermap_type       tagheader;
    const unsigned int      bits_p_chan = reframe->bits_per_channel;
    const unsigned int      bitrate     = reframe->bitrate;
    const unsigned int      input_size  = reframe->input_size;
    const unsigned int      output_size = reframe->output_size;
    const tagremapper_type& tagremapper = reframe->tagremapper;
    const bool              doremap     = (tagremapper.size()>0);
    const tagremapper_type::const_iterator  endptr = tagremapper.end();
    tagheadermap_type::iterator             hdrptr;

    // If the output size is unconstrained (==-1), then we pass the
    // frames on unmodified
    if( output_size==(unsigned int)-1 )
        dataframe_length = input_size;

    // If dataframe_length not set yet, then, given input and 
    // maxpayloadsize compute how large each chunk must be
    // We're looking for the largest multiple of 8 that will divide our
    // input size into an integral number of outputs
    // Also assume that the caller means by "output_size" the maximum
    // *payload* size - i.e. the VDIF header already been accounted for
    for(unsigned int i=1; dataframe_length==0 && i<input_size; i++) {
        const unsigned int dfl = input_size/i;
        const bool         fit = (dfl>input_size?(dfl%input_size==0):(input_size%dfl==0));

        if( dfl%8==0 && fit && dfl<=output_size )
            dataframe_length = dfl;
    }

    ASSERT2_COND(dataframe_length!=0,
                 SCINFO("failed to find suitable VDIF dataframelength: input="
                        << input_size << ", output=" << output_size));

    // The blockpool only has to deliver the VDIF headers
    SYNCEXEC(args,
             reframe->pool = new blockpool_type(sizeof(vdif_header), 16));
    blockpool_type* pool = reframe->pool;

    DEBUG(-1, "reframe_to_vdif: VDIF dataframe_length = " << dataframe_length << " (input: " << input_size << ")" << endl <<
             "         total VDIF=" << dataframe_length+sizeof(vdif_header) << ", bitrate=" << bitrate << ", bits_per_channel=" << bits_p_chan << endl);

    // Wait for the first bit of data to come in
    if( inq->pop(tf)==false ) {
        DEBUG(1, "reframe_to_vdif: cancelled before beginning" << endl);
        return;
    }
    // Ok, we *have* a frame! Now we can initialize our VDIF epoch stuff
    struct tm klad;

    ::gmtime_r(&tf.item.frametime.tv_sec, &klad);
    const int epoch    = (klad.tm_year + 1900 - 2000)*2 + (klad.tm_mon>=6);

    // Now set the zero point of that epoch, 00h00m00s on the 1st day of
    // month 0 (Jan) or 6 (July)
    klad.tm_hour  = 0;
    klad.tm_min   = 0; 
    klad.tm_sec   = 0;
    klad.tm_mon   = (klad.tm_mon/6)*6;
    klad.tm_mday  = 1;
    const time_t    tm_epoch = ::mktime(&klad);

    DEBUG(3, "reframe_to_vdif: year=" << klad.tm_year << " epoch=" << epoch << ", tm_epoch=" << tm_epoch << endl);

    // By having waited for the first frame for setting up our timing,
    // our fast inner loop can be way cleanur!
    do {
        const block                      data( tf.item.framedata );
        unsigned int                     datathreadid;
        const unsigned int               last = data.iov_len;
        const struct timespec            time = tf.item.frametime;
        tagremapper_type::const_iterator tagptr = tagremapper.find(tf.tag);

        if( last!=input_size ) {
            DEBUG(-1, "reframe_to_vdif: got inputsize " << last << ", expected " << input_size << endl);
            continue;
        }

        // Deal with tag -> datathreadid mapping
        if( doremap && tagptr==endptr ) {
            // no entry for the current tag - discard data
            continue;
        }
        datathreadid = (doremap?tagptr->second:tf.tag);

        if( (hdrptr=tagheader.find(datathreadid))==tagheader.end() ) {
            pair<tagheadermap_type::iterator,bool> insres = tagheader.insert( make_pair(datathreadid,vdif_header()) );
            ASSERT2_COND( insres.second,
                          SCINFO("Failed to insert new VDIF header for datathread #" << datathreadid
                                 << " (tag:" << tf.tag << ")"));
            hdrptr = insres.first;

            // haven't seen this datathreadid before, must initialize VDIF
            // header
            vdif_header&  hdr( hdrptr->second /*tagheader[t]*/ );

            hdr.station_id      = reframe->station_id;
            hdr.thread_id       = (short unsigned int)(hdrptr->first & 0x3ff);
            hdr.data_frame_len8 = (unsigned int)(((dataframe_length+sizeof(vdif_header))/8) & 0x00ffffff);
            hdr.bits_per_sample = (unsigned char)((reframe->bits_per_sample - 1) & 0x1f);
            hdr.ref_epoch       = (unsigned char)(epoch & 0x3f);
        }

        // break up the frame into smaller bits?
        vdif_header&    hdr = hdrptr->second;

        // dataframes cannot span second boundaries so this can be done
        // easily outside the breaking-up loop
        hdr.epoch_seconds   = (unsigned int)((time.tv_sec - tm_epoch) & 0x3fffffff);

        // And compute the frameduration in nano-seconds.
        hdr.log2nchans    = ((unsigned int)(::log2f((float)tf.item.ntrack/bits_p_chan)) & 0x1f);
        chunk_duration_ns = (unsigned int)((((double)dataframe_length * 8)/((double)tf.item.ntrack*(double)bitrate))*1.0e9);

        for(unsigned int dfn=time.tv_nsec/chunk_duration_ns, pos=0;
             !stop && (pos+dataframe_length)<=last;
             dfn++, pos+=dataframe_length) {
            block           vdifh( pool->get() );

            hdr.data_frame_num = (unsigned int)(dfn & 0x00ffffff);

            // copy vdif header 
            ::memcpy(vdifh.iov_base, &hdr, sizeof(vdif_header));

            stop = (outq->push(tagged<miniblocklist_type>(hdrptr->first/*tf.tag*/,
                               miniblocklist_type(vdifh, data.sub(pos, dataframe_length))))==false);
        }
        done++;
    } while( !stop && inq->pop(tf) );
    DEBUG(1, "reframe_to_vdif: done " << done << " frames " << endl);
}

void multicloser( multifdargs* mfd ) {
    fdreaderlist_type::iterator curfd;

    for( curfd=mfd->fdreaders.begin();
         curfd!=mfd->fdreaders.end();
         curfd++ ) {
            DEBUG(4, "multicloser: closing filedescriptor " << *curfd << endl);
            ::close_filedescriptor( *curfd ); 
    }
}

