// implementation
#include <bqueue.h>
#include <pthreadcall.h>

// the block thingy
block::block():
    iov_base( 0 ), iov_len( 0 )
{}

block::block(void* base, size_t sz):
    iov_base( base ), iov_len( sz )
{}

bool block::empty( void ) const {
    return (iov_base==0 && iov_len==0);
}


// The bqueue, a queue of blocks


bqueue::bqueue():
    enabled( false ), numRegistered( 0 ), capacity( 0 )
{
    // initialize the mutex with default attributes
    // throws if failure
    PTHREAD_CALL( ::pthread_mutex_init(&mutex, 0) );

    // id. for the condition variable.
    PTHREAD_CALL( ::pthread_cond_init(&condition, 0) ); 
}

bqueue::bqueue( const queue_type::size_type cap ):
    enabled( true ), numRegistered( 0 ), capacity( cap )
{
    // initialize the mutex with default attributes
    // throws if failure
    PTHREAD_CALL( ::pthread_mutex_init(&mutex, 0) );

    // id. for the condition variable.
    PTHREAD_CALL( ::pthread_cond_init(&condition, 0) ); 
}


// disable the queue
void bqueue::disable( void ) {
    // grab hold of mutex and change state of
    // the queue
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
    enabled = false;
    // and broadcast that something happened to the queue
    PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
    // done
    return;
}

void bqueue::enable( const queue_type::size_type newcap ) {
    // get mutex and change state of the queue
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
    enabled = true;
    if( newcap )
        capacity = newcap;
    // and broadcast that something happened to the queue
    PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
    // done
    return;
}

bool bqueue::push( const block& b ) {
    bool  dopush;

    // attempt to push a new block on the queue
    // 1) grab hold of the mutex
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

    // whilst we have the mutex, tell the system
    // that there's another thread on the block 
    // waiting on this object
    numRegistered++;

    // wait until we can either push OR the queue is disabled
    while( enabled && queue.size()>=capacity )
        PTHREAD_CALL( ::pthread_cond_wait(&condition, &mutex) );

    // ok. either enabled turned/was false OR
    // the queue became empty enough for us to push!
    // Save a *copy* of 'enabled' so that we return
    // the correct value [between the unlock() and
    // return below, the value of 'enabled' might change
    // but *we* don't want to reflect that]
    if( (dopush=enabled)==true )
        queue.push( b );
    numRegistered--;
    // broadcast that something has happened to the
    // state of the queue [at least numRegistered
    // has changed]
    PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );

    // and we're done
    return dopush;
}

// return a copy of the first element in the queue
// OR an empty one, if the queue is disabled
block bqueue::pop( void ) {
    block   rv; // default block() is already 'empty'

    // grab the mutex
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
    
    // tell system that another thread is waiting
    // on this object
    numRegistered++;

    // wait until we can pop or until queue is
    // disabled
    while( enabled && queue.empty() )
        PTHREAD_CALL( ::pthread_cond_wait(&condition, &mutex) );

    // ok. we have the mutex again and either:
    // * queue was disabled, or,
    // * queue became not empty
    if( enabled ) {
        rv = queue.front();
        queue.pop();
    }
    numRegistered--;
    // broadcast (possible) waiting threads that
    // something happened to the state of the queue
    // (at least numRegistered has changed)
    PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );

    return rv;
}



// destroy the queue.
// First disable it, before destroying the resources.
// This cannot lock :) - a thread, blocking waiting on
// this object cannot execute the destructor because it,
// well, is in a blocking wait for something else :)
bqueue::~bqueue() {
    // ...
    this->disable();

    // Great. Wait until nobody's registered anymore
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
    while( numRegistered>0 )
        PTHREAD_CALL( ::pthread_cond_wait(&condition, &mutex) );
    // Ok, all threads that were blocking on the queue have left the building.
    // now we can safely destroy the resources. We *have* the mutex.
    PTHREAD_CALL( ::pthread_cond_destroy(&condition) );
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
    PTHREAD_CALL( ::pthread_mutex_destroy(&mutex) );
}
