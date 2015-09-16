#include <interchainfns.h>
#include <interchain.h>
#include <threadfns.h> // for emergency_type
#include <sciprint.h>  // for byteprint()
#include <iostream>
#include <memory>

using namespace std;



void finalize_queue_reader(runtime* rteptr) {
    if( !rteptr ) {
        DEBUG(-1, "finalize_queue_reader: no runtime?!" << endl);
        return;
    }
    // Ok, we have pointer-to-runtime
    if( rteptr->interchain_source_queue ) {
        remove_interchain_queue( rteptr->interchain_source_queue );
        // If that succeeded without throwing, we may rest assured that
        // the interchain queue was removed from the system and as such
        // the pointer can be set to 0; we don't need to reference the
        // object anymore.
        rteptr->interchain_source_queue = 0;
    }
}


#define MIN(x, y) ((x) < (y) ? (x) : (y))

// make the interchain queue contain at max 512 MB, by default
const unsigned int INTERCHAIN_QUEUE_SIZE = 512 * 1024 * 1024; 

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
    // note: we must have at least 1 entry! (With the advent of FlexBuff,
    // 'blocksize' could become *huge* such that
    // INTERCHAIN_QUEUE_SIZE/blocksize would become 0 ...)
    interchain_queues_resize_enable_push( std::max(INTERCHAIN_QUEUE_SIZE / blocksize, 1u) );
    args->userdata->disable = true;

    DEBUG(0, "queue_writer: starting" << endl);
    do {
        block b;
        if ( !inq->pop(b) ) {
            break;
        }
        counter += b.iov_len;
        interchain_queues_try_push(b);
    } while(true);
    DEBUG(0, "queue_writer: stopping" << endl);
}

void queue_reader(outq_type<block>* outq, sync_type<queue_reader_args>* args) {
    // Make sure we're not 'made out of 0-pointers'
    // apart from the interchain-source-queue; that one MUST be 0
    ASSERT_COND( args && 
                 args->userdata && 
                 args->userdata->rteptr && 
                 args->userdata->rteptr->interchain_source_queue==0 );
    queue_reader_args* qargs = args->userdata;
    runtime* rteptr = qargs->rteptr;
    
    RTEEXEC(*rteptr, rteptr->sizes.validate());
    const unsigned int out_blocksize = rteptr->sizes[constraints::blocksize];
    ASSERT_COND(out_blocksize > 0);

    SYNCEXEC(args, qargs->pool = new blockpool_type(out_blocksize, 16));

     // Request a counter for counting into
    RTEEXEC(*rteptr,
            rteptr->statistics.init(args->stepid, "QueueReaderV1", 0));

    // Provide unconditional and unlocked access to a counter
    volatile int64_t& counter( rteptr->statistics.counter(args->stepid) );

    bool         stop;
    unsigned int nrestart = 0;

    args->lock();
    while( !qargs->run && !args->cancelled )
        args->cond_wait();
    // Ah. At least one of the conditions was met.
    stop     = args->cancelled;
    args->unlock();

    // Premature cancellation?
    if( stop ) {
        DEBUG(0, "queue reader v1: cancelled before start" << endl);
        return;
    }

    // Now we are really going to read data; better have a queue then!
    // the request_interchain_queue() always succeeds or throws an exception
    bqueue<block>* interchain_queue = rteptr->interchain_source_queue = request_interchain_queue();

    // Now we can indicate we're running!
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.clr(wait_flag).set(run_flag));

    // Inner loop drains until either our transfer stops or the
    // interchainqueue is disabled
    //
    // Outer loop only finishes if *our* transfer is cancelled
    while( qargs->run && !stop && !args->cancelled ) {
        typedef std::list<block>   m_blocklist_type;
        // Perform a (re) start
        interchain_queue->enable_pop_only();

        DEBUG(0, "queue_reader v1: starting loop #" << nrestart << endl);

        // Now drain our interchain_queue
        unsigned int     nAvail = 0;
        m_blocklist_type blocklist;

        while( true ) {
            if( nAvail<out_blocksize ) {
                // Not enough bytes yet
                block   tmp;

                // Fail to pop from interchain queue only means that the source
                // transfer has stopped, so only break inner loop. 
                if( interchain_queue->pop(tmp)==false )
                    break;
                // And append it - prevent 0-sized blocks in the list
                if( tmp.iov_len ) {
                    blocklist.push_back(tmp);
                    nAvail += tmp.iov_len;
                }
                // Try again?
                continue;
            }

            // Ok. If the first block does not have enough bytes we must
            // assemble a new block, otherwise cut a piece out of it.
            // Note: we can guarantee that blocklist is non-empty at this
            // point - out_blocksize guaranteed > 0, nAvail guaranteed >=
            // out_blocksize 
            block                       outblock;
            m_blocklist_type::iterator  ptr = blocklist.begin();

            if( ptr->iov_len==out_blocksize ) {
                // simplest case: we just pass on the block unmodified and
                // remove it from our list
                outblock = *ptr;
                blocklist.erase( ptr );
            } else if( ptr->iov_len>out_blocksize ) {
                // extract the requested amount of bytes from the block,
                // keep the remainder of the bytes as the first entry in our list
                outblock = ptr->sub(0, out_blocksize);
                *ptr     = ptr->sub(out_blocksize, ptr->iov_len - out_blocksize);
            } else {
                // We have to assemble a couple of blocks together into a
                // new one, removing all blocks that we fully copied and
                // keeping a possible partial last block
                unsigned int    nCopied = 0;
                // Need a new block to aggregate smaller blocks into
                outblock = qargs->pool->get();

                // Gobble up enough blocks from the list until we
                // have a full output block.
                // Note: don't have to check for 'ptr==blocklist.end()'
                // because we're guaranteed to find enough blocks for
                // out_blocksize bytes
                while( nCopied<out_blocksize ) {
                    // always start at begin of list
                    ptr = blocklist.begin();
                    const size_t          n = std::min((size_t)(out_blocksize-nCopied), ptr->iov_len);

                    ::memcpy((unsigned char*)outblock.iov_base + nCopied, ptr->iov_base, n);
                    nCopied += n;

                    // If this condition holds, there's more bytes in the
                    // current block than we actually needed. So we cut off
                    // what we've used so far and keep the rest. It should
                    // become the new head-of-list.
                    // Otherwise all bytes in the current block were copied
                    // and the block can be removed from our list.
                    if( n<ptr->iov_len )
                        *ptr = ptr->sub(n, ptr->iov_len - n);
                    else
                        blocklist.erase( ptr );
                }
            }

            // Hoorah. 'outblock', wherever it came from, can be pushed
            // downstream
            if( (stop=(outq->push(outblock)==false))==true )
                break;
            // Number of available bytes has decreased
            nAvail  -= out_blocksize;
            counter += out_blocksize;
        }
        nrestart++;
    }
    DEBUG(0, "queue_reader v1: stopping" << endl);
    qargs->finished = true;
}


void stupid_queue_reader(outq_type<block>* outq, sync_type<queue_reader_args>* args) {
    // Make sure we're not 'made out of 0-pointers'
    // apart from the interchain-source-queue; that one MUST be 0
    ASSERT_COND( args && 
                 args->userdata && 
                 args->userdata->rteptr && 
                 args->userdata->rteptr->interchain_source_queue==0 );
    queue_reader_args* qargs = args->userdata;
    runtime* rteptr = qargs->rteptr;
    
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

    // Now we are really going to read data; better have a queue then!
    // the request_interchain_queue() always succeeds or throws an exception
    bqueue<block>* interchain_queue = rteptr->interchain_source_queue = request_interchain_queue();

    // Now we can indicate we're running!
    RTEEXEC(*rteptr,
            rteptr->transfersubmode.clr(wait_flag).set(run_flag));
    
    DEBUG(0, "stupid_queue_reader: starting" << endl);
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
    DEBUG(0, "stupid_queue_reader: stopping, read " << counter << " (" << byteprint((double)counter, "byte") << ")" << endl);
    qargs->finished = true;
}

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
    // note: we must have at least 1 entry! (With the advent of FlexBuff,
    // 'blocksize' could become *huge* such that
    // INTERCHAIN_QUEUE_SIZE/blocksize would become 0 ...)
    interchain_queues_resize_enable_push( std::max(INTERCHAIN_QUEUE_SIZE / blocksize, 1u) );
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
    // Interchain-source-queue not there is not an error anymore
    ASSERT_COND( args && args->rteptr );
    //ASSERT_COND( args->rteptr && args->rteptr->interchain_source_queue );
    if( args->rteptr->interchain_source_queue )
        args->rteptr->interchain_source_queue->disable_pop();
}

