#ifndef MUTEX_LOCKER_H
#define MUTEX_LOCKER_H

#include <pthread.h>

// simplest scoped pthread_mutex locker
struct mutex_locker {
    mutex_locker( pthread_mutex_t& m ); // locks

    ~mutex_locker(); // unlocks

private:
    pthread_mutex_t& mutex;

    mutex_locker( const mutex_locker& );
    mutex_locker& operator=( const mutex_locker& );
};

// does the reverse of the locker: unlock on init, lock on destroy
struct mutex_unlocker {
    mutex_unlocker( pthread_mutex_t& m ); // unlocks

    ~mutex_unlocker(); // locks

private:
    pthread_mutex_t& mutex;

    mutex_unlocker( const mutex_unlocker& );
    mutex_unlocker& operator=( const mutex_unlocker& );
};

#endif
