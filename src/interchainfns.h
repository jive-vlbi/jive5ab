#ifndef JIVE5A_INTERCHAINFNS_H
#define JIVE5A_INTERCHAINFNS_H

#include <runtime.h>
#include <chain.h>
#include <block.h>
#include <blockpool.h>

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

void queue_writer(inq_type<block>*, sync_type<queue_writer_args>* );

// two version of queue reader, the first resizes to the requested blocksize
// the stupid version does not
void queue_reader(outq_type<block>*, sync_type<queue_reader_args>* );
void stupid_queue_reader(outq_type<block>*, sync_type<queue_reader_args>* );

void queue_forker(inq_type<block>*, outq_type<block>*, sync_type<queue_forker_args>*);

void cancel_queue_reader(queue_reader_args*);

// When registered as chain finalizer it will remove the
// interchain queue from the runtime - this releases
// the interchain resources as soon as possible
void finalize_queue_reader(runtime*);

#endif
