// support per_runtime caching of data and cleanup up runtime is destroyed
// Copyright (C) 2007-2015 Harro Verkouter
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
#ifndef EVLBI5A_PER_RUNTIME_H
#define EVLBI5A_PER_RUNTIME_H

// system headers
#include <map>
#include <cerrno>
#include <cstdio>         // for ::strerror()
#include <string>
#include <exception>
#include <stdexcept>

#include <runtime.h>      // sadly must include this, for we need access to runtime's API
#include <mutex_locker.h> // let's be thread-safe
#include <stringutil.h>


// Since the actual functions typically operate on a runtime environment
// and sometimes they need to remember something, it makes sense to do
// this on a per-runtime basis. This struct allows easy per-runtime
// saving of state.
// Usage:
//   per_runtime<string>  lasthost;
//   ...
//   swap(rte.netparms.host, lasthost[&rte]);
//   cout << lasthost[&rte] << endl;
//   lasthost[&rte] = "foo.bar.bz";
template <typename T>
struct per_runtime {
    // Yah. Public is default in 'struct'. But we'll also have private
    // parts. So. Thus. Deal with it.
    public:
        // Let's get some typedefs out of the way.
        // Follow the types of the underlying map
        typedef std::map<runtime const*, T>            __my_map_type;

        typedef typename __my_map_type::size_type      size_type;

        typedef typename __my_map_type::iterator       iterator;
        typedef typename __my_map_type::const_iterator const_iterator;

        typedef typename __my_map_type::key_type       key_type;
        typedef typename __my_map_type::value_type     value_type;
        typedef typename __my_map_type::mapped_type    mapped_type;


        // set up the lock for this object
        per_runtime() {
            if( ::pthread_mutex_init(&__my_mutex, 0) )
                throw std::runtime_error( std::string("Failed to initialize mutex: ")+repr(errno)+" "+::strerror(errno) );
        }

        // begin()/end()
        iterator       begin( void ) {
            return __my_map.begin();
        }
        const_iterator begin( void ) const {
            return __my_map.begin();
        }
        iterator       end( void ) {
            return __my_map.end();
        }
        const_iterator end( void ) const {
            return __my_map.end();
        }

        // find()
        iterator       find(runtime const* rteptr) {
            return __my_map.find(rteptr);
        }
        const_iterator find(runtime const* rteptr) const {
            return __my_map.find(rteptr);
        }

        // We must intercept "operator[Key]" because that (potentially)
        // adds a new entry to the map. We must lock the runtime
        // because we don't want other 'per_runtime<>' thingies
        // clobbering with the '.key_deleters' member at the 
        // same time that we're doing it
        T& operator[]( runtime const* rteptr ) {
            scopedrtelock  srtl( *const_cast<runtime*>(rteptr) );

            if( rteptr->key_deleters.find((void*)this) == rteptr->key_deleters.end() )
                rteptr->key_deleters[ (void*)this ] = &per_runtime<T>::key_deleter;
            return __my_map[ rteptr ];
        }

        // insert(..) 
        //   (we must intercept these too
        std::pair<iterator, bool> insert(const value_type& val) {
            // value_type == pair<Key, Value>
            runtime const*  rteptr = val.first;
            scopedrtelock   srtl( *const_cast<runtime*>(rteptr) );

            if( rteptr->key_deleters.find((void*)this) == rteptr->key_deleters.end() )
                rteptr->key_deleters[ (void*)this ] = &per_runtime<T>::key_deleter;
            return __my_map.insert(val);
        }

        // and we must intercept .erase( iterator )
        void erase( iterator p ) {
            // Ok, someone explicitly erases an entry from the map
            // so we must remove the key-deleter from the runtime
            // because the entry's already being erased
            runtime const*  rteptr = p->first;
            scopedrtelock   srtl( *const_cast<runtime*>(rteptr) );

            rteptr->key_deleters.erase( (void*)this );
            __my_map.erase( p );
            return;
        }

        size_type erase( runtime const* rteptr ) {
            // See above ".erase( iterator )"
            scopedrtelock   srtl( *const_cast<runtime*>(rteptr) );

            rteptr->key_deleters.erase( (void*)this );
            return __my_map.erase( rteptr );
        }

        ~per_runtime() {
            // Lock ourselves!
            mutex_locker   sml( __my_mutex );

            // for every runtime, de-register the keydeleter.
            // I mean: the per_runtime mapping is going away so
            // the runtimes don't have to remove the keys anymore
            for( typename __my_map_type::iterator p = __my_map.begin(); p!=__my_map.end(); p++)
                p->first->key_deleters.erase( (void*)this );
        }

    private:
        __my_map_type   __my_map;
        pthread_mutex_t __my_mutex;

        // This is a static member function taking two void*.
        // Because it is a member function of this templated object and we
        // also control the arguments being fed to the function call (when
        // it is called) we can be *very* sure about what the void*
        // _actually_ point to [some form of type safety].
        // So it's not a problem to reinterpret the void*
        static void key_deleter(void* m, void* k) {
            runtime const*   rteptr = reinterpret_cast<runtime const*>(k);
            per_runtime<T>*  prtptr = reinterpret_cast<per_runtime<T>*>(m);

            // Do lock the per_runtime<> instance
            mutex_locker   sml( prtptr->__my_mutex );
            DEBUG(5, "key_deleter: attempt to delete key=" << k << std::endl);
            prtptr->__my_map.erase( rteptr );
            DEBUG(5, "             map now has " << prtptr->__my_map.size() << " entries" << std::endl);
        }

};

#endif
