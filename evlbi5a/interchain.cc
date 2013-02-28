#include <interchain.h>
#include <iostream>
#include <mutex_locker.h>
#include <evlbidebug.h>
#include <set>

using namespace std;

DEFINE_EZEXCEPT(interchainexception)

// global queue object used to communicate between 2 chains (written into by queue_writers, read from by queue_readers)
struct interchain_queues_type {
    set< bqueue<block>* > queues;
    interchain_queues_type() {}
    ~interchain_queues_type() {
        // all registered queues should be removed at this stage, so check it
        if ( !queues.empty() ) {
            DEBUG( -1, "Error: not all interchain queues are removed at cleanup!" << endl);
            for ( set< bqueue<block>* >::iterator i = queues.begin();
                  i != queues.end();
                  i++) {
                delete *i;
            }
        }
    }
};
static interchain_queues_type interchain;
static pthread_rwlock_t interchain_lock = PTHREAD_RWLOCK_INITIALIZER;

bqueue<block>* request_interchain_queue() {
    bqueue<block>* q = new bqueue<block>();
    rw_write_locker locker( interchain_lock );
    EZASSERT2( interchain.queues.insert(q).second, interchainexception,
               EZINFO("Interchain queue is already registered?!?!") );
    // enable the queue to receive data, so it can jump into the middle
    // of a running transfer
    q->resize_enable_push( 1024 );
    return q;
}

void remove_interchain_queue( bqueue<block>* queue ) {
    rw_write_locker locker( interchain_lock );
    set< bqueue<block>* >::iterator i = interchain.queues.find(queue);
    EZASSERT2( i != interchain.queues.end(), interchainexception, 
               EZINFO("Failed to find queue to remove") );
    delete *i;
    interchain.queues.erase(i);
}

bool interchain_queues_push( block& b ) {
    rw_read_locker locker( interchain_lock );
    for ( set< bqueue<block>* >::iterator i = interchain.queues.begin();
          i != interchain.queues.end();
          i++ ) {
        if ( !(*i)->push( b ) ) {
            return false;
        }
    }
    return true;
}

void interchain_queues_try_push( block& b ) {
    rw_read_locker locker( interchain_lock );
    for ( set< bqueue<block>* >::iterator i = interchain.queues.begin();
          i != interchain.queues.end();
          i++ ) {
        (*i)->try_push( b );
    }
}

void interchain_queues_disable() {
    rw_read_locker locker( interchain_lock );
    for ( set< bqueue<block>* >::iterator i = interchain.queues.begin();
          i != interchain.queues.end();
          i++ ) {
        (*i)->disable();
    }
}

void interchain_queues_resize_enable_push( bqueue<block>::capacity_type newcap ) {
    rw_read_locker locker( interchain_lock );
    for ( set< bqueue<block>* >::iterator i = interchain.queues.begin();
          i != interchain.queues.end();
          i++ ) {
        (*i)->resize_enable_push( newcap );
    }
}
