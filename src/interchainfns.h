// Copyright (C) 2007-2019 Harro Verkouter
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
#ifndef JIVE5A_INTERCHAINFNS_H
#define JIVE5A_INTERCHAINFNS_H

#include <runtime.h>
#include <chain.h>
#include <block.h>
#include <blockpool.h>
#include <threadfns.h>

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
void tagged_queue_forker(inq_type< tagged<block> >*, outq_type< tagged<block> >*, sync_type<queue_forker_args>*);

void cancel_queue_reader(queue_reader_args*);

// When registered as chain finalizer it will remove the
// interchain queue from the runtime - this releases
// the interchain resources as soon as possible
void finalize_queue_reader(runtime*);

#endif
