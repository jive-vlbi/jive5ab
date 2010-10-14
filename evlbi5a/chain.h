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

#include <thunk.h>
#include <bqueue.h>
#include <ezexcept.h>
#include <pthreadcall.h>
#include <countedpointer.h>

// Make it compile with GCC >=4.3 and <4.3
#define GVERS (10000 * __GNUC__ + 100 * __GNUC_MINOR__)
#if GVERS > 40299
    #define STATICTEMPLATE
#else
    #define STATICTEMPLATE static
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
// Each step will run in a separate thread, the syncronization between 
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
// The sync_type allows for locking/condition_wait
// on the datatype. It holds a pointer to an instance of ExtraInfo.
// Do NOT delete the pointer - the system takes care of that.
//


// Code in this file throws exceptions of this flavour
DECLARE_EZEXCEPT(chainexcept);


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
    // call this one only when you hold the lock
    inline void cond_wait(void) {
        PTHREAD_CALL( ::pthread_cond_wait(condition, mutex) );
    }

    private:
        sync_type(pthread_cond_t* cond, pthread_mutex_t* mtx):
            cancelled(false), userdata(0), qdepth(0), stepid(0),
            condition(cond), mutex(mtx)
        {}

        // These methods will all be called with
        // held mutex. The framework ensures that
        void setuserdata(void* udptr) {
            userdata = (UserData*)udptr;
        }
        void setqdepth(unsigned int d) {
            *((unsigned int*)(&this->qdepth)) = d;
        }
        void setstepid(unsigned int s) {
            *((unsigned int*)(&this->stepid)) = s;
        }
        void setcancel(bool v) {
            cancelled = v;
        }


        pthread_cond_t*  condition;
        pthread_mutex_t* mutex;
};
// Specialization for no syncythings 
template <>
struct sync_type<void> {
    sync_type(pthread_cond_t*, pthread_mutex_t*) {}
    void setuserdata(void*) {}
    void setqdepth(unsigned int){}
    void setstepid(unsigned int){}
    void setcancel(bool) {}
};

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

    private:
        inq_type(bqueue<Element>* q): qptr(q) {}

        bqueue<Element>*  qptr;
};
// OutputQueues allow pushing and delayed_disabling.
template <typename Element>
struct outq_type {
    friend class chain;

    bool push(const Element& e) {
        return qptr->push(e);
    }
    void delayed_disable(void) {
        qptr->delayed_disable();
    }

    private:
        outq_type(bqueue<Element>* q): qptr(q) {}

        bqueue<Element>*  qptr;
};



// Functions for constructing/deleting thread-argument-types
// in a typesafe manner.
template <typename T>
static T* maker(void) {
    return new T();
}


template <>
STATICTEMPLATE void* maker<void>(void) {
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
STATICTEMPLATE void deleter<void>(void*) { }

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
        struct runstepargs {
            thunk_type*     threadthunkptr;
            thunk_type*     delayeddisableoutqptr;
            thunk_type*     disableinqptr;
            unsigned int    nthread;
            pthread_mutex_t mutex;

            runstepargs(thunk_type* tttptr, thunk_type* ddoptr, thunk_type* diptr);

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
                         unsigned int sid, unsigned int n=1);
           
            // total depth of queue downstream of this step. 
            unsigned int       qdepth;
            const unsigned int stepid;
            const unsigned int nthread;

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

    public:
        // Typedefs for the threadfunction signatures
        typedef steps_type::size_type      stepid;
        static const steps_type::size_type invalid_stepid = (steps_type::size_type)-1;

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
        template <typename Out, typename UD>
        stepid add(void (*prodfn)(outq_type<Out>*, sync_type<UD>*), unsigned int qlen) {
            // insert a default maker
            return add(prodfn, qlen, &maker<UD>);
        }
        // allow specification of a "template" for your type - you'll get 
        // a copy of it
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
            return add(prodfn, qlen, makethunk(m,a, b));
        }

        template <typename T, typename UD>
        stepid add(void (*prodfn)(outq_type<T>*, sync_type<UD>*), 
                   unsigned int qlen, thunk_type udmaker) {
            typedef bqueue<T>     qtype;
            typedef outq_type<T>  oqtype;
            typedef sync_type<UD> stype;

            // Finally! Assert we *CAN* add a producer 
            // (ie not one set yet)
            EZASSERT(_chain->steps.size()==0, chainexcept);
            // Make sure the thunk returns something of type UD*
            EZASSERT(udmaker.returnvaltype()==TYPE(UD*), chainexcept);

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
            internalstep* is = new internalstep(TYPE(UD*), &iq->delayed_disable, &chain::nop, 0);
            stype*        s  = new stype(&is->condition, &is->mutex);

            is->qdepth      = qlen;
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
            EZASSERT(_chain->queues[previousq]->elementtype==TYPE(In), chainexcept);

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
                                                   sid);
            stype*        s     = new stype(&is->condition, &is->mutex);

            // Now the step (it holds the adapters, make sure
            // they get type-safe deleted)
            is->qdepth      = qlen;
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
            return add(consfn, makethunk(m,a, b));
        }

        template <typename In, typename UD>
        stepid add(void (*consfn)(inq_type<In>*, sync_type<UD>*), thunk_type udmaker) {
            typedef bqueue<In>    qtype;
            typedef inq_type<In>  iqtype;
            typedef sync_type<UD> stype;

            // Finally! Assert we *CAN* add a consumer
            EZASSERT(_chain->steps.size()>0, chainexcept);
            EZASSERT(_chain->closed==false, chainexcept);
            // Make sure the thunk returns something of type UD*
            EZASSERT(udmaker.returnvaltype()==TYPE(UD*), chainexcept);

            // Assert that the last queue's elementtype is
            // what this one expects to receive
            stepid    sid = _chain->steps.size();
            queueid   previousq = (_chain->queues.size()-1);
            EZASSERT(_chain->queues[previousq]->elementtype==TYPE(In), chainexcept);

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
                                                   sid);
            stype*        s     = new stype(&is->condition, &is->mutex);


            is->qdepth      = 0;
            is->iqdeleter   = makethunk(&deleter<iqtype>, iqptr);
            is->oqdeleter   = thunk_type(); 
            is->sdeleter    = makethunk(&deleter<stype>, s);
            // Now the UserData-related stuff
            is->udmaker     = udmaker;
            is->udtovoid    = makethunk(&tovoid<UD>);
            is->uddeleter   = makethunk(&deleter<UD>);
            is->setud       = makethunk(&stype::setuserdata, s);
            is->setqd       = curry_type();
            is->setstepid   = makethunk(&stype::setstepid, s);
            is->setcancel   = makethunk(&stype::setcancel, s);

            // and finally, the actual threadfunction
            is->threadfn    = makethunk(consfn, iqptr, s);

            _chain->steps.push_back(is);

            // And close the chain!
            _chain->closed = true;
            return sid;
        }


        // This will run the queue.
        // New steparguments will be constructed according to what was
        // specified when adding the steps.
        // The queues will be enabled.
        // Finally, the threads are created and run, starting
        // from the consumer back to the producer.
        void run(); 

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
        // If you need your thread to wait for some external event before
        // starting to enter its "main"loop (pushing/popping on the
        // queue(s)), you have to prepare the userdata and then use the
        // "communicate()" method to modify & inform the thread that
        // _something_ has happened.
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
        template <typename M>
        void communicate(stepid s, M m) {
            curry_type ct = makethunk(m);
            _chain->communicate(s, ct);
            ct.erase();
        }
        template <typename M, typename A>
        void communicate(stepid s, M m, A a) {
            curry_type ct = makethunk(m, a);
            _chain->communicate(s, ct);
            ct.erase();
        }


        // Register a function to be called for a step immediately before the
        // queues are disabled. This allows threadfunction authors to
        // make a thread which is POTENTIALLY NOT (YET) push()ing or pop()ing from
        // a queue(*) break from their condition_wait or break from a blocking
        // read on a filedescriptor (eg close the fd ...) or anything else
        // to signal the thread it should stop running.
        //
        // All registered functions are called in the order they were
        // registered, which the mutex for the indicated step held.
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
        template <typename M>
        void register_cancel(stepid s, M m) {
            _chain->register_cancel(s, makethunk(m));
        }
        template <typename M, typename A>
        void register_cancel(stepid s, M m, A a) {
            _chain->register_cancel(s, makethunk(m, a));
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

        // Returns wether the chain is empty (== a default chain)
        bool empty( void ) const;

        ~chain();
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

        // The actual implementation of the chain - just bookkeeping really
        struct chainimpl {
            bool               closed;
            bool               running;
            steps_type         steps;
            queues_type        queues;
            cancellations_type cancellations;

            // do any initialization, if necessary
            chainimpl();

            // implementations of the chain-level methods
            void run();
            void stop();
            void gentle_stop();
            bool empty(void) const;
            void communicate(stepid s, curry_type ct);
            void communicate(stepid s, thunk_type tt);
            void register_cancel(stepid stepnum, curry_type ct);

            // Process the cancellations
            void do_cancellations();

            // cancel the sync_types<>
            void cancel_synctype();

            // Assumes that someone has signalled the threads that stop
            // is imminent. This will join all thread (ie wait until
            // everyone is finished) and then cleanup the 
            // runtime arguments (producerargs, consumerargs and stepargs)
            // and threadids.
            void join_and_cleanup();

            // destroy the resources.
            ~chainimpl();
        };

        static thunk_type nop;
        static void*      run_step(void* runstepargsptr);


        countedpointer<chainimpl> _chain;
};


#endif