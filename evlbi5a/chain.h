// Multithread processing chain building 
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
#ifndef EVLBI5A_CHAIN_H
#define EVLBI5A_CHAIN_H

#include <string>
#include <vector>
#include <typeinfo>
#include <memory>

#include <thunk.h>
#include <bqueue.h>
#include <ezexcept.h>
#include <pthreadcall.h>
#include <countedpointer.h>
#include <mutex_locker.h>

// Make it compile with GCC >=4.3 and <4.3 as well as clang500.2.79

#ifdef __clang__          /* __clang__ begin */

#define CVERS (10000 * __clang_major__ + 100 * __clang_minor__)
#define CVERSMIN 49999

#elif defined(__GNUC__)   /* __clang__ end,  __GNUC__ begin */

#define CVERS (10000 * __GNUC__ + 100 * __GNUC_MINOR__)
#define CVERSMIN 40299

#else   /* neither __clang__ nor __GNUC__ */

#define CVERS    1
#define CVERSMIN 0

#endif  /* End of compiler version stuff */

#if CVERS > CVERSMIN
    #define CSTATICTEMPLATE
#else
    #define CSTATICTEMPLATE static
#endif

// The idea is to be able to define a multithreaded
// processing chain at runtime.
// A chain consists of at least two steps: a producer and a consumer.
// Intermediate steps are optional. 
//
// producer -> push blocks in a bqueue -> step pops+processes
//          -> push in another bqueue -> [... more steps ...]
//          -> consumer pops block from bqueue and does final thing
//                   (send over internet, write to file, drop it) 
//
// Each step will run in a separate thread, the synchronization between 
// the threads is done via the queue.
// Once a chain is built, it can be stored(*) and run/stopped any number of 
// times. Provided your threadfunctions do not crash, that is.
// (*) the chain object is copyable so you can have functions returning 
//     a chain.
//
// Threadfunctions that are ment to be part of a processing chain should
// have one of the following signatures:
//
// Producerthreads only may write to an outputqueue of some data type.
// 
//  void producer(outq_type<OutData>* qptr)
//  void producer(outq_type<OutData>* qptr, sync<ExtraInfo>*);
//
// Intermediate steps in a chain have an input queue and an outputqueue.
//
// void step(inq_type<InData>*, outq_type<OutData>*)
// void step(inq_type<InData>*, outq_type<OutData>*, sync_type<Blahblah>*)
//
// As long as the inq_type of your step matches the output-type of
// the previous step/producer it's ok
//
// Finally, at the end of a chain is a consumer, only taking an input queue:
//
// void consumer(inq_type<InData>*)
// void consumer(inq_type<InData>*, sync_type<Foobar>*)
//
//
// If a step needs an extra argument of, say, type ExtraInfo,
// signal that by accepting an additional "sync_type<ExtraInfo>*" parameter.
// The system will allocate a fresh "ExtraInfo" instance for each
// run of the chain.
// The sync_type wraps the ExtraInfo type in a type supporting locking/condition_wait
// on the datatype. It holds a pointer to an instance of ExtraInfo.
// Do NOT delete the pointer - the system takes care of that AFTER *all*
// threads have finished [this makes your life considerable easier]


// Code in this file throws exceptions of this flavour
DECLARE_EZEXCEPT(chainexcept)


// Forward declaration so it can be marked as friend
class chain;

// A holder for a pointer to userdata with accompanying "cancelled" flag.
// The mutex and conditionvariable are there but inaccessible
// to users, other than via the obvious "lock()", "unlock()" and
// "cond_wait()" synchronization primitives.
template <typename UserData>
struct sync_type {
    friend class chain;

    // cancellation flag. If you are waiting for a 
    // specific condition on userdata, add this condition
    // as well. If the chain is cancelled the flag will
    // be set to "true" and the condition broadcast.
    //
    // Example: your threadfunction is doing this: waiting for
    //          *userdata to become >0. It will deadlock
    //          if the chain is cancelled before someone else
    //          makes *userdata >0 since the thread will not
    //          know that it is supposed to cancel (and *userdata
    //          is still not >0):
    //
    //   sptr->lock();
    //   while( *sptr->userdata<=0 )
    //      sptr->cond_wait();
    //
    // Rather, implement it like this:
    //
    //   sptr->lock();
    //   while( !sptr->cancelled && *sptr->userdata<=0 )
    //      sptr->cond_wait();
    //
    // Which makes it break out of the loop if the chain
    // is cancelled OR *userdata becomes >0. The system will set
    // "sptr->cancelled" to true so after falling out of the while()
    // loop you can test which of the conditions held true and take
    // appropriate action.
    bool               cancelled; 

    // Pointer to an instance of UserData
    UserData*          userdata;

    // Depth of queues downstream of this stream
    const unsigned int qdepth;

    // your allocated stepid
    const unsigned int stepid;

    inline void lock( void ) {
        PTHREAD_CALL( ::pthread_mutex_lock(mutex) );
    }
    inline void unlock( void ) {
        PTHREAD_CALL( ::pthread_mutex_unlock(mutex) );
    }
    // call these ones only when you hold the lock
    inline void cond_wait(void) {
        PTHREAD_CALL( ::pthread_cond_wait(condition, mutex) );
    }
    inline void cond_signal(void) {
        PTHREAD_CALL( ::pthread_cond_signal(condition) );
    }
    inline void cond_broadcast(void) {
        PTHREAD_CALL( ::pthread_cond_broadcast(condition) );
    }


//    private:
        sync_type(pthread_cond_t* cond, pthread_mutex_t* mtx):
            cancelled(false), userdata(0), qdepth(0), stepid(0),
            condition(cond), mutex(mtx)
        {}

        // These methods will all be called with
        // held mutex. The framework ensures this.
        void setuserdata(void* udptr) {
            userdata = (UserData*)udptr;
        }
        void setqdepth(unsigned int d) const {
            *(const_cast<unsigned int*>(&this->qdepth)) = d;
        }
        void setstepid(unsigned int s) const {
            *(const_cast<unsigned int*>(&this->stepid)) = s;
        }
        void setcancel(bool v) {
            cancelled = v;
        }

    private:
        pthread_cond_t*  condition;
        pthread_mutex_t* mutex;
};
// Specialization for no syncythings 
template <>
struct sync_type<void> {
    sync_type(pthread_cond_t*, pthread_mutex_t*) {}
    void setuserdata(void*) {}
    void setqdepth(unsigned int) const {}
    void setstepid(unsigned int) const {}
    void setcancel(bool) {}
};

// Execute statements "f" with the lock
// on sync_type<> "s" held.
// If any of the statements throws, we
// catch it, unlock "s" and rethrow.
#define SYNC3EXEC(s, f, c) \
    do { \
         s->lock();\
         try { f; }\
         catch( ... ) { s->unlock(); c; throw; }\
         s->unlock();\
    } while(0);

#define SYNCEXEC(s, f) \
    SYNC3EXEC(s, f, ;)

// Helper function for communicating with a thread
template <typename T>
void assign(T* valptr, T val) {
    *valptr = val;
}

// InputQueues only allow popping
template <typename Element>
struct inq_type {
    friend class chain;

    bool pop(Element& e) {
        return qptr->pop(e);
    }

    pop_result_type pop(Element& e, const struct timespec& absolute_time) {
        return qptr->pop(e, absolute_time);
    }

//    private:
        inq_type(bqueue<Element>* q): qptr(q) {}

    private:
        bqueue<Element>*  qptr;
};
// OutputQueues allow pushing and delayed_disabling.
template <typename Element>
struct outq_type {
    friend class chain;

    bool push(const Element& e) {
        return qptr->push(e);
    }

    //private:
        outq_type(bqueue<Element>* q): qptr(q) {}

    private:
        bqueue<Element>*  qptr;
};



// Functions for constructing/deleting thread-argument-types
// in a typesafe manner.
template <typename T>
static T* maker(void) {
    return new T();
}
#if 0
// in order to make
//  chain::add( (void)(*fn)(<someQtype>, sync_type<UD>*) );
// work, when UD (UserData) is, in fact, of the pointerpersuasion,
// we really should have a default maker that looks like below.
// Note: this only applies to those circumstances where the userdata is a
// pointer and the user did not supply either:
//    * an existing pointer (pointing to something allocated by the user
//      him/herself)
//    * a function/functor which returns a pointer to a new instance of
//      pointer making sure the pointer pointed at actually points at something
// One could argue this is likely an error/undesirable behaviour
template <typename T> 
static T** maker<T*>(void) {
    T** ptr2ptr2T = new T*(0);
    *ptr2ptr2T = new T();
    return ptr2ptr2T;
}
#endif
template <>
CSTATICTEMPLATE void* maker<void>(void) {
    return 0;
}

// Duplicate a given prototype
template <typename T>
struct duplicator {
    typedef T* Return;
    typedef void Argument;
    duplicator(const T& proto): prototype(proto) {}

    T* operator()( void ) {
        return new T(prototype);
    }

    T  prototype;
};

// typesafe delete
template <typename T>
static void deleter(T* ptr) {
    delete ((T*)ptr);
}
template <>
CSTATICTEMPLATE void deleter<void>(void*) { }

template <typename T>
static void* tovoid(thunk_type* t) {
    T*   orgptr;
    t->returnval(orgptr);
    return (void*)orgptr;
}

// Most of the methods will throw up if inconsistencies being found
// (eg attempting to run an empty chain, adding a consumer before
// there is a producer etc)
class chain {
    private:
        // forward declaration
        struct chainimpl;

        struct runstepargs {
            thunk_type*     threadthunkptr;
            thunk_type*     delayeddisableoutqptr;
            thunk_type*     disableinqptr;
            chainimpl*      thechain;
            unsigned int    nthread;
            pthread_mutex_t mutex;

            runstepargs(thunk_type* tttptr, thunk_type* ddoptr, thunk_type* diptr, chainimpl* impl);

            private:
                runstepargs();
        };
        // The "sync_type<UserData>*" is NOT stored literally.
        // It is already contained in the thunks/curried functions
        // (including the deleter). We do not need to know
        // its value anymore - it lives as long as the chain lives.
        // When the chain is destructed, then the "sync_type<UD>"
        // is deleted.
        typedef std::vector<pthread_t*>  tid_type;

        struct internalstep {
            internalstep(const std::string& udtp, thunk_type* oqdisabler, thunk_type* iqdisabler,
                         unsigned int sid, chainimpl* impl, unsigned int n=1);
          
            // did we call upon the userdata maker and fill in the
            // actualudptr?
            bool               haveUD;

            // total depth of queue downstream of this step. 
            unsigned int       qdepth;
            const unsigned int stepid;
            /*const*/ unsigned int nthread;

            // Modify the contents of the sync_type<UserData>.
            // The pointer to the actual "sync_type<UserData>"
            // is already filled in, only need to pass the 
            // missing argument later on :)
            curry_type   setud;
            curry_type   setqd;
            curry_type   setstepid;
            curry_type   setcancel;

            // type-safe delete the "sync_type<UserData>"
            thunk_type   sdeleter;
            // Delete the inq_type<..> and outq_type<..> objects
            thunk_type   iqdeleter;
            thunk_type   oqdeleter;

            // Pointer to current "UserData" (if any)
            void*             actualudptr;
            // typeid().name() of "UserData"
            const std::string udtype;
            // Pointer to actual sync_type<UserData>
            void*             actualstptr;

            // Returns a new instance of a "UserData"
            thunk_type   udmaker;
            // Casts it to void* - this seems a bit superfluous but
            // it means that everything is accounted for re typesafety
            // and we do not need to do "weird" casts. At compiletime
            // everything is typechecked and found to be legal [else
            // it won't compile] so we know at runtime we can "just do it".
            //
            // The problem is:
            //    udmaker() returns a "UserData*"
            // attempting to extract a "void*" gives a runtime error;
            // the returnvalue and the thing we request it to be extracted
            // into do not match.
            // So we build an adaptor which holds a "UserData*", so it
            // CAN typesafely extract the "UserData*" and then return
            // it as void*.
            curry_type   udtovoid;
            // type-safe deletes an instance of "UserData"
            curry_type   uddeleter;

            // And the actual threadfunction - all
            // arguments already filled in.
            thunk_type   threadfn;

            // Each step has its own mutex and conditionvariable
            pthread_cond_t  condition;
            pthread_mutex_t mutex;

            // And a number of threads executing this step
            tid_type        threads;
            // And the argument they were passed
            runstepargs     rsa;

            ~internalstep();

            private:
                internalstep();
        };
        struct internalq {
            internalq(const std::string& tp);

            // Pointer to the actual "queue<SomeType>"
            // We only retain this pointer for a possible
            // next step which may need to re-interpret
            // this pointer into the correct type
            void*              actualqptr;
            // typeid().name() of "SomeType"
            const std::string  elementtype; 

            // These thunks should contain a boxed call
            // to "actualqptr->enable()" and "->disable()"
            // [with the pointer being already filled in
            // and of the correct type!]
            thunk_type   enable;
            thunk_type   disable;
            thunk_type   delayed_disable;
            thunk_type   qdeleter;

            ~internalq();

            private:
                internalq();
        };

        typedef std::vector<internalq*>     queues_type;
        typedef std::vector<internalstep*>  steps_type;
        typedef queues_type::size_type      queueid;


        //
        //  Now we have defined the steps-type, we can
        //  typedef the stepid. This is a public one;
        //  the users must be able to see this one
        //
    public:
        // Typedefs for the threadfunction signatures
        typedef steps_type::size_type      stepid;
        static const steps_type::size_type invalid_stepid = (steps_type::size_type)-1;

        // 
        // And back to private; now we can define some
        // more implementation details
        //
    private:
        // group together a stepid and a curried method.
        // When appropriate this thing can be executed
        // via the communicate method.
        struct stepfn_type {
            stepfn_type(stepid s, curry_type ct);

            stepid     step;
            curry_type calldef;
        };
        typedef std::vector<stepfn_type> cancellations_type;

        // After the last thread exists we can trigger
        // these finals. They are just thunks - they should
        // be canned function calls
        typedef std::vector<thunk_type> finals_type;


        // The actual implementation of the chain - just bookkeeping really
        struct chainimpl {
            bool               closed;
            bool               running;
            bool               joining;
            bool               cancelled;
            steps_type         steps;
            queues_type        queues;
            finals_type        finals;
            unsigned int       nthreads;
            cancellations_type cleanups;
            cancellations_type cancellations;
            pthread_mutex_t    mutex;
            pthread_cond_t     condition;

            // do any initialization, if necessary
            chainimpl();

            // implementations of the chain-level methods
            void run();
            void stop( bool be_gentle = false );
            void gentle_stop();
            void delayed_disable();
            bool empty(void) const;
            void nthread(stepid s, unsigned int num_threads);
            void communicate(stepid s, curry_type ct);
            void communicate(stepid s, thunk_type tt);
            void register_cancel(stepid stepnum, curry_type ct);
            void register_cleanup(stepid stepnum, curry_type ct);
            void register_final(thunk_type tt);

            typedef std::auto_ptr<mutex_locker> scoped_lock_type;
            scoped_lock_type scoped_lock();

            template <typename Ret>
            typename Storeable<Ret>::Type communicate_d(stepid s, curry_type ct) {
                typename Storeable<Ret>::Type  result;

                // Only necessary to know if the step exists
                EZASSERT(s<steps.size(), chainexcept);

                thunk_type     thunk = makethunk(ct, steps[s]);

                this->communicate(s, thunk);
                // extract the result
                thunk.returnval(result);
                thunk.erase();
                return result;
            }

            // Process the cancellations
            void do_cancellations();

            // cancel the sync_types<>
            void cancel_synctype();

            // Run all the registerd finalizers
            void do_finals();

            // Assumes that someone has signalled the threads that stop
            // is imminent. This will join all thread (ie wait until
            // everyone is finished) and then cleanup the 
            // runtime arguments (producerargs, consumerargs and stepargs)
            // and threadids.
            void join_and_cleanup();

            // destroy the resources.
            ~chainimpl();
        };

        
        //
        // And *finally* the full public API
        //
    public:

        chain();

        // Chain-building methods. 'qlen' is the amount of elements in the
        // outputqueue. They return a step-handle. If you want to
        // "communicate()" with the step, retain that handle.

        // The producers (with and without extra data).
        // Those with extra data also may pass something 
        // wich builds a new UserData instance
        template <typename Out>
        stepid add(void (*prodfn)(outq_type<Out>*), unsigned int qlen) {
            // call the function taking no extra data as one that does
            typedef void (*nosyncfn)(outq_type<Out>*, sync_type<void>*);
            return add((nosyncfn)prodfn, qlen); 
        }

        // (*** NOTE ***)
        // this is a prototype only. this makes sure that if the userdata in
        // your sync_type is of the pointer persuasion you're forced to
        // either come up with an existing pointer (which will be copied to
        // all threads in this step, or come up with something that
        // allocates stuff properly.
        template <typename Out, typename UD>
        stepid add(void (*prodfn)(outq_type<Out>*, sync_type<UD*>*) );

        template <typename Out, typename UD>
        stepid add(void (*prodfn)(outq_type<Out>*, sync_type<UD>*), unsigned int qlen) {
            // insert a default maker
            return add(prodfn, qlen, &maker<UD>);
        }
        // allow specification of a "template" for your type - you'll get 
        // a copy of it. Note: this will delegate to
        //    add( (*), qlen, M m) below; "duplicator<UD>()" must
        //    be turned into a thunk first!
        template <typename Out, typename UD>
        stepid add(void (*prodfn)(outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, UD prototype) {
            return add(prodfn, qlen, duplicator<UD>(prototype));
        }
        // extra data + extradata maker "M" (supposed to return "UD*")
        template <typename Out, typename UD, typename M>
        stepid add(void (*prodfn)(outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, M m) {
            return add(prodfn, qlen, makethunk(m));
        }
        // Id: only take a Maker with an Argument (eg "malloc" and "1024").
        template <typename T, typename UD, typename M, typename A>
        stepid add(void (*prodfn)(outq_type<T>*, sync_type<UD>*),
                   unsigned int qlen, M m, A a) {
            return add(prodfn, qlen, makethunk(m,a));
        }
        template <typename T, typename UD, typename M, typename A, typename B>
        stepid add(void (*prodfn)(outq_type<T>*, sync_type<UD>*),
                   unsigned int qlen, M m, A a, B b) {
            return add(prodfn, qlen, makethunk(m,a,b));
        }
#if 0
        ////////////////// Add functions for producers
        ///////// allowing you to tell how many threads to run

        template <typename Out>
        stepid addN(void (*prodfn)(outq_type<Out>*), unsigned int qlen, unsigned int nthr) {
            // call the function taking no extra data as one that does
            typedef void (*nosyncfn)(outq_type<Out>*, sync_type<void>*);
            return addN((nosyncfn)prodfn, qlen, nthr); 
        }

        // (*** NOTE ***)
        // this is a prototype only. this makes sure that if the userdata in
        // your sync_type is of the pointer persuasion you're forced to
        // either come up with an existing pointer (which will be copied to
        // all threads in this step, or come up with something that
        // allocates stuff properly.
        template <typename Out, typename UD>
        stepid addN(void (*prodfn)(outq_type<Out>*, sync_type<UD*>*) );

        template <typename Out, typename UD>
        stepid addN(void (*prodfn)(outq_type<Out>*, sync_type<UD>*), unsigned int qlen, unsigned int nthr) {
            // insert a default maker
            return add(prodfn, qlen, &maker<UD>, nthr);
        }
        // allow specification of a "template" for your type - you'll get 
        // a copy of it
        template <typename Out, typename UD>
        stepid addN(void (*prodfn)(outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, unsigned int nthr, UD prototype) {
            return add(prodfn, qlen, duplicator<UD>(prototype), nthr);
        }
        // extra data + extradata maker "M" (supposed to return "UD*")
        template <typename Out, typename UD, typename M>
        stepid addN(void (*prodfn)(outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, unsigned int nthr, M m) {
            return add(prodfn, qlen, makethunk(m), nthr);
        }
        // Id: only take a Maker with an Argument (eg "malloc" and "1024").
        template <typename T, typename UD, typename M, typename A>
        stepid addN(void (*prodfn)(outq_type<T>*, sync_type<UD>*),
                   unsigned int qlen, unsigned int nthr, M m, A a) {
            return add(prodfn, qlen, makethunk(m,a), nthr);
        }
        template <typename T, typename UD, typename M, typename A, typename B>
        stepid addN(void (*prodfn)(outq_type<T>*, sync_type<UD>*),
                   unsigned int qlen, unsigned int nthr, M m, A a, B b) {
            return add(prodfn, qlen, makethunk(m,a,b), nthr);
        }
#endif
        /////////// The actual step adder function
        template <typename T, typename UD>
        stepid add(void (*prodfn)(outq_type<T>*, sync_type<UD>*), 
                   unsigned int qlen, thunk_type udmaker /*, unsigned int nthr=1*/) {
            typedef bqueue<T>     qtype;
            typedef outq_type<T>  oqtype;
            typedef sync_type<UD> stype;

            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            // Finally! Assert we *CAN* add a producer 
            // (ie not one set yet)
            EZASSERT(_chain->steps.size()==0, chainexcept);
            // Make sure the thunk returns something of type UD*
            EZASSERT2(udmaker.returnvaltype()==TYPE(UD*), chainexcept, EZINFO("udmaker:" << udmaker.returnvaltype() << " TYPE(UD*):" << TYPE(UD*)));
#if 0
            // At least one thread/step
            EZASSERT(nthr>0, chainexcept);
#endif
            // It's time to create a queue and curried
            // pointers-to-memberfunctions of the queue such
            // that we can influence the queue irrespective of its type.
            // h00t. (once you understand what the 'curry' and 'thunk'
            // thingies can do for you, you probably say W000000000T! ;))
            //
            // We can do this since once the queues will remain. Between
            // successive runs of the chain the queues will only be 
            // disabled and enabled but not deleted/reconstructed.
            // Same goes for the sync_type wrapper.
            // Once created it remains alive until the chain itself is
            // deleted
            
            // outq is nothing but adapter around a fully functional queue,
            // only allowing "push()". Create the basic queue type
            // (qtype) and then create an oqtype - wrapping the original
            // queue. The same process (adapting the real queue) will
            // be used whilst adding subsequent steps)
            // Construct the new step and queue
            qtype*        q  = new qtype(qlen);
            internalq*    iq = new internalq(TYPE(T));

            // Fill in the details of the internal Q
            iq->actualqptr      = q;
            iq->qdeleter        = makethunk(&deleter<qtype>, q);
            iq->enable          = makethunk(&qtype::enable, q);
            iq->disable         = makethunk(&qtype::disable, q);
            iq->delayed_disable = makethunk(&qtype::delayed_disable, q);


            // And the internal step. Because this is the
            // producer, it has no input-queue.
            // As a result, the input-queue-disabler will be a no-op
            oqtype*       oq = new oqtype(q);
            internalstep* is = new internalstep(TYPE(UD*), &iq->delayed_disable, &chain::nop, 0, &(*_chain) /*, nthr*/);
            stype*        s  = new stype(&is->condition, &is->mutex);

            is->qdepth      = qlen;
            is->actualstptr = s;
            is->iqdeleter   = thunk_type(); 
            is->oqdeleter   = makethunk(&deleter<oqtype>, oq);
            is->sdeleter    = makethunk(&deleter<stype>, s);
            // Now the UserData-related stuff
            is->udmaker     = udmaker;
            is->udtovoid    = makethunk(&tovoid<UD>);
            is->uddeleter   = makethunk(&deleter<UD>);
            is->setud       = makethunk(&stype::setuserdata, s);
            is->setqd       = makethunk(&stype::setqdepth, s);
            is->setstepid   = makethunk(&stype::setstepid, s);
            is->setcancel   = makethunk(&stype::setcancel, s);

            // and finally, the actual threadfunction
            is->threadfn    = makethunk(prodfn, oq, s);

            _chain->queues.push_back(iq);
            _chain->steps.push_back(is);
            return (stepid)0;
        }



        // Add a step to the chain.
        // Only allowed if there is already a chain [namely at least a producer]
        // and it's not yet closed
        template <typename In, typename Out>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*), unsigned int qlen) {
            // wrap the function taking no extra data into one that does
            // so the rest of the code may assume the function is always
            // called with two arguments
            typedef void (*nosyncfn)(inq_type<In>*, outq_type<Out>*, sync_type<void>*);
            return add((nosyncfn)stepfn, qlen);
        }
        // Prototype only. See above under "(**** NOTE ****)".
        template <typename In, typename Out, typename UD>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*, sync_type<UD*>*) );

        template <typename In, typename Out, typename UD>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen) {
            // insert a default maker
            return add(stepfn, qlen, &maker<UD>);
        }
        // allow specification of a "template" for your type - you'll get 
        // a copy of it
        template <typename In, typename Out, typename UD>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, const UD& prototype) {
            return add(stepfn, qlen, duplicator<UD>(prototype));
        }
        // extra data + extradata maker "M" (supposed to return "UD*")
        template <typename In, typename Out, typename UD, typename M>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, M m) {
            return add(stepfn, qlen, makethunk(m));
        }
        // Id: only take a Maker with an Argument (eg "malloc" and "1024").
        template <typename In, typename Out, typename UD, typename M, typename A>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, M m, A a) {
            return add(stepfn, qlen, makethunk(m,a));
        }
        // Id: only take a Maker with an Argument (eg "malloc" and "1024").
        template <typename In, typename Out, typename UD, typename M, typename A, typename B>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*, sync_type<UD>*),
                   unsigned int qlen, M m, A a, B b) {
            return add(stepfn, qlen, makethunk(m,a, b));
        }

        template <typename In, typename Out, typename UD>
        stepid add(void (*stepfn)(inq_type<In>*, outq_type<Out>*, sync_type<UD>*), 
                   unsigned int qlen, thunk_type udmaker) {
            typedef bqueue<In>     prevqtype;
            typedef bqueue<Out>    qtype;
            typedef inq_type<In>   iqtype;
            typedef outq_type<Out> oqtype;
            typedef sync_type<UD>  stype;

            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            
            // Do the necessary assertions.
            // Split them so the user knows exactly which one failed.
            EZASSERT(_chain->steps.size()>0, chainexcept);
            EZASSERT(_chain->closed==false, chainexcept);
            // Make sure the thunk returns something of type UD*
            EZASSERT(udmaker.returnvaltype()==TYPE(UD*), chainexcept);
            // Assert that the underlying datatype of the previous 
            // queue is the same as we are taking as input.
            // At least then we know that the "void*" in the internalq
            // points to an instance of "bqueue<In>"
            queueid   previousq = (_chain->queues.size()-1);
            EZASSERT2(_chain->queues[previousq]->elementtype==TYPE(In), chainexcept,
                      EZINFO("previous step out '" << _chain->queues[previousq]->elementtype << "'"
                             << " current step in '" << TYPE(In) << "'") );

            // This will be the stepid of the new step
            stepid        sid  = _chain->steps.size();
            qtype*        newq = new qtype(qlen);
            internalq*    iq   = new internalq(TYPE(Out));

            // We have created a new queue: bqueue<Out>
            iq->actualqptr      = newq;
            iq->enable          = makethunk(&qtype::enable, newq);
            iq->disable         = makethunk(&qtype::disable, newq);
            iq->qdeleter        = makethunk(&deleter<qtype>, newq);
            iq->delayed_disable = makethunk(&qtype::delayed_disable, newq);

            // Now the internal step.
            // This step created a new queue (its output).
            // Create an output adaptor of that new queue and an
            // input adaptor of the previous queue [cast the "void*"
            // in the previous internalq to the correct type - we may
            // do so because the elementtypes did match (see the asserts
            // above) and we KNOW we only stick in pointers to
            // an instance of 'bqueue<ElementType>']
            // Also create the "sync_type<UD>" object.
            //
            // The inq-disabler is taken from the previous queue, obviously
            prevqtype*    prevq = (prevqtype*)_chain->queues[previousq]->actualqptr;
            iqtype*       iqptr = new iqtype(prevq);
            oqtype*       oqptr = new oqtype(newq);
            internalstep* is    = new internalstep(TYPE(UD*), &iq->delayed_disable,
                                                   &_chain->queues[previousq]->disable,
                                                   sid, &(*_chain));
            stype*        s     = new stype(&is->condition, &is->mutex);

            // Now the step (it holds the adapters, make sure
            // they get type-safe deleted)
            is->qdepth      = qlen;
            is->actualstptr = s;
            is->iqdeleter   = makethunk(&deleter<iqtype>, iqptr);
            is->oqdeleter   = makethunk(&deleter<oqtype>, oqptr);
            is->sdeleter    = makethunk(&deleter<stype>, s);
            // Now the UserData-related stuff
            is->udmaker     = udmaker;
            is->udtovoid    = makethunk(&tovoid<UD>);
            is->uddeleter   = makethunk(&deleter<UD>);
            is->setud       = makethunk(&stype::setuserdata, s);
            is->setqd       = makethunk(&stype::setqdepth, s);
            is->setstepid   = makethunk(&stype::setstepid, s);
            is->setcancel   = makethunk(&stype::setcancel, s);

            // and finally, the actual threadfunction
            is->threadfn    = makethunk(stepfn, iqptr, oqptr, s);

            // Before we actually add the step to the list of steps to 
            // perform, we update the cumulative queuedepth of all previous
            // steps :). Add an extra "1" - to allow for local buffering.
            // Typically, each step reserves qdepth+1 items; such that
            // even if all queues downstream are full it still has
            // space to work with (and not overwrite data)
            for(steps_type::iterator curstep=_chain->steps.begin();
                curstep!=_chain->steps.end();
                curstep++) 
                    (*curstep)->qdepth += (qlen+1);
            // Now append
            _chain->queues.push_back(iq);
            _chain->steps.push_back(is);

            return sid;
        }
    
    
        // We can only allow the chain to be closed if
        // 1) there IS a chain
        // 2) it hasn't been closed before
        // Also, consumers take no queuelength; they only read
        template <typename In>
        stepid add(void (*consfn)(inq_type<In>*)) {
            // wrap the function taking no extra data into one that does
            // so the rest of the code may assume the function is always
            // called with two arguments
            typedef void (*nosyncfn)(inq_type<In>*, sync_type<void>*);
            return add((nosyncfn)consfn);
        }
        // Prototype only. See above under "(**** NOTE ****)".
        template <typename In, typename UD>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD*>*) );

        template <typename In, typename UD>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD>*)) {
            // insert a default maker
            return add(consfn, &maker<UD>);
        }
        // allow specification of a "template" for your type - you'll get 
        // a copy of it
        template <typename In, typename UD>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD>*), const UD& prototype) {
            return add(consfn, duplicator<UD>(prototype));
        }
        // extra data + extradata maker "M" (supposed to return "UD*")
        template <typename In, typename UD, typename M>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD>*), M m) {
            return add(consfn, makethunk(m));
        }
        // Id: only take a Maker with an Argument (eg "malloc" and "1024").
        template <typename In, typename UD, typename M, typename A>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD>*), M m, A a) {
            return add(consfn, makethunk(m,a));
        }
        template <typename In, typename UD, typename M, typename A, typename B>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD>*), M m, A a, B b) {
            return add(consfn, makethunk(m,a,b));
        }
#if 0
        //////////// Consumers taking a number of threads argument
        template <typename In>
        stepid addN(void (*consfn)(inq_type<In>*), unsigned int nthr) {
            // wrap the function taking no extra data into one that does
            // so the rest of the code may assume the function is always
            // called with two arguments
            typedef void (*nosyncfn)(inq_type<In>*, sync_type<void>*);
            return addN((nosyncfn)consfn, nthr);
        }
        // Prototype only. See above under "(**** NOTE ****)".
        template <typename In, typename UD>
        stepid addN(void (*consfn)(inq_type<In>*, sync_type<UD*>*) );

        template <typename In, typename UD>
        stepid addN(void (*consfn)(inq_type<In>*, sync_type<UD>*), unsigned int nthr) {
            // insert a default maker
            return add(consfn, nthr, &maker<UD>);
        }
        // allow specification of a "template" for your type - you'll get 
        // a copy of it
        template <typename In, typename UD>
        stepid addN(void (*consfn)(inq_type<In>*, sync_type<UD>*), unsigned int nthr, const UD& prototype) {
            return addN(consfn, nthr, duplicator<UD>(prototype));
        }
        // extra data + extradata maker "M" (supposed to return "UD*")
        template <typename In, typename UD, typename M>
        stepid addN(void (*consfn)(inq_type<In>*, sync_type<UD>*), unsigned int nthr, M m) {
            return add(consfn, makethunk(m), nthr);
        }
        // Id: only take a Maker with an Argument (eg "malloc" and "1024").
        template <typename In, typename UD, typename M, typename A>
        stepid addN(void (*consfn)(inq_type<In>*, sync_type<UD>*), unsigned int nthr, M m, A a) {
            return add(consfn, makethunk(m,a), nthr);
        }
        template <typename In, typename UD, typename M, typename A, typename B>
        stepid addN(void (*consfn)(inq_type<In>*, sync_type<UD>*), unsigned int nthr, M m, A a, B b) {
            return add(consfn, makethunk(m,a,b), nthr);
        }
#endif
        ////////// The actual consumer adding function
        template <typename In, typename UD>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD>*), thunk_type udmaker /*, unsigned int nthr*/) {
            typedef bqueue<In>    qtype;
            typedef inq_type<In>  iqtype;
            typedef sync_type<UD> stype;

            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            
            // Finally! Assert we *CAN* add a consumer
            EZASSERT(_chain->steps.size()>0, chainexcept);
            EZASSERT(_chain->closed==false, chainexcept);
            // Make sure the thunk returns something of type UD*
            EZASSERT(udmaker.returnvaltype()==TYPE(UD*), chainexcept);

            // Assert that the last queue's elementtype is
            // what this one expects to receive
            stepid    sid = _chain->steps.size();
            queueid   previousq = (_chain->queues.size()-1);
            EZASSERT2(_chain->queues[previousq]->elementtype==TYPE(In), chainexcept,
                      EZINFO("previous step out '" << _chain->queues[previousq]->elementtype << "'"
                             << " current consumer in '" << TYPE(In) << "'"));

            // Great. Now we know everything matches up Ok.
            // Time to fill in the details.
            // This one does NOT create a new queue, only a new step.
            // It just adapts the last queue from a bqueue<> to an inq_type<>.
            // As a result, it does not have an output queue.
            qtype*        prevq = (qtype*)_chain->queues[previousq]->actualqptr;
            iqtype*       iqptr = new iqtype(prevq);
            // This step has no outputqueue so the delayed-disabler is a
            // no-op. The inq-disabler is obviously taken from the 
            // previous queue
            internalstep* is    = new internalstep(TYPE(UD*), &chain::nop,
                                                   &_chain->queues[previousq]->disable,
                                                   sid, &(*_chain) /*, nthr*/);
            stype*        s     = new stype(&is->condition, &is->mutex);


            is->qdepth      = 0;
            is->actualstptr = s;
            is->iqdeleter   = makethunk(&deleter<iqtype>, iqptr);
            is->oqdeleter   = thunk_type(); 
            is->sdeleter    = makethunk(&deleter<stype>, s);
            // Now the UserData-related stuff
            is->udmaker     = udmaker;
            is->udtovoid    = makethunk(&tovoid<UD>);
            is->uddeleter   = makethunk(&deleter<UD>);
            is->setud       = makethunk(&stype::setuserdata, s);
            is->setqd       = curry_type();
            is->setstepid   = makethunk(&stype::setstepid, (const stype*)s);
            is->setcancel   = makethunk(&stype::setcancel, s);

            // and finally, the actual threadfunction
            is->threadfn    = makethunk(consfn, iqptr, s);

            _chain->steps.push_back(is);

            // And close the chain!
            _chain->closed = true;
            return sid;
        }

        // By default each step is runs one instance of a thread.
        // Using this function you can set the amount of threads to spawn
        // for step <stepid> to num_threads.
        // * the step must already exist in the chain
        // * the chaim must NOT be running
        // * the chain does not have to be closed
        // * num_threads may NOT be 0 (zero)
        void nthread(stepid s, unsigned int num_threads);

        // This will run the queue.
        // New steparguments will be constructed according to what was
        // specified when adding the steps.
        // The queues will be enabled.
        // Finally, the threads are created and run, starting
        // from the consumer back to the producer.
        void run(); 



    private:
        // HV/BE: 18 Nov 2014
        //
        // In order to support communicating to a step at two levels:
        // 1. access to UserData only
        // 2. access to the sync_type (containing the UserData)
        //
        // we must rewrite the .communicate(stepid, curry_type) to 
        // assume that the curried function takes "pointer to internal step"
        // rather than "pointer to UserData".
        // Then, depending on wether the caller wants access to
        // UserData* or sync_type<UserData>* we wrap the actual functioncall
        // in one of the two following methods, each extracting the correct
        // data from the internal step; either the UserData* directly or the
        // sync_type<UserData>* and call the wrapped function with that
        // value.
        template <typename Ret, typename UD>
        static typename Storeable<Ret>::Type wrap_ud(chain::internalstep* isptr, curry_type ct) {
            typename Storeable<Ret>::Type rv = (typename Storeable<Ret>::Type());
            EZASSERT2_NZERO(isptr->udtype==ct.argumenttype(), chainexcept,
                    EZINFO("communicate: type mismatch for step " << isptr->stepid
                           << ": expect=" << isptr->udtype << " got=" << ct.argumenttype()));
            // Only assert non-NULL userdata pointer and call cleanup if we actually expect
            // it to be there ...
            if( isptr->haveUD ) {
                EZASSERT2_NZERO(isptr->actualudptr, chainexcept,
                        EZINFO("communicate: step[" << isptr->stepid << "] has no userdata. No communication."));
                ct( (UD*)isptr->actualudptr );
                ct.returnval(rv);
            }
            return rv;
        }
        template <typename Ret, typename UD>
        static typename Storeable<Ret>::Type wrap_st(chain::internalstep* isptr, curry_type ct) {
            typename Storeable<Ret>::Type  rv = (typename Storeable<Ret>::Type());
            // This is slightly hairier; the curry_type here should expect
            // sync_type<UD>* so let's check for that
            const std::string expect( TYPE(UD*) );

            EZASSERT2_NZERO(expect==isptr->udtype, chainexcept,
                    EZINFO("communicate: type mismatch for step " << isptr->stepid
                           << ": expect=" << expect << " got=" << isptr->udtype));
            // Only check for non-NULL pointer and call function if we
            // actually expect userdata
            if( isptr->haveUD ) {
                EZASSERT2_NZERO(isptr->actualstptr, chainexcept,
                        EZINFO("communicate: step[" << isptr->stepid << "] has no synctype!? No communication."));
                ct( (sync_type<UD>*)isptr->actualstptr );
                ct.returnval(rv);
            }
            return rv;
        }

    public:
        // If you need to "communicate" with a thread, i.e. altering the
        // userdata and signalling the threadfunction that something has
        // changed, you can use one or more of the "communicate" methods below. 
        //
        // Make it pthread_cond_wait() and use the "communicate()" method
        // below make the event happen (typically: modifying the 
        // state of the threadargument into a state recognized by the
        // thread to indicate "yay! let's go!").
        // One example would be waiting for a filedescriptor to become >0.
        // The thread should block on the condition "threadargs->fd <= 0".
        // Then, in another thread, someone opens a file and passes
        // the filedescriptor via
        //   chain.communicate(step, &threadargs::setfd, fd);
        //
        // Whilst doing this, be sure to include "threadargs->cancelled"
        // in case the thread got cancelled before it actually was doing
        // something. See "cond_wait()" in the "sync_type<>" class above.
        //
        // The communicate method executes the function pointed to by 'modfn'
        // with the mutex for the specified thread held, with a pointer to the
        // threads' userdata (of type T) as argument.
        //
        // What it does is this:
        //
        // * Find the thread with handle 's'
        // * for that thread, grab the mutex
        // * execute modfn(usrdataptr)
        // * issue a pthread_condition_broadcast, still with the mutex held
        //    (the docs say this is best practice)
        // * unlock the mutex
        // * return to caller
        //
        // If anything seems fishy: 
        //      - addressing outside step-array
        //      - type of userdata does not match that
        //        with which the step was created
        // an exception is thrown.
        //
        // Obviously, the threadfunction you're communicating with should
        // use cond_wait and/or mutex_lock semantics for this to work
        // reliably ...
        template <typename Ret, typename UD>
        typename Storeable<Ret>::Type communicate(stepid s, Ret (*fptr)(UD*)) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            return _chain->communicate_d<Ret>(s, makethunk(&wrap_ud<Ret, UD>, makethunk(fptr)));
        }
        template <typename Ret, typename UD>
        typename Storeable<Ret>::Type communicate(stepid s, Ret (*fptr)(sync_type<UD>*)) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            return _chain->communicate_d<Ret>(s, makethunk(&wrap_st<Ret, UD>, makethunk(fptr)));
        }


        template <typename Ret, typename UD, typename A>
        typename Storeable<Ret>::Type communicate(stepid s, Ret (*fptr)(UD*, A), A a) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            return _chain->communicate_d<Ret>(s, makethunk(&wrap_ud<Ret, UD>, makethunk(fptr, a)));
        }
        template <typename Ret, typename UD, typename A>
        typename Storeable<Ret>::Type communicate(stepid s, Ret (*fptr)(sync_type<UD>*, A), A a) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            return _chain->communicate_d<Ret>(s, makethunk(&wrap_st<Ret, UD>, makethunk(fptr, a)));
        }

        // The following communicates call member functions of the user data
        template <typename Ret, typename UD>
        typename Storeable<Ret>::Type communicate(stepid s, Ret (UD::*fptr)()) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            return _chain->communicate_d<Ret>(s, makethunk(&wrap_ud<Ret, UD>, makethunk(fptr)));
        }
        template <typename Ret, typename UD, typename A>
        typename Storeable<Ret>::Type communicate(stepid s, Ret (UD::*fptr)(A), A a) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            return _chain->communicate_d<Ret>(s, makethunk(&wrap_ud<Ret, UD>, makethunk(fptr, a)));
        }

        // Register a function to be called for a step immediately before the
        // queues are disabled. This allows threadfunction authors to
        // make a thread which is POTENTIALLY NOT (YET) push()ing or pop()ing from
        // a queue(*) break from their condition_wait or break from a blocking
        // read on a filedescriptor (eg close the fd ...) or anything else
        // to signal the thread it should stop running.
        //
        // All registered functions are called in the order they were
        // registered, with the mutex for the indicated step held.
        //
        // The function should take a pointer to an instance of the indicated steps'
        // userdatatype [as indicated when the step was added].
        //
        // Cancellationfunctions may be registered at any point
        // in time, even when the queue is already running.
        //
        // (*) In principle, the semantics of the chain are such that a
        // thread should interpret a failed push() or pop() as a signal to
        // stop. Hence, if your thread is'nt (yet) push'ing or pop'ing they
        // won't pick up that signal ...
        //
        // Note: if this seems quite like the "communicate()" function it
        // is, well, because it actually is :)
        // The registered cancel function will, eventually, when
        // appropriate, be executed with the "communicate()" method.
        template <typename Ret, typename UD>
        void register_cancel(stepid s, Ret (*fptr)(UD*)) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_cancel(s, makethunk(&wrap_ud<Ret, UD>, makethunk(fptr)));
        }
        template <typename Ret, typename UD, typename A>
        void register_cancel(stepid s, Ret (*fptr)(UD*, A), A a) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_cancel(s, makethunk(&wrap_ud<Ret, UD>, makethunk(fptr, a)));
        }
        template <typename Ret, typename UD, typename A>
        void register_cancel(stepid s, Ret (UD::*fptr)(A), A a) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_cancel(s, makethunk(&wrap_ud<Ret, UD>, makethunk(fptr, a)));
        }

        template <typename Ret, typename UD>
        void register_cancel(stepid s, Ret (*fptr)(sync_type<UD>*)) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_cancel(s, makethunk(&wrap_st<Ret, UD>, makethunk(fptr)));
        }
        template <typename Ret, typename UD, typename A>
        void register_cancel(stepid s, Ret (*fptr)(sync_type<UD>*, A), A a) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_cancel(s, makethunk(&wrap_st<Ret, UD>, makethunk(fptr, a)));
        }

        // Register a function to be called after the last thread of a
        // running chain exits. All registered finals() are called in 
        // the order they were registered.
        // No mutex lock is held on account of no threads being active 
        // anymore.
        // The registered finals should be "thunks", i.e. nullary functions
        // with no free arguments.
        template <typename M>
        void register_final(M m) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_final(makethunk(m));
        }
        template <typename M, typename A>
        void register_final(M m, A a) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_final(makethunk(m, a));
        }
        template <typename M, typename A1, typename A2>
        void register_final(M m, A1 a1, A2 a2) {
            chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
            _chain->register_final(makethunk(m, a1, a2));
        }

        // If you want to wait for the chain to finish, use this.
        // No cancellations are processed so you either must:
        // 1) know sure that the chain WILL finish (eg you know
        //    it has a finite input or only collects a finite
        //    amount of output)  OR
        // 2) Make sure *someone else* stops the chain - eg
        //    another thread calling ".stop()" or ".gentle_stop()"
        //    (in which cases any registered cancellations ARE run)
        void wait( void );

        // stop the chain, quite brutally. Bluntly disable all queues.
        // Then joins and deletes the runtime stuff.
        void stop( void );

        // Stops the chain by delayed-disabling the first queue. This
        // allows all data currently in the system to be processed
        // before the chain actually stops.
        // After everybody's finished the runtime args are deleted
        // and this function returns.
        void gentle_stop();

         // Stops the chain by delayed-disabling the first queue. This
        // allows all data currently in the system to be processed
        // before the chain actually stops.
        // This function does not stop the chain yet
        void delayed_disable();

       // Returns wether the chain is empty (== a default chain)
        bool empty( void ) const;

        ~chain();
    private:



        static thunk_type nop;
        static void*      run_step(void* runstepargsptr);


        countedpointer<chainimpl> _chain;
};

#endif
