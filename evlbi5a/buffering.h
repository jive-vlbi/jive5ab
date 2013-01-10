#ifndef JIVE5A_BUFFERING_H
#define JIVE5A_BUFFERING_H

#include <runtime.h>
#include <chain.h>
#include <block.h>
#include <blockpool.h>
#include <bqueue.h>
#include <threadfns.h>

// functions to setup queues used by thread functions below to communicate data between runtimes/chains
void init_interchain_queues(unsigned int len);
bqueue<block>& get_interchain_queue(unsigned int index);

struct fifo_queue_writer_args {
    fifo_queue_writer_args();
    fifo_queue_writer_args(runtime* r);

    ~fifo_queue_writer_args();

    runtime*        rteptr;
    blockpool_type* pool;
    bool            disable;
};

struct queue_writer_args {
    queue_writer_args();
    queue_writer_args(runtime* r);

    ~queue_writer_args();

    runtime*        rteptr;
    bool            disable;
};

struct queue_reader_args {
    runtime* rteptr;
    blockpool_type*    pool;
    bool run;
    bool reuse_blocks; // reuse blocks from interchain queue in this chain (if true), or always make a local copy (if false)
    bool finished;

    queue_reader_args();
    queue_reader_args(runtime* r);

    ~queue_reader_args();

    void set_run(bool val);
    bool is_finished();
};

struct queue_forker_args {
    runtime* rteptr;
    bool     disable;
    queue_forker_args();
    queue_forker_args(runtime* r);
    ~queue_forker_args();
};

void fifo_queue_writer(outq_type<block>*, sync_type<fifo_queue_writer_args>* );
void queue_writer(inq_type<block>*, sync_type<queue_writer_args>* );

// two version of queue reader, the first resizes to the requested blocksize
// the stupid version does not
void queue_reader(outq_type<block>*, sync_type<queue_reader_args>* );
void stupid_queue_reader(outq_type<block>*, sync_type<queue_reader_args>* );
void void_step(inq_type<block>*);
void queue_forker(inq_type<block>*, outq_type<block>*, sync_type<queue_forker_args>*);

void cancel_queue_reader(queue_reader_args*);

#endif
