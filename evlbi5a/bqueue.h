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
#include <time.h>
#include <errno.h>

// Include this for the PTHREAD_CALL* macros.
// They WILL throw if the pthread_* function inside it returns an errorcode.
#include <pthreadcall.h>

enum pop_result_type { pop_success, pop_timeout, pop_disabled };
enum push_result_type { push_success, push_overflow, push_disabled };

// Inside the push() and pop() methods, which are called a bazillion
// times/second, the PTHREAD_CALL() macro is WAY to expensive. (Each
// invocation creates a std::string + some more stuff). Great for
// non-high-performance bits [gives you very detailed info what failed where
// and why]. So, for the high-volume bits we fall back to this one that just
// does the systemcall and throws a generic error if it fails.
// Can't have everything - both speed & copious debug.
#define FASTPTHREAD_CALL(p) if(p) throw pthreadexception(std::string(#p));

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
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_push) );
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_pop) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return;
        }

        // delayed-disable the queue. Calling this
        // will disallow further push()ing (so we only broadcast the threads
        // blocked on the wait-to-push condition) onto the
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
            // those waiting to be able to push should be told off.
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_push) );
            // if the queue IS already empty, we may as well signal those
            // waiting to pop that it ain't gonna happen anymore
            if( enable_pop==false )
                PTHREAD_CALL( ::pthread_cond_broadcast(&condition_pop) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return;
        }

        // disable the popping end of the queue
        // pushing elements is still allowed, but of course this will
        // fill up the queue quickly
        // note: does NOT reflect delayed_disable, by disabling pushing
        // when the queue is full
        void disable_pop() {
            // need mutex to safely change our state
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
            enable_pop  = false;
            // broadcast that something happened to the queue
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_pop) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
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
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_push) );
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_pop) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return;
        }

        // enable the push side of the queue only
        void resize_enable_push(capacity_type newcap) {
            // need mutex to safely change state of the queue
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
            enable_push = true;
            if( newcap )
                capacity = newcap;
            // start with a fresh, empty, queue!
            queue = queue_type();
            // and broadcast that something happened to the queue
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_push) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return;
        }

        // enable the pop end of the queue only
        void enable_pop_only() {
            // need mutex to safely change our state
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
            enable_pop = true;
            // broadcast that something happened to the queue
            PTHREAD_CALL( ::pthread_cond_broadcast(&condition_pop) );
            PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );            
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
            FASTPTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            // whilst we have the mutex, tell the system
            // that there's another pusher on the block,
            // waiting on this object
            nPush++;

            // wait until we can either push OR the queue is disabled
            //   (if necessary)
            while( enable_push && queue.size()>=capacity )
                FASTPTHREAD_CALL( ::pthread_cond_wait(&condition_push, &mutex) );

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
            nPush--;
            // If there are poppers blocked and we pushed let's unlock one
            // of them
            if( nPop && did_push )
                FASTPTHREAD_CALL( ::pthread_cond_signal(&condition_pop) );
            FASTPTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );

            return did_push;
        }

        //try_ push(): 
        //   Try to push the element onto the queue.
        //   Will always return immediately.
        //   If the queue is disabled, return push_disabled.
        //   If there is no room, will return push_overflow.
        //   Otherwise, the element is put onto the queueu
        //   and push_success is returned.
        bool try_push( const Element& b ) {
            push_result_type ret;
            // first things first ...
            FASTPTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            if ( !enable_push ) {
                ret = push_disabled;
            }
            else if ( queue.size()>=capacity ) {
                ret = push_overflow;
            }
            else {
                ret = push_success;
                queue.push( b );

                // If there are poppers blocked and we pushed let's unlock one
                // of them
                if( nPop ) {
                    FASTPTHREAD_CALL( ::pthread_cond_signal(&condition_pop) );
                }
            }
            FASTPTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );

            return ret;
        }

        // pop(): Wait indefinitely for something to be present
        //        in the queue or a queue-cancellation.
        //        If the queue already was disabled or there is already
        //        something to pop, the function return immediately
        //        (obviously).  Note: a copy of '.front()' is put into b.
        bool pop( Element& b ) {
            bool did_pop;

            // first things first ...
            FASTPTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            // another popper is waiting on this object
            nPop++;

            // wait until we can pop or until queue is disabled
            //   (if necessary)
            while( enable_pop && queue.empty() )
                FASTPTHREAD_CALL( ::pthread_cond_wait(&condition_pop, &mutex) );

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

            nPop--;

            // We only ever ever wake up a pusher IF it makes sense to wake
            // one. Sense is:
            //    * there actually IS potentially someone waiting to push
            //      (nPush>0)
            //    * we actually popped (did_pop)
            // We must signal those blocked threads since if we don't then
            // they will never re-evaluate their condition to see that they
            // can actually push, even if it was us who actually emptied the
            // queue. Those blocking threads don't wake themselves up you
            // know. 
            // It is sufficient to just wake up one of them.
            if( nPush && did_pop )
                FASTPTHREAD_CALL( ::pthread_cond_signal(&condition_push) );
            FASTPTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            return did_pop;
        }

        // pop(): Wait until specified time for something to be present
        //        in the queue or a queue-cancellation.
        //        If the queue already was disabled or there is already
        //        something to pop, the function return immediately
        //        (obviously).  Note: a copy of '.front()' is put into b.
        pop_result_type pop( Element& b, const struct timespec& absolute_time ) {
            // first things first ...
            FASTPTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            // another thread is waiting on this object
            nPop++;

            // wait for pop or until queue is disabled
            //   (if necessary)
            int timed = 0;
            while( enable_pop && queue.empty() && timed != ETIMEDOUT) {
                PTHREAD_TIMEDWAIT( (timed = ::pthread_cond_timedwait(&condition_pop, &mutex, &absolute_time)), if ( ::pthread_mutex_unlock(&mutex) ) PTINFO(" (in cleanup: mutex unlocking failed)") ; );
            }

            // ok. we have the mutex again and either:
            // * queue popping was disabled, or,
            // * queue became not empty
            // * timeout
            pop_result_type result;
            if( enable_pop ) {
                if ( !queue.empty()) {
                    b = queue.front();
                    queue.pop();
                    result = pop_success;
                }
                else {
                    result = pop_timeout;
                }
            }
            else {
                result = pop_disabled;
            }
            // take care of delayed disable: if enable_push=false and
            // queue.empty() => possibly delayed disable in effect.
            // Actually, it does not matter wether it was a delayed 
            // or blunt disable: if the condition holds, enable_pop
            // now MUST become false since only an enabled queue has
            // enable_push set to true.
            if( !enable_push && queue.empty() )
                enable_pop = false;

            nPop--;

            // We only ever ever wake up a pusher IF it makes sense to wake
            // one. Sense is:
            //    * there actually IS potentially someone waiting to push
            //      (nPush>0)
            //    * we actually popped (result=pop_success)
            // We must signal those blocked threads since if we don't then
            // they will never re-evaluate their condition to see that they
            // can actually push, even if it was us who actually emptied the
            // queue. Those blocking threads don't wake themselves up you
            // know. 
            // It is sufficient to just wake up one of them.
            if( nPush && (result == pop_success) )
                FASTPTHREAD_CALL( ::pthread_cond_signal(&condition_push) );
            FASTPTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );

            return result;
        }
        
        // trypop(): if something is in the queue, return it,
        //            check for queue-cancellation.
        pop_result_type trypop( Element& b ) {
            // first things first ...
            PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );

            // another thread is waiting on this object
            nPop++;

            pop_result_type result = pop_disabled;
            if ( enable_pop ) {
                if (queue.empty()) {
                    result = pop_timeout;
                }
                else {
                    b = queue.front();
                    queue.pop();
                    result = pop_success;
                }
            }

            // take care of delayed disable: if enable_push=false and
            // queue.empty() => possibly delayed disable in effect.
            // Actually, it does not matter wether it was a delayed 
            // or blunt disable: if the condition holds, enable_pop
            // now MUST become false since only an enabled queue has
            // enable_push set to true.
            if( !enable_push && queue.empty() )
                enable_pop = false;

            nPop--;
            // We only ever ever wake up a pusher IF it makes sense to wake
            // one. Sense is:
            //    * there actually IS potentially someone waiting to push
            //      (nPush>0)
            //    * we actually popped (did_pop)
            // We must signal those blocked threads since if we don't then
            // they will never re-evaluate their condition to see that they
            // can actually push, even if it was us who actually emptied the
            // queue. Those blocking threads don't wake themselves up you
            // know. 
            // It is sufficient to just wake up one of them.
            if( nPush && (result == pop_success) )
                FASTPTHREAD_CALL( ::pthread_cond_signal(&condition_push) );
            FASTPTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
            
            return result;
        }

        // clear the queue of all contents
        // leaves the enabled/disabled state in tact, 
        // except when the queue is delayed disabled
        void clear() {
            // first things first ...
            FASTPTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
            
            bool did_pop = !queue.empty();
            if ( did_pop ) {
                do {
                    queue.pop();
                } while (!queue.empty());
            }

            if( !enable_push ) {
                // delayed disabled queue
                enable_pop = false;
            }
            // We only ever ever wake up a pusher IF it makes sense to wake
            // one. Sense is:
            //    * there actually IS potentially someone waiting to push
            //      (nPush>0)
            //    * we actually popped (did_pop)
            // We must signal those blocked threads since if we don't then
            // they will never re-evaluate their condition to see that they
            // can actually push, even if it was us who actually emptied the
            // queue. Those blocking threads don't wake themselves up you
            // know. 
            // Since we cleared the queue, wake up all pushers
            if( nPush && did_pop )
                FASTPTHREAD_CALL( ::pthread_cond_broadcast(&condition_push) );
            FASTPTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
        }
        

        // Destroy the queue.
        // First disable it, before destroying the resources.
        // This cannot deadlock :) - a thread, blocking waiting on
        // this object cannot execute the destructor because it,
        // well, is in a blocking wait for something else :)
        ~bqueue() {
            // Ok, all threads that were blocking on the queue have left the building.
            // now we can safely destroy the resources. We *have* the mutex.
            PTHREAD_CALL( ::pthread_cond_destroy(&condition_pop) );
            PTHREAD_CALL( ::pthread_cond_destroy(&condition_push) );
            PTHREAD_CALL( ::pthread_mutex_destroy(&mutex) );
        }

    private:
        // As of this version (Mar 2011) we separate the condition variable
        // into two flavours: one if someone pushed on the queue and one if
        // someone popped the queue.
        // This was an oversight on my behalf (not separating the two) and
        // may lead to incorrect behaviour when multiple push()ers and
        // pop()pers are present.
        // Before March 2011 the queue did the following:
        //   * there were always only one push()er and one pop()er
        //   * each succesfull push() or pop() triggered a condition
        //    _broadcast_ (stoopid me!)
        // I realised this is both inefficient (if someone did a push() then
        // only a single pop()per needs to be unlocked - all others will
        // find that their condition_wait is still invalid and will re-enter
        // the wait). The solution is: use condition_signal() in case of a
        // succesfull push() or pop().
        // However, this simple scheme (condition_signal() + only one
        // condition-variable) fails if there are an arbitrary, yet >1,
        // number of each of push()ers or pop()pers.
        // condition_signal() only unlocks 1 waiting thread on the
        // condition-variable that's signalled. Suppose there are three
        // threads involved: two pushers and one popper and the queue is
        // empty and has only room for one slot.
        // If the first pusher pushes and does a condition_signal then it
        // might just be that the other pusher gets woken up (remember: all
        // three threads are blocked on the same condition). that one
        // decides it cannot push (the one empty slot is filled by now) and
        // the popper doesn't wake up so the filled slot does not get
        // emptied => deadlock, i.e. #FAIL.
        // You could go back to condition_broadcast() (all blocked threads
        // get woken up so the popper, when woken up, will detect "ah, I
        // *can* pop" and resolve the deadlock (note: this will again create
        // a condition_broadcast!). This is highly inefficient since you get
        // 2*(N-1) wakeups (once because of the first pusher succeeding
        // and another one because of the succesfull pop).
        //
        // The best and correct way is to have separate push() and pop()
        // conditions - each thread can wait + signal on the appropriate condition.
        //  
        // In fact, it became even slightly more efficient: the code now
        // only signals when it is possible that someone is blocking waiting
        // on you:
        //    * if you just popped then you only need to signal a
        //      (potential) pusher if there is exactly one free slot left:
        //      this implies that the queue was full before you popped, i.e.
        //      pushers could be blocking waiting on someone, not unlike
        //      you, making space.
        //    * the same applies for pushers pushing the/a first element on
        //      queue: only then there may be poppers in a blocking wait.
        // In all other cases there is no need to signal since there is
        // either space available/are elements available so neither the
        // pushers nor the pushers will go into a blocking wait ...
        //
        // The conditionvariable names indicate the condition you're waiting
        // for: condition_push means that if you're blocked waiting on this
        // condition, you're waiting to be able to push.
        bool                   enable_push;
        bool                   enable_pop;
        queue_type             queue;
        unsigned int           nPush;
        unsigned int           nPop;
        pthread_cond_t         condition_pop;
        pthread_cond_t         condition_push;
        pthread_mutex_t        mutex;
        capacity_type          capacity;

        // init with capacity 'cap'
        // Note: '0' is a valid size.
        void init(capacity_type cap) {
            capacity      = cap;
            enable_push   = enable_pop = (capacity!=invalid_size);
            nPush         = 0;
            nPop          = 0;

            PTHREAD_CALL( ::pthread_mutex_init(&mutex, 0) );
            PTHREAD_CALL( ::pthread_cond_init(&condition_pop, 0) ); 
            PTHREAD_CALL( ::pthread_cond_init(&condition_push, 0) ); 
        }

        // do not support copy/assignment
        // the functions are declared here, but NOT implemented
        // -> triggers a compile-time error if used.
        bqueue( const bqueue<Element>& );
        const bqueue<Element>& operator=( const bqueue<Element>& );
};



#endif
