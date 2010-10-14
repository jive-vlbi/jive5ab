// efficient producer/consumer queue for threads
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
#ifndef EVLBI5A_QUEUE_H
#define EVLBI5A_QUEUE_H

#include <queue>
#include <iostream>

// Include this for the PTHREAD_CALL* macros.
// They WILL throw if the pthread_* function inside it returns an errorcode.
#include <pthreadcall.h>


// An interthread queue storing up to 'capacity' elements of type 'Element'.
// Element must be copyable and assignable.
template <typename Element>
class bqueue {
    public:
        typedef std::queue<Element>            queue_type;
        typedef typename queue_type::size_type capacity_type;
        static const capacity_type             invalid_size = (capacity_type)-1;

        // create a disabled queue of capacity 0.
        // Your threads will not block on push() or pop() 
        // but will not be able to do so anyway.
        // Call 'enable()' with a capacity>0 for 
        // usable queue
        bqueue() {
            init(invalid_size);
        }

        // create a (possibly) fully enabled queue of capacity
        // 'cap'. You *can* pass '0' as capacity but don't
        // assume much will happen; threads attempting
        // to push()/pop() will block [rather than return
        // with a fault, as with a disabled queue].
        // You can, however, from another thread, call
        // enable() with a capacity>0 to get things going.
        bqueue(capacity_type cap) {
            init(cap);
        }

        // disable the queue: this means that all threads
        // waiting to push or pop will be signalled and
        // will return values to their callers indicating
        // that the queue was disabled:
        // push(): will return false [if not cancelled
        //         it will always return true as it will
        //         blocking wait until it *CAN* push]
        // pop():  Id. Only thread will block forever on
        //         a non-disabled queue until it *CAN* pop.
        void disable( void ) {
            // need mutex to safely change our state
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
            enable_push = false;
            enable_pop  = false;
            // AND CLEAR THE QUEUE!
            queue = queue_type();
            // broadcast that something happened to the queue
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return;
        }

        // delayed-disable the queue. Calling this
        // will disallow further push()ing onto the
        // queue immediately. Allow  popping until the
        // queue becomes empty, at which point it
        // will become completely disabled.
        void delayed_disable( void ) {
            // need mutex to safely change our state
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            enable_push = false;

            // if the queue is empty, we can disable the queue immediately
            if( queue.empty() )
                enable_pop = false;

            // and broadcast that something happened to the queue
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return;
        }

        // (re-)enable and clear the queue. Optionally, one can resize
        // the queue by passing a non-zero new capacity.
        void enable(void) {
            this->resize_enable(0);
        }
        void resize_enable(capacity_type newcap) {
            // need mutex to safely change state of the queue
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
            enable_push = true;
            enable_pop  = true;
            if( newcap )
                capacity = newcap;
            // start with a fresh, empty, queue!
            queue = queue_type();
            // and broadcast that something happened to the queue
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return;
        }

        // push(): only returns false is queue is disabled.
        //         Returns immediately if queue disabled or
        //         there is room to push. Otherwise wait
        //         indefinitely for space to become available
        //         or notification of cancellation of the queue.
        //         Note: a copy of b is pushed on the queue
        bool push( const Element& b ) {
            bool  did_push;

            // first things first ...
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            // whilst we have the mutex, tell the system
            // that there's another thread on the block 
            // waiting on this object
            numRegistered++;

            // wait until we can either push OR the queue is disabled
            //   (if necessary)
            while( enable_push && queue.size()>=capacity )
                PTHREAD_CALL( ::pthread_cond_wait(&condition, &mutex) );

            // Ok. There is something we can do.
            // Either the queue was cancelled (takes precedence)
            // or space to push came available.
            //
            // Save a copy of "enable_push" as returnvalue.
            // Returning "this->enable_push" _after_ unlocking
            // the mutex may result in corruption as, after we
            // unlocked the mutex, someone else may alter 
            // "this->enable_push" before we actually get round
            // to returning it to our caller.
            if( (did_push=enable_push)==true )
                queue.push( b );
            numRegistered--;
            // broadcast that something has happened to the
            // state of the queue: at least numRegistered has changed
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );

            return did_push;
        }

        // pop(): Wait indefinitely for something to be present
        //        in the queue or a queue-cancellation.
        //        If the queue already was disabled or there is already
        //        something to pop, the function return immediately
        //        (obviously).  Note: a copy of '.front()' is put into b.
        bool pop( Element& b ) {
            bool did_pop;

            // first things first ...
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            // another thread is waiting on this object
            numRegistered++;

            // wait for pop or until queue is disabled
            //   (if necessary)
            while( enable_pop && queue.empty() )
                PTHREAD_CALL( ::pthread_cond_wait(&condition, &mutex) );

            // ok. we have the mutex again and either:
            // * queue popping was disabled, or,
            // * queue became not empty
            if( (did_pop=enable_pop)==true ) {
                b = queue.front();
                queue.pop();
            }
            // take care of delayed disable: if enable_push=false and
            // queue.empty() => possibly delayed disable in effect.
            // Actually, it does not matter wether it was a delayed 
            // or blunt disable: if the condition holds, enable_pop
            // now MUST become false since only an enabled queue has
            // enable_push set to true.
            if( !enable_push && queue.empty() )
                enable_pop = false;

            numRegistered--;
            // broadcast (possible) waiting threads that
            // something happened to the state of the queue
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return did_pop;
        }

        // Destroy the queue.
        // First disable it, before destroying the resources.
        // This cannot deadlock :) - a thread, blocking waiting on
        // this object cannot execute the destructor because it,
        // well, is in a blocking wait for something else :)
        ~bqueue() {
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

    private:
        bool                   enable_push;
        bool                   enable_pop;
        queue_type             queue;
        unsigned int           numRegistered;
        pthread_cond_t         condition;
        pthread_mutex_t        mutex;
        capacity_type          capacity;

        // init with capacity 'cap'
        // Note: '0' is a valid size.
        void init(capacity_type cap) {
            capacity      = cap;
            enable_push   = enable_pop = (capacity!=invalid_size);
            numRegistered = 0;

            PTHREAD_CALL( ::pthread_mutex_init(&mutex, 0) );
            PTHREAD_CALL( ::pthread_cond_init(&condition, 0) ); 
        }

        // do not support copy/assignment
        // the functions are declared here, but NOT implemented
        // -> triggers a compile-time error if used.
        bqueue( const bqueue<Element>& );
        const bqueue<Element>& operator=( const bqueue<Element>& );
};



#endif
