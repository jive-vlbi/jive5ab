#include <interchainfns.h>
#include <interchain.h>
#include <threadfns.h> // for emergency_type
#include <iostream>
#include <memory>

using namespace std;

#define MIN(x, y) ((x) < (y) ? (x) : (y))

// make the interchain queue contain at max 512 MB, by default
const unsigned int INTERCHAIN_QUEUE_SIZE = 512 * 1024 * 1024; 

fifo_queue_writer_args::fifo_queue_writer_args() : rteptr(NULL), pool(NULL), disable(false) {}
fifo_queue_writer_args::fifo_queue_writer_args(runtime* r) : rteptr(r), pool(NULL), disable(false) {}
fifo_queue_writer_args::~fifo_queue_writer_args() {
    // clean up
    if ( disable ) {
        interchain_queues_disable();
    }
    delete pool;
}

queue_writer_args::queue_writer_args() : rteptr(NULL), disable(false) {}
queue_writer_args::queue_writer_args(runtime* r) : rteptr(r), disable(false) {}
queue_writer_args::~queue_writer_args() {
    // clean up
    if ( disable ) {
        interchain_queues_disable();
    }
}

queue_reader_args::queue_reader_args() : rteptr(NULL), pool(NULL), run(false), reuse_blocks(false), finished(false) {}
queue_reader_args::queue_reader_args(runtime* r) : rteptr(r), pool(NULL), run(false), reuse_blocks(false), finished(false) {}
queue_reader_args::~queue_reader_args() {
    delete pool;
}
void queue_reader_args::set_run(bool val) {
    run = val;
}
bool queue_reader_args::is_finished() {
    return finished;
}

queue_forker_args::queue_forker_args() : rteptr(NULL), disable(false) {}
queue_forker_args::queue_forker_args(runtime* r) : rteptr(r), disable(false) {}
queue_forker_args::~queue_forker_args() {
    // clean up
    if ( disable ) {
        interchain_queues_disable();
    }
}

void fifo_queue_writer(outq_type<block>*, sync_type<fifo_queue_writer_args>* args) {
    // If we must empty the FIFO we must read the data to somewhere so
    // we allocate an emergency block, 
    // for emptying the fifo if downstream isn't fast enough
    typedef emergency_type<256000>  em_block_type;
    const DWORDLONG                 hiwater = (512*1024*1024)/2;
    std::auto_ptr<em_block_type>    eb(new em_block_type);

    // Make sure we're not 'made out of 0-pointers'
    ASSERT_COND( args && args->userdata && args->userdata->rteptr );

    // automatic variables
    runtime*           rteptr = args->userdata->rteptr;
    SSHANDLE           sshandle;
    DWORDLONG          fifolen;
    fifo_queue_writer_args* qwargs = args->userdata;

    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int blocksize = rteptr->sizes[constraints::blocksize];

    // allocate enough working space.
    SYNCEXEC( args,
              qwargs->pool = new blockpool_type(blocksize, 16) );

    // enable the queue between the chains, remember to disable it when done
    interchain_queues_resize_enable_push(INTERCHAIN_QUEUE_SIZE / blocksize);
    qwargs->disable = true;

    // Request a counter for counting into
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "Fifo", 0));

    // Provide unconditional and unlocked access to A counter,
    // which, if everything goes to plan, might even be THE counter
    volatile int64_t& counter( rteptr->statistics.counter(args->stepid) );

    RTEEXEC(*rteptr,
            sshandle = rteptr->xlrdev.sshandle());

    DEBUG(0, "fifo_queue_writer: starting" << endl);

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
            cerr << "above hiwater" << endl;
            if( ::XLRReadFifo(sshandle, eb->buf, sizeof(eb->buf), 0)!=XLR_SUCCESS ) {
                do_xlr_unlock();
                throw xlrexception("Failure to XLRReadFifo whilst trying "
                        "to get below hiwater mark!");
            }
            cerr << "read emergency block" << endl;
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
        block   b = qwargs->pool->get();

        XLRCALL( ::XLRReadFifo(sshandle, (READTYPE*)b.iov_base, b.iov_len, 0) );

        // indicate we've read another 'blocksize' amount of
        // bytes from the StreamStor
        counter += b.iov_len;

        interchain_queues_try_push(b);
    }
    DEBUG(0, "fifo_queue_writer: stopping" << endl);
}

void queue_writer(inq_type<block>* inq, sync_type<queue_writer_args>* args) {
    ASSERT_COND( args && args->userdata && args->userdata->rteptr );
    queue_writer_args* qwargs = args->userdata;
    runtime* rteptr = qwargs->rteptr;

    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int blocksize = rteptr->sizes[constraints::blocksize];
    // Request a counter for counting into
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "MemWriter", 0));
    volatile int64_t& counter( rteptr->statistics.counter(args->stepid) );

    // enable the queue between the chains, remember to disable it when done
    interchain_queues_resize_enable_push(INTERCHAIN_QUEUE_SIZE / blocksize);
    args->userdata->disable = true;

    do {
        block b;
        if ( !inq->pop(b) ) {
            break;
        }
        counter += b.iov_len;
        interchain_queues_try_push(b);
    } while(true);

}

void queue_reader(outq_type<block>* outq, sync_type<queue_reader_args>* args) {
    // Make sure we're not 'made out of 0-pointers'
    ASSERT_COND( args && 
                 args->userdata && 
                 args->userdata->rteptr && 
                 args->userdata->rteptr->interchain_source_queue );
    queue_reader_args* qargs = args->userdata;
    runtime* rteptr = qargs->rteptr;
    bqueue<block>* interchain_queue = rteptr->interchain_source_queue;
    
    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int out_blocksize = rteptr->sizes[constraints::blocksize];
    ASSERT_COND(out_blocksize > 0);

    SYNCEXEC(args, qargs->pool = new blockpool_type(out_blocksize, 16));

     // Request a counter for counting into
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "QueueReader", 0));

    // Provide unconditional and unlocked access to a counter
    volatile int64_t& counter( rteptr->statistics.counter(args->stepid) );

    bool stop;
    args->lock();
    while( !qargs->run && !args->cancelled )
        args->cond_wait();
    // Ah. At least one of the conditions was met.
    stop     = args->cancelled;
    args->unlock();

    // Premature cancellation?
    if( stop ) {
        DEBUG(0, "queue reader: cancelled before start" << endl);
        return;
    }

    // Now we can indicate we're running!
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.clr(wait_flag).set(run_flag));

    while (!stop) {
        interchain_queue->enable_pop_only();
        if (!qargs->run || args->cancelled) break;
        block b;
        unsigned int read_index = 0; // first unused byte in first block in buffer
        std::queue<block> blocks_buffer;
        unsigned long int bytes_in_buffer = 0;
        DEBUG(0, "queue_reader: (re-)starting" << endl);
        bool interchain_queue_enabled = true;
        while (interchain_queue_enabled && !stop) {
            // read from queue till we have enough bytes to form a new output block
            if (bytes_in_buffer < out_blocksize) {
                if (!interchain_queue->pop(b)) {
                    interchain_queue_enabled = false;
                }
                else {
                    blocks_buffer.push(b);
                    bytes_in_buffer += b.iov_len;
                }
            }
            // now form the new blocks
            else {
                ASSERT_COND(!blocks_buffer.empty());
                b = blocks_buffer.front();
                if ( (qargs->reuse_blocks) && (b.iov_len - read_index >= out_blocksize)) {
                    // can request a slice of the current block
                    if (!outq->push(b.sub(read_index, out_blocksize))) {
                        stop = true;
                    }
                    else {
                        counter += out_blocksize;
                        read_index += out_blocksize;
                        bytes_in_buffer -= out_blocksize;
                        if (b.iov_len == read_index) {
                            blocks_buffer.pop();
                            read_index = 0;
                        }
                    }
                }
                else {
                    block out_block = qargs->pool->get();
                    unsigned int long bytes_copied = 0;
                    while (bytes_copied < out_blocksize) {
                        unsigned int long bytes_to_copy = MIN(b.iov_len - read_index, out_blocksize - bytes_copied);
                        memcpy(((char*)out_block.iov_base) + bytes_copied, 
                               ((char*)b.iov_base) + read_index,
                               bytes_to_copy);
                        read_index += bytes_to_copy;
                        bytes_in_buffer -= bytes_to_copy;
                        bytes_copied += bytes_to_copy;
                        if (read_index == b.iov_len) {
                            blocks_buffer.pop();
                            read_index = 0;
                            if (!blocks_buffer.empty()) {
                                b = blocks_buffer.front();
                            }
                            else {
                                ASSERT_COND(bytes_copied == out_blocksize);
                            }
                        }
                    }
                    if (!outq->push(out_block)) {
                        stop = true;
                    }
                    else {
                        counter += out_blocksize;
                    }
                }
            }
        }
    }
    DEBUG(0, "queue_reader: stopping" << endl);
    qargs->finished = true;
}

void stupid_queue_reader(outq_type<block>* outq, sync_type<queue_reader_args>* args) {
    // Make sure we're not 'made out of 0-pointers'
    ASSERT_COND( args && 
                 args->userdata && 
                 args->userdata->rteptr && 
                 args->userdata->rteptr->interchain_source_queue );
    queue_reader_args* qargs = args->userdata;
    runtime* rteptr = qargs->rteptr;
    bqueue<block>* interchain_queue = rteptr->interchain_source_queue;
    
     // Request a counter for counting into
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "StupidQReader", 0));

    // Provide unconditional and unlocked access to a counter
    volatile int64_t& counter( rteptr->statistics.counter(args->stepid) );

    bool stop;
    args->lock();
    while( !qargs->run && !args->cancelled )
        args->cond_wait();
    // Ah. At least one of the conditions was met.
    stop = args->cancelled;
    args->unlock();

    // Premature cancellation?
    if( stop ) {
        DEBUG(0, "stupid queue reader: cancelled before start" << endl);
        return;
    }

    // Now we can indicate we're running!
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.clr(wait_flag).set(run_flag));
    
    bool interchain_queue_enabled;
    while (!stop) {
        interchain_queue->enable_pop_only();
        if (!qargs->run || args->cancelled) break;
        interchain_queue_enabled = true;
        block b;
        while (interchain_queue_enabled && !stop) {
            if (!interchain_queue->pop(b)) {
                interchain_queue_enabled = false;
            }
            else {
                if (!outq->push(b)) {
                    stop = true;
                }
                else {
                    counter += b.iov_len;
                }
            }
        }
    }
    DEBUG(0, "stupid_queue_reader: stopping" << endl);
    qargs->finished = true;
}

void void_step(inq_type<block>*) {}

void queue_forker(inq_type<block>* inq, outq_type<block>* outq, sync_type<queue_forker_args>* args) {
    ASSERT_COND( args && args->userdata && args->userdata->rteptr );

    runtime* rteptr = args->userdata->rteptr;

    // Request a counter for counting into
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "Forker", 0));
    volatile int64_t& counter( rteptr->statistics.counter(args->stepid) );

    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int blocksize = rteptr->sizes[constraints::blocksize];

    // enable the queue between the chains, remember to disable it when done
    interchain_queues_resize_enable_push(INTERCHAIN_QUEUE_SIZE / blocksize);
    args->userdata->disable = true;

    do {
        block b;
        if (!inq->pop(b) || !outq->push(b)) {
            break;
        }
        counter += b.iov_len;
        interchain_queues_try_push(b);
    } while(true);

}

void cancel_queue_reader(queue_reader_args* args) {
    args->set_run(false);
    ASSERT_COND( args->rteptr && args->rteptr->interchain_source_queue );
    args->rteptr->interchain_source_queue->disable_pop();
}

