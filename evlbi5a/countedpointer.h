//
// countedpointer - define copy/assignement etc. for pointers such
// that the memory the're pointing at only gets deleted if nobody's
// referencing it anymore!
//
//   Author:  Harro Verkouter,  06-08-1999
//   Import into mark5a s/w     25-09-2007
//
//   $Id$
//
//   $Log$
//   Revision 1.1.1.1  2007/09/28 10:16:41  jive_cc
//   Rewrite of Mark5A
//
//   Revision 1.11  2005/09/15 14:58:02  verkout
//   HV: As per c++ std - all pointers may be casted to void* and hence countedpointers should be able to do so too
//
//   Revision 1.10  2005/03/17 16:38:51  verkout
//   ${logmsg}
//
//   Revision 1.9  2004/11/03 21:15:51  verkout
//   HV: - This one is now put in namespace pcint. To avoid clashes with,
//         e.g. JCCS::countedpointer...
//       - COMPLETELY re-vamped the locking/ref-counting. Should now be thread-safe...
//         ('t wasn't, of course...*sigh*)
//
//   Revision 1.8  2002/11/04 14:47:09  loose
//   Minor: just a few additions to keep GCC 3.x happy ;-)
//
//   Revision 1.7  2002/10/25 14:12:22  verkout
//   HV: *countedpointer now try to keep track of memleaks. Not 100% safe yet..
//
//   Revision 1.6  2002/07/23 11:48:58  loose
//   Removed std:: form all calls to pthread methods, cause they reside in the
//   global namespace, rather than namespace std.
//
//   Revision 1.5  2002/06/06 14:39:16  verkout
//   HV: Removed the awful 'getPtr' method from both flavours of countedpointer.
//
//   Revision 1.4  2002/04/05 07:27:52  verkout
//   HV: Added more thread-safety (I hope). Added 'getRefCount' method so users can inspect the actual refcount. Maybe the fn will go away in the future (it was mainly for debugging purposes).
//
//   Revision 1.3  2002/03/21 23:21:34  verkout
//   HV: (hopefully)Made the countedpointer yet a little more threadsafe. mysleep now returns a boolean value. Should indicate whether or not all the time was slept
//
//   Revision 1.2  2002/02/11 09:10:42  verkout
//   Made the counted pointer class MT-safe.
//
//   Revision 1.1.1.1  2001/11/12 08:00:06  verkout
//   PCIntCode - initial stuff
//
//   Revision 1.1.1.1  2000/03/17 14:55:39  verkout
//   HV: The connection library! - imported
//
//
#ifndef EVLBI5A_COUNTEDPOINTER_H
#define EVLBI5A_COUNTEDPOINTER_H


#include <typeinfo>
#include <vector>
#include <iostream>
#include <exception>
#include <string>
#include <sstream>

#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <pthreadcall.h>

struct countedpointerexception:
    public std::exception
{
    countedpointerexception( const std::string& m ):
        message( m )
    {}
    
    virtual const char* what( void ) const throw() {
        return message.c_str();
    }
    virtual ~countedpointerexception( void ) throw()
    {}

    const std::string  message;
};


#define DOTHROW(msg) \
    do {\
        std::ostringstream c0untedh_m_str;\
        c0untedh_m_str << msg;\
        throw countedpointerexception(c0untedh_m_str.str());\
    } while(0);



template <class T>
class countedpointer
{
public:
    // An empty (null) pointer
    countedpointer();
    
    // Take over the ownership of 'ptr.
    // 'ptr' must have come from a call to 'new' (ie not new[]!)
    countedpointer( T* ptr );
    
    // Define copy/assignment
    countedpointer( const countedpointer<T>& other );
    
    //  Assignment
    const countedpointer<T>& operator=( const countedpointer<T>& other );
    
    //  cast to bool, can use the object in constructions like:
    //
    //  if( countedptrvariable ) {
    //       cout << "Hello world!" << endl;
    //  }
    operator     bool() const;
    
    
    // Make the thing behave like a real pointer:
    // implement dereference and 'pointer-to-member' access
    T*           operator->();
    const T*     operator->() const;

    T&           operator*();
    const T&     operator*() const;
  
	// Actually, in C++ *all* pointers may be 
	// cast to 'void *' so in order to make
	// this thang behave like a 'real' pointer
	// we must support that tooooo!!!
	operator void*( void );
	operator const void*( void ) const;

	// Nice one.. templated conversion?
	// do a dynamic_cast<> so you can
	// upcast to derived.
	// Only allow *if* we can upcast!
	// This uses "some magic" *cough*
	// to get this done and quote typesafe unquote
	template <typename U>
	operator countedpointer<U>( void ) const {
		countedpointer<U> rv;

		// Before accessing 'myPointer' and
		// 'myPointer->ptr' we first must
		// lock ourselves and our implementation:
		//
		// By first locking 'this' we can guarantee
		// that 'this.myPointer' doesn't get clobbered
		// and by locking 'this.myPointer' we can guarantee
		// that the refcnt doesn't get clobbered [or:
		// doesn't get to be zero since it will always
		// be 'this' that's still referring to 'this.myPointer'
		while( true ) {
			int tryresult;

			this->lock();
			tryresult = this->myPointer->trylock();
		
			// ok, we HAVE locked 'this' and *may* have locked
			// 'this->myPointer'. If someelse had the shared storage
            // locked, we sleep a bit an retry later
			if( tryresult==EBUSY ) {
				this->unlock();	
                ::usleep( 100 ); // 100 microseconds of sleep... enough?
				continue;
			} else if( tryresult==0 ) {
                // weehee! got locks on both. just break from the loop w/o unlocking
				break;
			}
            // Ok. neither BUSY nor successfull lock. Unlock and throw up!
			this->unlock();
			DOTHROW("countedpointer<" << typeid(T).name() << ">::operator " <<
					"countedpointer<" << typeid(U).name() << ">()" << 
					"/Failed to trylock!!!!" << std::endl << "   Error was " <<
					::strerror(tryresult) << std::endl);
		}
		
		// Yessss! we got locks!
		U*                uptr( (myPointer->ptr)?(dynamic_cast<U*>(myPointer->ptr)):(0) );

		// cant't release locks yet: we may have to 
		// do something with uptr still (if uptr!=0, that is)
		// we must ensure that as long as the refcount for
		// myPointer->ptr (which is same as uptr if uptr!=0)
		// is not updated, nobody tinkers with the refcount.
		// See, if uptr is !=0 at this point, it means that
		// the upcast (dynamic_cast) succeeded, which means
		// that we're going to create a new CountePointer
		// to the same object which means that the refcount
		// must be upped by one which means that nobody should
		// be able to delete it out from under us...

		// uptr == 0 IF (this pointed at nothing OR we couldn't dynamic_cast to U)
		if( uptr!=0 ) {
			// We need to construct a new countedpointer and
			// we'll need to re-interpret the myPointer
			// as pointer to a different type...
			// "delegate" to private c'tor, dedicated to
			// re-interpreting...
			rv.reInterpret( myPointer );
		}
		// At this point all the bookkeeping is ok again...
		// release the locks!
		this->myPointer->unlock();
		this->unlock();

		// and return what we got
		return rv;
	}

    // Use placement-new to share the storage between
    // the T and V versions of the shared ptr.
    // Note: the argument to the c'tor is NOT USED, it's
    // merely to discriminate between c'tors (compare
    //   operator++() and operator++(int) to discriminate
    // between pre- and postfix operator++ ...
    // This should be a private method but then we couldn't make it
    // visible from different CountedPointer types
	template <typename V>
	void reInterpret( const V* cpbptr ) {
		myPointer = new ((void*)cpbptr) typename countedpointer<T>::_cPtrBlock((unsigned int)0);
	}

    //  Destructor
    ~countedpointer();
    
private:
    //  The private parts....
    struct cPtrBlock {
		public:
			T*              ptr;
			unsigned long   refcnt;

			//  Constructors for this struct....
			cPtrBlock();              // empty; ptr=0 && refcnt=1
			cPtrBlock( T* ptrvalue ); // ptr=ptrvalue && refcnt=1
			
			// Re-interpret constructor:
			// does not *initialize* values, only increments refcount by one
            // Used by 'reInterpret()'.
			cPtrBlock( unsigned int );
		
			// locking interface
			void         lock( void ) const;
			void         unlock( void ) const;
			// the trylock *does* return an errorcode;
			// the caller may decided what to do.
			// Iff retval==0, the lock was acquired
			int          trylock( void ) const;
	
			~cPtrBlock();

		private:
			void         initMutex( void );

			// This mutex will be used for locking the transactions
			// for this object
			mutable pthread_mutex_t  mtx;
    };
   
    //  The member(s) we have....
    cPtrBlock*               myPointer;
	mutable pthread_mutex_t  mtx;

    //  Private methods
	void lock( void ) const;
	void unlock( void ) const;
	void initMutex( void );
};


//  The private struct stuff
template <class T>
countedpointer<T>::cPtrBlock::cPtrBlock( ) :
    ptr( 0 ), refcnt( 1 )
{
	this->initMutex();
}

template <class T>
countedpointer<T>::cPtrBlock::cPtrBlock( T* ptrvalue ) :
    ptr( ptrvalue ), refcnt( 1 )
{
	this->initMutex();
}

// this pseudo c'tor doesn't need to initMutex()
// since it's basically a "reinterpret" c'tor
// only upping the refcnt. All other stuff is
// already initialized
template <class T>
countedpointer<T>::cPtrBlock::cPtrBlock( unsigned int ) {
	++refcnt;
}


template <class T>
void countedpointer<T>::cPtrBlock::lock( void ) const {
    PTHREAD_CALL( ::pthread_mutex_lock(&mtx) );
}
template <class T>
void countedpointer<T>::cPtrBlock::unlock( void ) const {
    PTHREAD_CALL( ::pthread_mutex_unlock(&mtx) );
}

template <class T>
int countedpointer<T>::cPtrBlock::trylock( void ) const {
	int   rv( 0 );
    PTHREAD_CALL( (rv=::pthread_mutex_trylock(&mtx)) );
    return rv;
}
#if 0
	int   rv( 0 );
	rv = ::pthread_mutex_trylock( &mtx );
	if( rv && rv!=EBUSY && rv!=EDEADLK ) {
		DOTHROW( "countedpointer<" << typeid(T).name() << ">::cPtrBlock::trylock()/"
                << "::pthread_mutex_trylock() fails: " << ::strerror(rv)
                << std::endl);
	}
	return rv;
}
#endif

template <class T>
void countedpointer<T>::cPtrBlock::initMutex( void ) {
#if 0
	pthread_mutexattr_t  attr;

	PTHREAD_CALL( ::pthread_mutexattr_init(&attr) );
	PTHREAD_CALL( ::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) );
	PTHREAD_CALL( ::pthread_mutex_init(&mtx, &attr) );
	PTHREAD_CALL( ::pthread_mutexattr_destroy(&attr) );
#endif
	PTHREAD_CALL( ::pthread_mutex_init(&mtx, 0) );
}

template <class T>
countedpointer<T>::cPtrBlock::~cPtrBlock() {
	PTHREAD_CALL( ::pthread_mutex_destroy(&mtx) );
}




//
//
//  The counted pointer class itself
//
//


//    DEFAULT C'TOR
// Each counted pointer object (even default ones)
// get a pointer to an implementation (a cPtrBlock).
// This has the advantage that we never have to check
// for existance of an implementation
template <class T>
countedpointer<T>::countedpointer( ) :
    myPointer( new typename countedpointer<T>::cPtrBlock() ) {
	this->initMutex();
}

//    TAKE OVER RESPONSIBILITY FOR SOME STORAGE
template <class T>
countedpointer<T>::countedpointer( T* ptr ) :
    myPointer( new typename countedpointer<T>::cPtrBlock(ptr) ) {
	this->initMutex();
}

//     THE COPY C'TOR
template <class T>
countedpointer<T>::countedpointer( const countedpointer<T>& other ) :
	myPointer( 0 ) {
	this->initMutex();

	// Lock 'other'. Make sure that whilst we're 
	// copying, nobody tinkers with 'other'
    // No need to lock ourselves since we're in the process of being constructed
    // so we're inaccessible anyway!
	while( true ) {
		int tryresult;

		other.lock();
		tryresult = other.myPointer->trylock();
		
		// ok, we HAVE locked 'other' and *may* have locked
		// 'other.myPointer'
		// EDEADLK shouldn't be tested since the same thread
		// cannot/shouldn't have a lock on 'other.myPointer'...
		if( tryresult==EBUSY ) {
			other.unlock();	
			::usleep( 100 );
			continue;
		} else if( tryresult==0 ) {
			break;
		}
		other.unlock();
		DOTHROW("countedpointer<" << typeid(T).name() << ">::countedpointer("
                << "const countedpointer&)" <<
                "/Failed to trylock!!!!" << std::endl << "   Error was " <<
                ::strerror(tryresult) << std::endl);
	}

	// the object we are copying exists, and hence,
	// it must (eventually) have come from a call to:
	//
	// countedpointer<T>()     (=default; myPointer is filled in)
	// countedpointer<T>( T* ) (myPointer filled in; we took over storage)
	// countedpointer<T>( countedpointer<T>& ) 
	//                         (=copy c'tor => we are copying an object, 
	//                          which must, eventually, have come from...
	//                          etc...)	
	myPointer = other.myPointer;

	// Now we can safely increment the refcount
	myPointer->refcnt++;

	// Unlock 'other'
	other.myPointer->unlock();
	other.unlock();
}

//   THE ASSIGNMENT OPERATOR
template <class T>
const countedpointer<T>& countedpointer<T>::operator=( const countedpointer<T>& other ) {
	// First check if we're not assigning to ourselves
	// if yes, bail out asap. Otherwise, some work to do!
	if( this==&other )
		return *this;

	// Local variables
	bool                                     docleanup;
	typename countedpointer<T>::cPtrBlock*   oldptr( 0 );
	typename countedpointer<T>::cPtrBlock*   newptr( 0 );

	// 1. Grab locks on 'other'-s stuff
	//    By first locking 'other' we can guarantee
	//    that 'other.myPointer' doesn't get clobbered
	//    and by locking 'other.myPointer' we can guarantee
	//    that the refcnt doesn't get clobbered [or:
	//    doesn't get to be zero since it will always
	//    be 'other' that's still referring to 'other.myPointer'
	while( true ) {
		int tryresult;

		other.lock();
		tryresult = other.myPointer->trylock();
		
		// ok, we HAVE locked 'other' and *may* have locked
		// 'other.myPointer'
		if( tryresult==EBUSY ) {
			other.unlock();	
			::usleep( 100 );
			continue;
		} else if( tryresult==0 ) {
			break;
		}
		other.unlock();
		DOTHROW("countedpointer<" << typeid(T).name() << ">::operator=()" <<
                "/Failed to trylock!!!!" << std::endl << "   Error was " <<
                ::strerror(tryresult) << std::endl);
    }

	// 2. Ok we got it. Now we can increment the
	//    refcount and release the refcnt-lock
    //    This ensures it will never be deleted out from
    //    under us whilst giving other objects a chance
    //    to access it again
	other.myPointer->refcnt++;
	other.myPointer->unlock();
	
	// 2a. 'other' is still locked (so no-one can
	//     alter the 'other.myPointer' value so
	//     at this point we can safely copy the
	//     value.
	newptr = other.myPointer;

	// 3. we can unlock other; we've incremented the refcount
	//    as such the object won't be destroyed since
	//    there will be always *us* referring to it.
	//    Also, we have copied the pointer to the refcount
	//    across
	other.unlock();

	// 4. Grab a lock on ourselves. Nobody should mess with
	//    our pointer whilst we are doing that (which includes
	//    somebody else copying our pointer across, see 
	//    points 1. -> 3. above :)
	this->lock();
	oldptr    = myPointer;
	myPointer = newptr;
	this->unlock();

	// 5. Ok. Now our implementation already refers to the 
	//    'new' object (whose refcount cannot become zero
	//    which means that 'myPointer' cannot be invalidated)
	//    What's left is to decrement the refcnt of our
	//    old 'myPointer' whose value is stored in 'oldptr'.
	//    So grab a lock on 'oldptr', decrement refcnt
	//    and clean up if necessary
	oldptr->lock();
	docleanup = ((--oldptr->refcnt)==0);
	oldptr->unlock();

	// 6. If docleanup true we were the last one referring
	//    to oldptr so we can safely cleanup. If nobody
	//    else than us referred to oldptr, they sure as
	//    hell can't get a reference so refcnt can *never*
	//    become nonzero again!!
	if( docleanup ) {
		// destroy the object
		delete oldptr->ptr;
		// and destroy the cPtrBlock
		delete oldptr;
	}
    return *this;
}

	
template <class T>
countedpointer<T>::operator bool() const {
	bool        retval;

	this->lock();
	retval = (myPointer->ptr!=0);
	this->unlock();
	return retval;
}

template <class T>
T* countedpointer<T>::operator->() {
	T*        retval;

	this->lock();
	retval = myPointer->ptr;
	this->unlock();
	return retval;
}

template <class T>
const T* countedpointer<T>::operator->() const {
	const T*        retval;

	this->lock();
	retval = myPointer->ptr;
	this->unlock();
	return retval;
}

template <class T>
T& countedpointer<T>::operator*() {
	T*   rv;
	
	this->lock();
	rv = myPointer->ptr;
	this->unlock();
	if( !rv ) {
		DOTHROW("countedpointer<" << typeid(T).name() << ">::operator* /"
                << "Dereferencing a NULL pointer");
	}
	return *rv;
}

template <class T>
const T& countedpointer<T>::operator*() const {
	const T*   rv;
	
	this->lock();
	rv = myPointer->ptr;
	this->unlock();
	if( !rv ) {
		DOTHROW("countedpointer<" << typeid(T).name() << ">::operator* /"
                << "Dereferencing a NULL pointer");
	}
	return *rv;
}

template <class T>
countedpointer<T>::operator void*( void ) {
	void*   rv;
	
	this->lock();
	rv = myPointer->ptr;
	this->unlock();
	return rv;
}

template <class T>
countedpointer<T>::operator const void*( void ) const {
	const void*   rv;
	
	this->lock();
	rv = myPointer->ptr;
	this->unlock();
	return rv;
}

template <class T>
countedpointer<T>::~countedpointer() {
	bool                                     docleanup;
	typename countedpointer<T>::cPtrBlock*   oldptr( 0 );


	// 1. Grab locks on ourselves. We must be
	//    able to switch myPointer to 'null'
	//    atomically. The saved pointer will
	//    never be invalidated since it is still
	//    always *us* we still refer to it
	//    (only in this method, our implementation
	//    already refers to nothing anymore)
	this->lock();

	// 2. Fine. Switch our implementation to refer to
	//    nothing anymore, saving our old implementation
	oldptr    = myPointer;
	myPointer = 0;
	this->unlock();

	// 3. Good. Now grab a lock on 'oldptr' since we
	//    still have to decrement the refcnt on that
	//    one. Also keep track if we were the last one
	//    referring to 'oldptr'
	oldptr->lock();
	docleanup = ((--oldptr->refcnt)==0);
	oldptr->unlock();

	// 4. Yes we were... delete stuff as necessary
	if( docleanup ) {
		delete oldptr->ptr;
		delete oldptr;
	}
}

//  The private methods
template <class T>
void countedpointer<T>::lock( void ) const {
    PTHREAD_CALL( ::pthread_mutex_lock(&mtx) );
}

template <class T>
void countedpointer<T>::unlock( void ) const {
    PTHREAD_CALL( ::pthread_mutex_unlock(&mtx) );
}

template <class T>
void countedpointer<T>::initMutex( void ) {
    PTHREAD_CALL( ::pthread_mutex_init(&mtx, 0) );
}


#endif
