// wrapper around those perky pthread_* calls that do not set errno and return -1 but *return* an errno!
#ifndef EVLBI5A_PTHREADCALL_H
#define EVLBI5A_PTHREADCALL_H

#include <sstream>
#include <exception>

// obviously...
#include <pthread.h>

struct pthreadexception:
    public std::exception
{
    pthreadexception( const std::string& m ):
        message( m )
    {}
    
    virtual const char* what( void ) const throw() {
        return message.c_str();
    }
    virtual ~pthreadexception( void ) throw()
    {}

    const std::string  message;
};


#ifdef __GNUC__
#define PTCFUNC " in [" << __PRETTY_FUNCTION__ << "]"
#else
#define PTCFUNC ""
#endif

#define PTCALLLOCATION \
    std::string  fn_( __FILE__); int ln_(__LINE__);

#define PTHREAD2_CALL( a, b ) \
    do { PTCALLLOCATION;\
        int  teh_L0k4l = a;\
        if( teh_L0k4l!=0 ) {\
            std::ostringstream  lclStreAmvar_p6; \
            lclStreAmvar_p6 << fn_ << ":" << ln_ << PTCFUNC << " " <<  #a << " fails - " << ::strerror(teh_L0k4l);\
            b;\
            throw pthreadexception(lclStreAmvar_p6.str());\
        }\
    } while(0)

#define PTHREAD_CALL( a ) \
    PTHREAD2_CALL( a, ; )

// this one is specific for pthread_mutex_trylock(): throws
// if not successfull because of error. So IF you get past
// this one you know you have either locked succesfully or
// the error was EBUSY/EDEADLK.
// If you want to save the returnvalue use this one as follows:
// int   rv;
// PTHREAD_TRYLOCK( (rv=::pthread_mutex_trylock(&mutex)) );
// return rv;
#define PTHREAD_TRYLOCK( a ) \
    do { PTCALLLOCATION;\
        int  the_l0c4l_rv = a;\
        if( the_l0c4l_rv!=0 && the_l0c4l_rv!=EBUSY && the_l0c4l_rv!=EDEADLK ) {\
            std::ostringstream  lclStreAmvar_q8;\
            lclStreAmvar_q8 << fn_ << ":" << ln_ << PTCFUNC << " " <<  #a << " fails - " << ::strerror(the_l0c4l_rv);\
            throw pthreadexception(lclStreAmvar_q8.str());\
        }\
    } while(0)

#endif
