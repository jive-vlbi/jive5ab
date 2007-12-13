// implementation
#include <iostream>

// Yah we rly need to lock during outputting stuff
// to the screen ...
#include <pthread.h>

// for ::strerror()
#include <string.h>

static int             dbglev_val   = 1;
// if msglevel>fnthres_val level => functionnames are printed in DEBUG()
static int             fnthres_val  = 2; 
static pthread_mutex_t evlbidebug_cerr_lock = PTHREAD_MUTEX_INITIALIZER;

int dbglev_fn( void ) {
    return dbglev_val;
}

int dbglev_fn( int n ) {
    int rv = dbglev_val;
    dbglev_val = n;
    return rv;
}

int fnthres_fn( void ) {
    return fnthres_val;
}

int fnthres_fn( int n ) {
    int rv = fnthres_val;
    fnthres_val = n;
    return rv;
}

void do_cerr_lock( void ) {
    int rv;
    if( (rv=::pthread_mutex_lock(&evlbidebug_cerr_lock))!=0 ) {
        std::cerr << "do_cerr_lock() failed - " << ::strerror(rv) << std::endl;
    }
    //std::cerr << "{" << ::pthread_self() << "}" << std::endl;
}
void do_cerr_unlock( void ) {
    int rv;
    //std::cerr << "{" << ::pthread_self() << "}" << std::endl;
    if( (rv=::pthread_mutex_unlock(&evlbidebug_cerr_lock))!=0 ) {
        std::cerr << "do_cerr_unlock() failed - " << ::strerror(rv) << std::endl;
    }
}
