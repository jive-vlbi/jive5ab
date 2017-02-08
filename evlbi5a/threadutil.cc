// implementation
// Copyright (C) 2007-2013 Harro Verkouter
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
#include <threadutil.h>

#include <stdlib.h>   // for ::free(), ::srandom_r, ::initstate_r
#include <string.h>   // for ::strerror_r()
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <dosyscall.h>

#include <stdexcept>
#include <iostream>

// dummy empty function meant to install as signal-handler
// that doesn't actually do anything.
// it is necessary to be able to, under linux, wake up
// a thread from a blocking systemcall: send it an explicit
// signal. in order to limit sideeffects we use SIGUSR1
// and install a signal handler "dat don't do nuttin'"
void zig_func(int) {}

void install_zig_for_this_thread(int sig) {
    sigset_t         set;
    struct sigaction act;

    // Unblock the indicated SIGNAL from the set of all signals
    sigfillset(&set);
    sigdelset(&set, sig);

    // install the empty handler 'zig()' for this signal
    act.sa_handler = &zig_func;
    act.sa_flags   = 0;
    sigfillset(&act.sa_mask);
    sigdelset(&act.sa_mask, sig);
    ASSERT_ZERO( ::sigaction(sig, &act, 0) );

    // We do not care about the existing signalset
    ::pthread_sigmask(SIG_SETMASK, &set, 0);
}

void uninstall_zig_for_this_thread(int ) {
    sigset_t         set;

    // Just block all signals
    sigfillset(&set);

    // We do not care about the existing signalset - just block the
    // indicated signal
    ::pthread_sigmask(SIG_SETMASK, &set, 0);
}


namespace evlbi5a {


    // Wrapper around thread-local storage. The tag is there to 
    // be able to disambiguate different TLS types which have same
    // underlying storage (say "int") but mean something different
    template <typename T, typename Tag, void(*deleter)(void*)>
    struct tls_basics {
        static pthread_key_t    tls_key;
        static pthread_once_t   tls_once_guard;

        static void init( void ) {
            int  rc;
            char ebuf[128];

            if( (rc=::pthread_key_create(&tls_key, deleter))!=0 ) {
                if( ::strerror_r(rc, ebuf, sizeof(ebuf)) ) {}
                std::cerr << "tls_basics_type/failed to create TLS key: " << ebuf << std::endl;
                throw std::runtime_error(std::string("tls_basics_type/failed to create TLS key: ") + ebuf);
            }
        }
    };
    template <typename T, typename Tag, void(*deleter)(void*)>
    pthread_key_t tls_basics<T, Tag, deleter>::tls_key;
    template <typename T, typename Tag, void(*deleter)(void*)>
    pthread_once_t tls_basics<T, Tag, deleter>::tls_once_guard;

    // Will keep a pointer to a single object of type T per thread
    template <typename T, typename Tag>
    struct tls_object_type {
        // Type safe deleter
        static void deleter(void* ptr) {
            delete reinterpret_cast<T*>(ptr);
        }

        typedef tls_object_type<T, Tag>            Self;
        typedef tls_basics<T, Tag, &Self::deleter> Basics;

        tls_object_type() {
            ::pthread_once(&Basics::tls_once_guard, Basics::init);
            if( (ptr=reinterpret_cast<T*>(::pthread_getspecific(Basics::tls_key)))==0 ) {
                ::pthread_setspecific(Basics::tls_key, ptr = new T());
            }
        }

        tls_object_type(T* (*m)(void)) {
            ::pthread_once(&Basics::tls_once_guard, Basics::init);
            if( (ptr=reinterpret_cast<T*>(::pthread_getspecific(Basics::tls_key)))==0 ) {
                ::pthread_setspecific(Basics::tls_key, ptr = m());
            }
        }
    
        T*  ptr;

        private:
            // No copy c'tor or assignment
            tls_object_type(tls_object_type const&);
            tls_object_type& operator=(tls_object_type const&);
    };

    // Will keep an array of T of length N per thread
    template <typename T, size_t N, typename Tag>
    struct tls_array_type {
        static const size_t    size = N;
        // Type safe deleter
        static void deleter(void* ptr) {
            delete [] reinterpret_cast<T*>(ptr);
        }
        typedef tls_array_type<T, N, Tag>          Self;
        typedef tls_basics<T, Tag, &Self::deleter> Basics;

        tls_array_type() {
            ::pthread_once(&Basics::tls_once_guard, Basics::init);
            if( (ptr=reinterpret_cast<T*>(::pthread_getspecific(Basics::tls_key)))==0 ) {
                ::pthread_setspecific(Basics::tls_key, ptr = new T[N]());
            }
        }
    
        T*  ptr;

        private:
            // No copy c'tor or assignment
            tls_array_type(tls_array_type const&);
            tls_array_type& operator=(tls_array_type const&);
    };




    template <typename T>
    struct success_check {};

    template <>
    struct success_check<int> {
        static void check_success(int rc) {
            // strerror_r(3) / XSI version returns positive error number in case of error
            // or -1 + set errno depending on glibc version
            if( rc!=0 ) {
                std::ostringstream oss;
                if( rc==-1 )
                    oss << "strerror_r[int/XSI]/failed, return -1, did set errno=" << errno;
                else
                    oss << "strerror_r[int/XSI]/failed, returns errno=" << rc;
                std::cerr << oss.str() << std::endl;
                throw std::runtime_error( oss.str() );
            }
        }
    };
    template <>
    struct success_check<char*> {
        static void check_success(char* rc) {
            // strerror_r(3) / glibc always succeeds - apparently.
            // (the one that returns "char*" that is but our exacting
            // standards (== compiler options) require us to actually
            // *use* the return value of strerror_r ...)
            if( rc==0 )
                throw std::runtime_error(std::string("strerror_r[char*/posix] returns NULL pointer?!!!!"));
        }
    };

    // By wrapping the strerror call we can make it automatically deduce the return type
    // and check accordingly :-)
    // C++11 has decltype() but we don't do c++11 for this project (yet) :-(
    template <typename Ret>
    void lwrap_strerror_r(Ret(*fptr)(int, char*, size_t), int errnum, char* buf, size_t sz) {
        // Execute the call and handle error/success
        success_check<Ret>::check_success( fptr(errnum, buf, sz) );
    }

    namespace tags {
        struct strerror_buf {};
        struct random_buf   {};
    }

    char* strerror(int errnum) {
        // Get thread specific buffer 
        typedef tls_array_type<char, 128, tags::strerror_buf>  strerror_tls_buf_type;
        strerror_tls_buf_type   strerror_tls_buf;

        // Apparently, there can be two flavours of strerror_r - the XSI or non-XSI compliant version
        // which give a different return type. So we capture those differences into some templates
        // to handle each of them appropriately
        lwrap_strerror_r(::strerror_r, errnum, strerror_tls_buf.ptr, strerror_tls_buf_type::size);
        // Passed that hurdle correctly
        return strerror_tls_buf.ptr;
    }

    namespace detail {
        struct m_random_data {
            char                __m_random_state[32]; // more = better
            struct random_data  __m_random_data;

            m_random_data() {
                ::memset(&__m_random_state[0], 0x0, sizeof(__m_random_state));
                ::memset(&__m_random_data,     0x0, sizeof(struct random_data));
            }
        };

        struct m_random_data* mk_random_data( void ) {
            struct m_random_data* n = new m_random_data();
            ::initstate_r(::time(NULL), n->__m_random_state, sizeof(n->__m_random_state), &n->__m_random_data);
            return n;
        }
        // for *rand48(...) functions
        struct drand48_data* mk_random_data_48( void ) {
            struct drand48_data* n = new drand48_data;
            ::memset(n, 0x0, sizeof(struct drand48_data));
            ::srand48_r((long int)::time(NULL), n);
            return n;
        }
    }

    long int random( void ) {
        typedef tls_object_type<detail::m_random_data, tags::random_buf> tls_random_data_type;
        int32_t              rnd;
        tls_random_data_type tls_random_data( &detail::mk_random_data );

        ::random_r(&tls_random_data.ptr->__m_random_data, &rnd);
        return (long int)rnd;
    }

    long int lrand48( void ) {
        typedef tls_object_type<struct drand48_data, tags::random_buf> tls_random_data_type;
        long int             rnd;
        tls_random_data_type tls_random_data( &detail::mk_random_data_48 );

        ::lrand48_r(tls_random_data.ptr, &rnd);
        return rnd;
    }
} // namespace evlbi5a
