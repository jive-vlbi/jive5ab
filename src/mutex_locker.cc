#include <mutex_locker.h>

mutex_locker::mutex_locker( pthread_mutex_t& m ) : mutex(m) {
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
}

mutex_locker::~mutex_locker( ) throw() {
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
}

mutex_unlocker::mutex_unlocker( pthread_mutex_t& m ) : mutex(m) {
    PTHREAD_CALL( ::pthread_mutex_unlock(&mutex) );
}

mutex_unlocker::~mutex_unlocker( ) throw() {
    PTHREAD_CALL( ::pthread_mutex_lock(&mutex) );
}

rw_write_locker::rw_write_locker( pthread_rwlock_t& m ) : mutex(m) {
    PTHREAD_CALL( ::pthread_rwlock_wrlock(&mutex) );
}

rw_write_locker::~rw_write_locker( ) throw() {
    PTHREAD_CALL( ::pthread_rwlock_unlock(&mutex) );
}

rw_read_locker::rw_read_locker( pthread_rwlock_t& m ) : mutex(m) {
    PTHREAD_CALL( ::pthread_rwlock_rdlock(&mutex) );
}

rw_read_locker::~rw_read_locker( ) throw() {
    PTHREAD_CALL( ::pthread_rwlock_unlock(&mutex) );
}

