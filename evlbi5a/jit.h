// support just-in-time compilitation of C-code and loading of functionpointers from there
// Copyright (C) 2009-2012 Harro Verkouter
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
#ifndef JIVE5A_JIT_H
#define JIVE5A_JIT_H

#include <string>
#include <vector>

// dynamic library loading
#include <dlfcn.h>

// own stuff
#include <ezexcept.h>
#include <fptrhelper.h>
#include <countedpointer.h>

// Set linker command + extension of shared library based on O/S
#if defined(__linux__) || defined(__sun__)
    #define LOPT  " -shared"
    #define SOEXT ".so"
#elif defined(__APPLE__) && defined(__MACH__)
    #define LOPT  " -dynamiclib"
    #define SOEXT ".dylib"
#else
    #error Undefined system
#endif

// For the "Bits2Build" (32 or 64bit version)
#define BOPT " -m"<<B2B

// Exceptions of this type will be thrown
DECLARE_EZEXCEPT(jit_error)

// API:
//   compile the code in the string [assumed legal C] and
//   return a pointer to the module (in fact 
//   a dlopen handle) - you are responsible for
//   calling jit_close() on the handle when no
//   longer needed.
//
// Once you have a jit_handle you can extract/lookup
// symbols using the 'function_lookup' member function

//
// Usage:
//
// // dynamically create some C-code:
// string       code = "int foo(unsigned char* ptr) { return *ptr=='H'; }";
//
// // compile + load it
// jit_handle   jit = jit_c_compile( code );
// 
// // Now we must have a typedef for the functionpointer
// typedef int (*fptr_type)(unsigned char*);
//
// // get a pointer to the freshly compiled + loaded function
// fptr_type    fnptr_to_foo = jit_handle.function<fptr_type>("foo");
//
// // executed it!
// cout << fnptr_to_foo( "Hello world!" ) << endl;
// // should output "1" [since *ptr == 'H' ]
//
//
// As soon as the object goes out of scope and you did not
// pass it on to someone else, the library + code will be
// closed and deleted.

struct jit_handle;

// Compiles with "-O3 -NDEBUG" - the fact you're using
// this in the first place seems to hint at "I need
// performance".
// Then it loads the generated library and returns a
// handle to the module. Throws on error.
// With the handle you can extract/lookup symbols -
// see below
jit_handle   jit_c_compile(const std::string& code);

// Once you've jit compiled, you can lookup symbols
// as data or as function. The templates ensure it
// will be proper typecast.
//
// The pointers will be returned in the same order as
// the entries in the symbol name list.
typedef std::vector<std::string>  symbolname_type;

// Once you compile + link something dynamic
// you get back one of these. Keep it well
// protected, you need to close it later!
// [it will do the cleanup]
struct jit_handle {
    public:
        // no handle and no filename
        jit_handle();

        // construct a filled in jit handle.
        // Will throw on nullpointer or empty filename
        jit_handle(void* h, const std::string& f);

        inline operator bool(void) const {
            return (impl->handle==0);
        }

        // Because of no official defined conversion between
        // "pointer-to-void" and "pointer-to-function" we 
        // do some more work in the jit_function_lookup()
        // FPTR is the type of the functionpointer you're 
        // extracting it to.
        template <typename FPTR>
        FPTR function(const std::string& symbolname) const {
            return this->function<FPTR>(symbolname_type(1, symbolname))[0];
        }

        template <typename FPTR>
        std::vector<FPTR> function(const symbolname_type& symbolnames) const {
            // Now we need to get our dirty hands on tha symbolz.
            // The statement below [the typedef] will #FAIL as compiletime
            // if we are on a platform where a functionpointer is not
            // the same size as some integral type.
            typedef typename fptrhelper_type<FPTR>::size_type fptrint_type;
            fptrint_type                    fptr_as_int;
            std::vector<FPTR>               rv;
            symbolname_type::const_iterator cursym;

            EZASSERT2_NZERO(impl->handle, jit_error,
                            EZINFO("attempt to lookup function in un-initialized jit_handle"));

            // Ok, for all symbols in the list, get them + convert them
            for(cursym=symbolnames.begin(); cursym!=symbolnames.end(); cursym++) {
                // check we don't get back a null pointer
                EZASSERT_NZERO( fptr_as_int = reinterpret_cast<fptrint_type>(::dlsym(impl->handle, cursym->c_str())),
                        jit_error );
                // Ok!
                rv.push_back( reinterpret_cast<FPTR>(fptr_as_int) );
            }
            return rv;
        }

    private:
        struct jit_handle_impl {
            jit_handle_impl();
            jit_handle_impl(void* h, const std::string& f);

            void*       handle;
            std::string dllname;

            ~jit_handle_impl();
        };
        countedpointer<jit_handle_impl>  impl;
};


#endif
