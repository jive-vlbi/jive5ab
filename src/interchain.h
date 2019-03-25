#ifndef JIVE5A_INTERCHAIN_H
#define JIVE5A_INTERCHAIN_H

#include <block.h>
#include <bqueue.h>
#include <ezexcept.h>

DECLARE_EZEXCEPT(interchainexception)

// functions to setup queues used by thread functions to communicate data between runtimes/chains
bqueue<block>* request_interchain_queue();
void remove_interchain_queue( bqueue<block>* queue );

// push on all interchain queues, a blocking version and a non-blocking,
// note that the blocking version blocks on ALL queues and returns true iff all pushes return true
bool interchain_queues_push( block& b );
void interchain_queues_try_push( block& b );

// disable all interchain queues
void interchain_queues_disable();
// enable pushing on interchain queues
void interchain_queues_resize_enable_push( bqueue<block>::capacity_type newcap );
#endif
