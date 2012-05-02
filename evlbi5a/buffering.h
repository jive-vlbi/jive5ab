#ifndef JIVE5A_BUFFERING_H
#define JIVE5A_BUFFERING_H

#include <runtime.h>
#include <chain.h>
#include <block.h>
#include <blockpool.h>
#include <bqueue.h>
#include <threadfns.h>

struct queue_writer_args {
    queue_writer_args();
    queue_writer_args(runtime* r);

    ~queue_writer_args();

    runtime*        rteptr;
    blockpool_type* pool;
    bool            disable;
};

struct queue_reader_args {
    runtime* rteptr;
    blockpool_type*    pool;
    bool run;

    queue_reader_args();
    queue_reader_args(runtime* r);

    ~queue_reader_args();

    void set_run(bool val);
};

struct void_step_args {
};

void fifo_queue_writer(outq_type<block>*, sync_type<queue_writer_args>* );
void queue_reader(outq_type<block>*, sync_type<queue_reader_args>* );
void void_step(inq_type<block>*, sync_type<void_step_args>*);

void cancel_queue_readers(queue_reader_args*);

#endif
