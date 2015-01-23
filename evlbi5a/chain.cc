// Multithread processing chain building blocks
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
#include <chain.h>

// When using the chain code in a different project, remove this include
// and the line below which reads: "push_error( ... )"
#include <errorqueue.h>

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

using namespace std;

// Implement the exception class
DEFINE_EZEXCEPT(chainexcept)

// Reserve space for the static datamember
thunk_type chain::nop = thunk_type();

//  The chain interface
chain::chain() :
    _chain( new chainimpl() )
{}

// Run without any userarguments
void chain::run() {
    chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
    _chain->run();
}

void chain::nthread(stepid s, unsigned int num_threads) {
    chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
    _chain->nthread(s, num_threads);
}

void chain::wait() {
    chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
    _chain->join_and_cleanup();
    return ;
}

void chain::stop() {
    chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
    return _chain->stop();
}

void chain::gentle_stop() {
    chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
    return _chain->gentle_stop();
}

void chain::delayed_disable() {
    chain::chainimpl::scoped_lock_type locker = _chain->scoped_lock();
    return _chain->delayed_disable();
}

bool chain::empty(void) const {
    return _chain->empty();
}

chain::~chain() { }




//
//
//    IMPLEMENTATION CLASSES
//
//
chain::internalstep::internalstep(const string& udtp, thunk_type* oqdisabler,
                                  thunk_type* iqdisabler, unsigned int sid, chainimpl* impl, unsigned int n):
    haveUD( false ), qdepth(0), stepid(sid), nthread(n), actualudptr(0), udtype(udtp), actualstptr(0),
    rsa(&threadfn, oqdisabler,iqdisabler, impl)
{

    PTHREAD_CALL( ::pthread_mutex_init(&mutex, 0) );
    PTHREAD_CALL( ::pthread_cond_init(&condition, 0) );
}

// only called when step is dead.
// destroy all resourcen
chain::internalstep::~internalstep() {
    // delete dynamically allocated thingies
    uddeleter(actualudptr);
    iqdeleter();
    oqdeleter();
    sdeleter();
    // erase all thunks/curries
    setud.erase();
    setqd.erase();
    setstepid.erase();
    sdeleter.erase();
    iqdeleter.erase();
    oqdeleter.erase();
    udmaker.erase();
    uddeleter.erase();
    threadfn.erase();
    // destroy the mutex & conditionvar
    ::pthread_mutex_destroy(&mutex);
    ::pthread_cond_destroy(&condition);
}


chain::internalq::internalq(const string& tp):
    actualqptr(0), elementtype(tp)
{}

chain::internalq::~internalq() {
    qdeleter();
    // erase all thunks/curries
    enable.erase();
    disable.erase();
    delayed_disable.erase();
    qdeleter.erase();
}

// The stepfn_type: combines a pointer to an actual step
// and a set of functionpointers. When called, the 
// steps' mutex will be held, the function executed,
// the condition variable will be broadcast and finally,
// the mutex will be released again
chain::stepfn_type::stepfn_type(stepid s, curry_type ct):
    step(s), calldef(ct)
{}



//
// the actual chainimpl
//


chain::chainimpl::chainimpl() :
    closed(false), running(false), joining(false), cancelled(false), nthreads(0)
{
    PTHREAD_CALL( ::pthread_mutex_init(&mutex, NULL) );
    PTHREAD_CALL( ::pthread_cond_init(&condition, NULL) );
}

// Only allow a chain to run if it's closed and not running yet.
void chain::chainimpl::run() {
    EZASSERT2(closed==true && running==false, chainexcept, EZINFO("closed=" << closed << ", running=" << running));

    // Great. Now we begin running the chain,
    // setting it up from the end. We can nicely
    // Use the std::vector reverse iterators here.
    sigset_t                       oldset, newset;
    unsigned int                   n;
    ostringstream                  err;
    pthread_attr_t                 attribs;
    struct sched_param             parms;
    steps_type::reverse_iterator   sptrptr;
    queues_type::reverse_iterator  qptrptr;

    // First things first - set the amount of running threads to "0"
    this->nthreads = 0;

    // Make sure that 'haveUD' is false in every step before trying
    // to start the threads. It will be set to true once we've made
    // sure the userdata member has been filled in.  If anything
    // goes wrong trying to run, the cleanup function(s) will know
    // wether or not to be run
    for(steps_type::iterator isptrptr=steps.begin(); isptrptr!=steps.end(); isptrptr++)
        (*isptrptr)->haveUD = false;

    // Set the thread attributes for our kinda threads
    PTHREAD_CALL( ::pthread_attr_init(&attribs) );
    PTHREAD_CALL( ::pthread_attr_setdetachstate(&attribs, PTHREAD_CREATE_JOINABLE) );
    // Only set scheduling policy if we have root?
    if( ::geteuid()==0 ) {
        PTHREAD_CALL( ::pthread_attr_setschedpolicy(&attribs, SCHED_RR) );
        parms.sched_priority = sched_get_priority_max(SCHED_RR);
        PTHREAD_CALL( ::pthread_attr_setschedparam(&attribs, &parms) );
    }

    // Make the threads start with all signals blocked. Then,
    // each thread can decide if and if so which signals are 
    // to be unblocked.
    EZASSERT(sigfillset(&newset)==0, chainexcept);
    PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &newset, &oldset) );

    // Since the chain is closed, we are sure that there ARE
    // elements in the steps- and queues vectors, hence can
    // do the following unconditionally.
    
    // Enable all queues
    for(qptrptr=queues.rbegin(); qptrptr!=queues.rend(); qptrptr++)
        (*qptrptr)->enable();

    // Note: the variable "n" is initialized inside the loop
    // to the number of threads that need to be created.
    // At the end of the loop the value is tested againt 0
    // since if it is NOT 0, this means at least one of the
    // threads couldn't be created.
    try {
        for(sptrptr=steps.rbegin(), n=0; n==0 && sptrptr!=steps.rend(); sptrptr++) {
            internalstep* is = (*sptrptr);

            // Create a new UserData thingy.
            // First call the UserData-maker thunk
            is->udmaker();
            // Call upon another curried thing to transform it to void*
            // (it typesafely extracts "UserData*" and casts to void*
            is->udtovoid(&is->udmaker);
            // now we can extract the void* from that one.
            // Nice thing is that at this point we have NO clue about
            // what the actual type of UserData IS (nor do we NEED to know)
            // We DO need to store the actual pointer.
            is->udtovoid.returnval(is->actualudptr);
            // And by now we DO have filled in the acutaludptr so any
            // cancellation functions registered for this step may expect to
            // find userdata
            is->haveUD = true;

            // Update the steps' sync_data<UserData> with the new
            // UserDataptr. Also make sure the cumulative queuedepth
            // gets set. This never changes once the chain is closed, however, 
            // we do not know this value UNTIL the chain is closed, and not
            // at the time of adding the step. By always setting
            // it, we don't care when we finally knew what value it was -
            // it will always be set BEFORE the thread will actually
            // be able to access the value.
            is->setud(is->actualudptr);
            is->setqd(is->qdepth);
            is->setstepid(is->stepid);
            is->setcancel(false);

            // Create the threads!
            // Also store the number of threads in the runstepargs 
            // structure so terminating threads know when they are
            // last one to leave
            n               = is->nthread;
            is->rsa.nthread = is->nthread;
            while( n ) {
                int         rv;
                pthread_t*  tidptr = new pthread_t;

                // Only add the threadid if succesfully created!
                rv = ::pthread_create(tidptr, &attribs, &chain::run_step, &is->rsa); 
                if( rv!=0 ) {
                    err << "chain/run: failed to create thread: " << ::strerror(rv);
                    break;
                }
                // And another thread created - note there is a difference
                // between the total amount of threads and the amount of
                // threads created for a particular step.
                this->nthreads++;
                is->threads.push_back(tidptr);
                n--;
            }
        }
    }
    catch( const std::exception& e ) {
        err << "chain/run: " << e.what();
    }
    catch( ... ) {
        err << "chain/run: caught deadly unknown exception" << endl;
    }
    // Restore the old signalmask
    PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &oldset, 0) );
    // Do some cleanup
    PTHREAD_CALL( ::pthread_attr_destroy(&attribs) );

    // If we didn't reach the end of the chain something went wrong
    // and we stop the chain (all the ones we've started so far).
    // Preset running to true - in case of error "->stop()" will reset it
    // back to false after finishing.
    running = true;
    cancelled = false;
    if( sptrptr!=steps.rend() ) {
        cerr << "chain/run: failed to start the chain. Stopping." << endl
             << err.str() << endl;
        this->stop();
    }
    EZASSERT2(this->running==true, chainexcept, EZINFO(err.str()));
}

// delayed-disable the first queue and all other steps
// should finish cleanly. Much less brutal than stop().
// You must be reasonably sure that your chain adheres
// to the bqueue-disabled semantics for detecting
// a stop ....
// Registered cancellationroutines are first called
void chain::chainimpl::gentle_stop() {
    stop( true ); // call stop, but be gentle about it
    return;
}

// delayed-disable the first queue and all other steps
// should finish cleanly. 
// You must be reasonably sure that your chain adheres
// to the bqueue-disabled semantics for detecting
// a stop ....
void chain::chainimpl::delayed_disable() {
    if( !running )
        return;
    if( !closed )
        return;
    if ( this->cancelled )
        return;
           
    // disable the producers' queue
    (*queues.begin())->delayed_disable();
    return;
}

// Unconditionally returns the chain to clean state.
// The steps are kept but all threads are stopped
// and the runtime-info is cleared such it may be run again
// at a later stage.
void chain::chainimpl::stop( bool be_gentle ) {
    if( !this->running )
        return;
    if( !this->closed )
        return;
    if ( this->cancelled )
        return;

    // Ok, there is something to stop
    queues_type::iterator qptrptr;
   
    // first do the user-registered cancellations
    this->do_cancellations();
    
    // Inform the sync_type<>'s of the cancellation
    this->cancel_synctype();

    if ( be_gentle ) {
        // disable the producers' queue
        (*queues.begin())->delayed_disable();
    }
    else {
        // now bluntly disable ALL queues
        for(qptrptr=queues.begin(); qptrptr!=queues.end(); qptrptr++) 
            (*qptrptr)->disable();
    }
    
    cancelled = true;
    // sais it all ...
    this->join_and_cleanup();
}

bool chain::chainimpl::empty(void) const {
    return this->steps.empty();
}

void chain::chainimpl::nthread(stepid s, unsigned int num_threads) {
    // only allow setting nthread if not running yet.
    // chain doesn't need to be closed yet, you may call this fn whilst
    // building up the processing chain. 
    // do NOT allow setting 'no threads'
    // Separate the clauses such that in case of error, the user
    // actually knows which one was the culprit.
    EZASSERT(running==false, chainexcept);
    EZASSERT(s<steps.size(), chainexcept);
    EZASSERT(num_threads>0, chainexcept);

    steps[s]->nthread = num_threads;
    return;
}

// Loop over all steps and set the
// "cancel" flag for them to true
void chain::chainimpl::cancel_synctype() {
    stepid  s;

    for(s=0; s<steps.size(); s++) 
        this->communicate(s, makethunk(steps[s]->setcancel, true));
}

void chain::chainimpl::join_and_cleanup() {
    if ( !running )
        return;

    if ( joining ) {
        while ( running ) {
            PTHREAD_CALL( ::pthread_cond_wait( &condition, &mutex ) );
        }
    }
    else {
        joining = true;
        steps_type::iterator    sptrptr;
        {
            mutex_unlocker unlocker( mutex );
            void*                   voidptr;
            
            // Join everyone - all cancellations should have been processed
            // and at least of the queues was (delayed)disabled, triggering
            // a chain of delayed disables.
            //
            // Loop over all steps and for each step, join all
            // threads that were executing that step
            for(sptrptr=steps.begin(); sptrptr!=steps.end(); sptrptr++) {
                tid_type::iterator  thrdidptrptr;
                
                for(thrdidptrptr = (*sptrptr)->threads.begin();
                    thrdidptrptr != (*sptrptr)->threads.end();
                    thrdidptrptr++)
                    ::pthread_join(**thrdidptrptr, &voidptr);
            }
        }
        // And the queue is not running anymore
        running = false;
        joining = false;

        // Great. All threads have been joined. Time to clean up.
        // Before we throw away the userdata's, give the registered cleanup
        // functions a chance to do *their* thing.
        // We completely re-use the "communicate()" method, which does
        // mutexlocking etc, which is, at this point, a tad superfluous since
        // there are no more threads referring to that data. However, a lot of
        // errorchecking and exception-catching is done inside of that, which we
        // *don't* want to duplicate
        cancellations_type::iterator  cleanup;
        for( cleanup=cleanups.begin();
             cleanup!=cleanups.end();
             cleanup++ )
            this->communicate(cleanup->step, cleanup->calldef);
        
        // cleaning up the user data in reversed order because,
        // the last step might disable an interchain queue, freeing the memory
        // allocated by a memory pool belonging to an earlier step

        // a better solution might be to do this garbage collection in a 
        // seperate thread, because steps in another chain/runtime might keep 
        // the references data allocated in this chain, for now this is solved 
        // by putting a timeout on the deallocation of the data
        // see pool.{cc|h}, the descructor of blockpool
        // with the garbage collection scheme this timeout can be 
        // removed/infinte
        
        // the disadvantage of this scheme is that the garbage collection 
        // thread have to be joined after the cleanup of all runtimes

        // All the userdata must go.
        steps_type::reverse_iterator revsptrptr;
        for(revsptrptr=steps.rbegin(); revsptrptr!=steps.rend(); revsptrptr++) {
            internalstep*   sptr = (*revsptrptr);
            
            // call the registered delete function and indicate
            // deletion by setting the actualudptr to 0
            sptr->uddeleter(sptr->actualudptr);
            sptr->actualudptr = 0;
        }
        
        PTHREAD_CALL( ::pthread_cond_broadcast(&condition) );
        
    }
    return;
}

// Process all cancellations - that is, if we're running
void chain::chainimpl::do_cancellations() {
    if( !running )
        return;

    // Loop over all the cancellations
    cancellations_type::iterator  cancel;
    for( cancel=cancellations.begin();
         cancel!=cancellations.end();
         cancel++ )
            this->communicate(cancel->step, cancel->calldef);
}

void chain::chainimpl::do_finals() {
    if( !running )
        return;

    // Loop over all registered finally() routines
    finals_type::iterator  final;
    for( final=finals.begin();
         final!=finals.end();
         final++) {
            try {
                (*final)();
            }
            catch( const exception& exp ) {
                cerr << "Exception whilst doing final() - " << exp.what() << endl;
            }
            catch( ... ) {
                cerr << "Unknown exception whilst doing final()" << endl;
            }
    }
}

// Execute some function on the userdata for step 's'.
// It does it with the mutex held and the condition
// will be automatically broadcasted.
//void chain::chainimpl::communicate_c(stepid s, curry_type ct) {
//    this->communicate(s, ct);
//}

void chain::chainimpl::communicate(stepid s, curry_type ct) {
    // Make sure we can sensibly execute the code
    // Separate the clauses such that in case of error, the user
    // actually knows which one was the culprit
    //EZASSERT(running==true, chainexcept);
    EZASSERT(s<steps.size(), chainexcept);
    // Assert that the argument of the curried thing
    // equals the argument of the step it wants to
    // execute on/with. Typically it is of type "pointer-to-userdata",
    // ie: the curry_type must take a pointer-to-userdata as
    // single argument.
    thunk_type     thunk = makethunk(ct, steps[s]);

    this->communicate(s, thunk);
    thunk.erase();
}

void chain::chainimpl::communicate(stepid s, thunk_type tt) {
    // Make sure we can sensibly execute the code
    // Separate the clauses such that in case of error, the user
    // actually knows which one was the culprit
    //EZASSERT(running==true, chainexcept);
    EZASSERT(s<steps.size(), chainexcept);
    // Grab hold of the correct step, so's we have access
    // to the mutex & condition variable
    internalstep*  isptr = steps[s];

    mutex_locker   steplock( isptr->mutex );

    tt();

    // Ok, now we can broadcasts (if no-one was waiting for
    // the condition variable it is a no-op, otherwise those
    // waiting for it may need to re-evaluate their condition)
    ::pthread_cond_broadcast(&isptr->condition);
}

void chain::chainimpl::register_cancel(stepid stepnum, curry_type ct) {
    // Assert it's safe to register this'un.
    // Separate the clauses such that in case of error, the user
    // actually knows which one was the culprit
    // Only allow registering when not running.
    // cancel methods after the chain is closed.
    EZASSERT(running==false, chainexcept);
    EZASSERT(stepnum<steps.size(), chainexcept);

    cancellations.push_back( stepfn_type(stepnum, ct) );
}

void chain::chainimpl::register_cleanup(stepid stepnum, curry_type ct) {
    // Assert it's safe to register this'un.
    // Separate the clauses such that in case of error, the user
    // actually knows which one was the culprit
    // Only allow registering when not running.
    // cancel methods after the chain is closed.
    EZASSERT(running==false, chainexcept);
    EZASSERT(stepnum<steps.size(), chainexcept);

    cleanups.push_back( stepfn_type(stepnum, ct) );
}

void chain::chainimpl::register_final(thunk_type tt) {
    finals.push_back( tt );
}


chain::chainimpl::scoped_lock_type chain::chainimpl::scoped_lock() {
    return chain::chainimpl::scoped_lock_type( new mutex_locker(mutex) );
}

// If the chainimpl is to be deleted, this means no-one's referencing
// us anymore. This in turn means that the steps and the queues may
// go; we cannot be restarted ever again.
chain::chainimpl::~chainimpl() {
    // Make sure we're stopped ...
    this->stop();

    // Now delete all steps and queues
    steps_type::iterator   sptrptr;
    for(sptrptr=steps.begin(); sptrptr!=steps.end(); sptrptr++)
        delete (*sptrptr);

    queues_type::iterator  qptrptr;
    for(qptrptr=queues.begin(); qptrptr!=queues.end(); qptrptr++) 
        delete (*qptrptr);

    // And erase all registered cancellations
    cancellations_type::iterator  cancel;
    for( cancel=cancellations.begin();
         cancel!=cancellations.end();
         cancel++ )
            cancel->calldef.erase();

    // Erase all registered final functions
    finals_type::iterator  final;
    for( final=finals.begin();
         final!=finals.end();
         final++ )
            final->erase();

    ::pthread_cond_destroy(&condition);
    ::pthread_mutex_destroy(&mutex);
}

chain::runstepargs::runstepargs(thunk_type* tttptr,
                                thunk_type* ddoptr,
                                thunk_type* diptr,
                                chainimpl*  impl):
    threadthunkptr(tttptr), delayeddisableoutqptr(ddoptr),
    disableinqptr(diptr), thechain(impl), nthread(0)
{
    PTHREAD_CALL( ::pthread_mutex_init(&mutex, 0) );
}

// Run a thunk in a thread. Hoopla.
void* chain::run_step(void* runstepargsptr) {
    runstepargs*  rsaptr = (runstepargs*)runstepargsptr;
    
    try {
        (*rsaptr->threadthunkptr)();
    }
    catch( const std::exception& e ) {
        cerr << "OH NOES! A step threw an exception:" << endl
             << "**** " << e.what() << endl;
        push_error( error_type(-1, string("[chain::run_step/step threw exception] ")+e.what()) );
    }
    catch( ... ) {
        cerr << "OH NOES! A step threw an unknown exception!" << endl;
        push_error( error_type(-1, "[chain::run_step/step threw exception] unknown exception") );
    }
    // And now delayed-disable this ones' output queue.
    // And bluntly disable the input q.
    // The fact that WE are executing this code means that WE
    // have stopped popping stuff from an inputqueue.
    // If we are the last thread that stopped popping, better inform
    // the step upstream that no-one is popping anymore.
    // IF our inputQ WAS already (delayed)disabled then this adds nothing,
    // however, if it was NOT it allows the "stopsignal" also to
    // go upstream rather than just downstream.
    unsigned int  n;

    // Hold the mutex for as short as possible.
    // Copy out the updated "rsaptr->nthread" value.
    PTHREAD_CALL( ::pthread_mutex_lock(&rsaptr->mutex) );
    if( rsaptr->nthread )
        rsaptr->nthread--;
    n = rsaptr->nthread;
    PTHREAD_CALL( ::pthread_mutex_unlock(&rsaptr->mutex) );

    // If we were last of our step ... do the signalling!
    if( n==0 ) {
        (*rsaptr->delayeddisableoutqptr)();
        (*rsaptr->disableinqptr)();
    }

    // Do the same check to see if we were the last one
    // of the chain
    {
        chain::chainimpl::scoped_lock_type locker = rsaptr->thechain->scoped_lock();
        if( rsaptr->thechain->nthreads )
            rsaptr->thechain->nthreads--;
        n = rsaptr->thechain->nthreads;
    }
    if( n==0 )
        rsaptr->thechain->do_finals();
    return (void*)0;
}
