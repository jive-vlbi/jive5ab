#include <mutex_locker.h>
#include <pthreadcall.h>

mutex_locker::mutex_locker( pthread_mutex_t& m ) : mutex(m) {
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
}

mutex_locker::~mutex_locker( ) {
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
}

mutex_unlocker::mutex_unlocker( pthread_mutex_t& m ) : mutex(m) {
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
}

mutex_unlocker::~mutex_unlocker( ) {
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
}


