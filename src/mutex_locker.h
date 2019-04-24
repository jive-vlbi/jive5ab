#ifndef MUTEX_LOCKER_H
#define MUTEX_LOCKER_H

#include <pthread.h>
#include <pthreadcall.h>

// simplest scoped pthread_mutex locker
struct mutex_locker {
    mutex_locker( pthread_mutex_t& m ); // locks

    ~mutex_locker() throw(); // unlocks

private:
    pthread_mutex_t& mutex;

    mutex_locker( const mutex_locker& );
    mutex_locker& operator=( const mutex_locker& );
};

// does the reverse of the locker: unlock on init, lock on destroy
struct mutex_unlocker {
    mutex_unlocker( pthread_mutex_t& m ); // unlocks

    ~mutex_unlocker() throw(); // locks

private:
    pthread_mutex_t& mutex;

    mutex_unlocker( const mutex_unlocker& );
    mutex_unlocker& operator=( const mutex_unlocker& );
};

// rwlock write locker
struct rw_write_locker {
    rw_write_locker( pthread_rwlock_t& m ); // locks

    ~rw_write_locker() throw(); // unlocks

private:
    pthread_rwlock_t& mutex;

    rw_write_locker( const rw_write_locker& );
    rw_write_locker& operator=( const rw_write_locker& );
};

// rwlock read locker
struct rw_read_locker {
    rw_read_locker( pthread_rwlock_t& m ); // locks

    ~rw_read_locker() throw(); // unlocks

private:
    pthread_rwlock_t& mutex;

    rw_read_locker( const rw_read_locker& );
    rw_read_locker& operator=( const rw_read_locker& );
};

#endif
