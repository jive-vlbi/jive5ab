// create a thunk (http://en.wikipedia.org/wiki/Thunk section on FunctionalProgramming)
// Copyright (C) 2007-2009 Harro Verkouter
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
//
// The thunk is not yet a nullary function but always an arity 1 - the actual
// argument to which the function is to be applied still needs to 
// be supplied.
//
// The returned cached functioncall (thunk) is NOT templated
// so you can store it easily in a vector or array.
// The "creators" of the thunks ARE templated. It is left
// up to the user to ensure that the actual pointer to the thing that
// the thunk is executed upon is of the correct type.
//
// What seems to work nicely so far is storing the RTTI info (string)
// together with the thunk. When the user wants to execute
// the thunk the code can verify that the user-passed thing
// matches with what the thunk expects as input
#ifndef HAAVEE_THUNK_H
#define HAAVEE_THUNK_H

#include <exception>
#include <typeinfo>
#include <sstream>

// Make it compile with GCC >=4.3 and <4.3
//
#ifdef __clang__          /* __clang__ begin */

#define TVERS (10000 * __clang_major__ + 100 * __clang_minor__)
#define TVERSMIN 49999

#elif defined(__GNUC__)   /* __clang__ end,  __GNUC__ begin */

#define TVERS (10000 * __GNUC__ + 100 * __GNUC_MINOR__)
#define TVERSMIN 40299

#else   /* neither __clang__ nor __GNUC__ */

#define TVERS    1
#define TVERSMIN 0

#endif  /* End of compiler version stuff */

#if TVERS > TVERSMIN
    #define TSTATICTEMPLATE
#else
    #define TSTATICTEMPLATE static
#endif

// Very important: this type will represent a void return type.
// This is important to be able to "store" the result of
// an expression return nothing (void). 
// Functions can now use overloading; an argument of FPTR_NullType
// indicates a void expression - the functions can select what to
// do: store a result or not.
struct FPTR_NullType {};

struct function_returns_void_you_stupid {};
struct call_of_erased_function {};
struct curried_fn_called_with_wrong_argumenttype {};
struct extracting_returnvalue_from_erased_function {};
struct extracting_returnvalue_of_incorrect_type {};


#define TYPE(a) (typeid(a).name())

// This construction allows us to keep everything inside a single .h file
// AND compile with "-W -Wall -Werror" and no "defined-but-unused"
// warnings-cum-errors (compiletime) or doubly-defined symbols (linktime).
template <typename T>
static void throw_a_t(const std::string& file, int line, const std::string& msg="") {
    struct exc : public std::exception {
        public:
            exc(const std::string& m) throw(): s(m) {}
            virtual const char* what(void) const throw() { return s.c_str(); }
            virtual ~exc() throw() {};
            std::string s;
    };
    std::ostringstream strm;
    strm << file << ":" << line << " [" << TYPE(T) << "] " << msg;
    throw exc(strm.str());
}
#define THROW_A_T(type)      throw_a_t<type>(__FILE__, __LINE__);
#define THROW_A_T2(type, msg) throw_a_t<type>(__FILE__, __LINE__, msg);


// Functiontemplates for calling AND saving returnvalue - if applicable
// See FPTR_NullType above


// Nullary function, returning nothing, ie "void (*)(void)"
// Technically, the template below could be writtena as:
//
//  static void reallycall(FPTR_NullType*, void (*fptr)()) {
//      fptr();
//  }
//
// [ie a non-templated global function]
//
// However, since it is NOT a template (nor specialization), object code
// will be present (ie the symbol has a definition) in this compilation unit
// wether or not you actually *need* this function.
//
// With all warnings turned on the compiler then warns that the symbol
// is defined but not used, in case you do not actually *need* this symbol.
// With -Werror this means compilation fails, actually, for a non-issue!
//
// By rewriting it as a template, it will only be instantiated only when
// actually used, removing the error-even-if-you-don't-actually-need-it.
//
// "Ret" then will be "void" [the only matching case - otherwise either
// the previous template will match, or compilation will fail because 
// no other template actually matches], but since it's not ever used
// in the code it's Ok!
template <typename Ret>
static void reallycall(FPTR_NullType*, Ret (*fptr)(void) ) {
    fptr();
}

// Nullary function returning something. 
template <typename Ret>
static void reallycall(Ret* rvptr, Ret (*fptr)(void)) {
    *rvptr = fptr();
}

// Call a functor's operator().
template <typename Functor>
static void reallycall(FPTR_NullType*, Functor f) {
    f();
}
template <typename Functor, typename Ret>
static void reallycall(Ret* rvptr, Functor f) {
    *rvptr = f();
}

// Global functions of Arity 1
template <typename Arg>
static void reallycall(FPTR_NullType*, void (*fptr)(Arg), Arg arg ) {
    fptr(arg);
}
template <typename Ret, typename Arg>
static void reallycall(Ret* rvptr, Ret (*fptr)(Arg), Arg arg) {
    *rvptr = fptr(arg);
}
// Global functions of Arity 2
template <typename Arg1, typename Arg2>
static void reallycall(FPTR_NullType*, void (*fptr)(Arg1, Arg2), Arg1 arg1, Arg2 arg2) {
    fptr(arg1, arg2);
}
template <typename Ret, typename Arg1, typename Arg2>
static void reallycall(Ret* rvptr, Ret (*fptr)(Arg1, Arg2), Arg1 arg1, Arg2 arg2) {
    *rvptr = fptr(arg1, arg2);
}
// Global functions of Arity 3
template <typename Arg1, typename Arg2, typename Arg3>
static void reallycall(FPTR_NullType*, void (*fptr)(Arg1, Arg2,Arg3), Arg1 arg1, Arg2 arg2, Arg3 arg3) {
    fptr(arg1, arg2, arg3);
}
template <typename Ret, typename Arg1, typename Arg2, typename Arg3>
static void reallycall(Ret* rvptr, Ret (*fptr)(Arg1, Arg2,Arg3), Arg1 arg1, Arg2 arg2, Arg3 arg3) {
    *rvptr = fptr(arg1, arg2, arg3);
}

// Nullary memberfunctions
template <typename Class>
static void reallycall(FPTR_NullType*, void (Class::*memfun)(void), Class* ptr) {
    (ptr->*memfun)();
}
template <typename Ret, typename Class>
static void reallycall(Ret* rvptr, Ret (Class::*memfun)(void), Class* ptr) {
    *rvptr = (ptr->*memfun)();
}
// Memberfunctions of arity 1
template <typename Class, typename Arg>
static void reallycall(FPTR_NullType*, void (Class::*memfun)(Arg), Class* ptr, Arg arg) {
    (ptr->*memfun)(arg);
}
template <typename Ret, typename Class, typename Arg>
static void reallycall(Ret* rvptr, Ret (Class::*memfun)(Arg), Class* ptr, Arg arg) {
    *rvptr = (ptr->*memfun)(arg);
}

template <typename Class, typename Arg>
static void reallycall(FPTR_NullType*, void (Class::*memfun)(Arg) const, Class const* ptr, Arg arg) {
    (ptr->*memfun)(arg);
}
template <typename Ret, typename Class, typename Arg>
static void reallycall(Ret* rvptr, Ret (Class::*memfun)(Arg) const, Class const* ptr, Arg arg) {
    *rvptr = (ptr->*memfun)(arg);
}
// Memberfunctions of arity 2
template <typename Class, typename Arg1, typename Arg2>
static void reallycall(FPTR_NullType*, void (Class::*memfun)(Arg1, Arg2), Class* ptr, Arg1 arg1, Arg2 arg2) {
    (ptr->*memfun)(arg1, arg2);
}
template <typename Ret, typename Class, typename Arg1, typename Arg2>
static void reallycall(Ret* rvptr, Ret (Class::*memfun)(Arg1, Arg2), Class* ptr, Arg1 arg1, Arg2 arg2) {
    *rvptr = (ptr->*memfun)(arg1, arg2);
}


template <typename Ret>
static void copyfn(void* rvptr, const std::string& actualtype, const Ret& r) {
    static std::string expectedtype(typeid(Ret).name());
    if( expectedtype==actualtype )
        *((Ret*)rvptr) = r;
    else
        THROW_A_T2(extracting_returnvalue_of_incorrect_type,
                   expectedtype+" expected, got "+actualtype);
}
// This means someone is (attempting to) extract a returnvalue
// from something returning void
template <>
TSTATICTEMPLATE void copyfn(void*, const std::string&, const FPTR_NullType&) {
    //THROW_A_T(function_returns_void_you_stupid);
}




// Helper struct to save a functioncall, arguments and its returnvalue.
// Ya. It's Clunky. Works good enough tho!
template <typename FPTR, typename Ret, 
          typename ARG1=FPTR_NullType, typename ARG2=FPTR_NullType,
          typename ARG3=FPTR_NullType>
struct tuple {
    typedef tuple<FPTR,Ret,ARG1,ARG2,ARG3> Self;

    tuple(FPTR f): Function(f) {}
    tuple(FPTR f, ARG1 a1): Function(f), Arg1(a1) {} 
    tuple(FPTR f, ARG1 a1, ARG2 a2): Function(f), Arg1(a1), Arg2(a2) {} 
    tuple(FPTR f, ARG1 a1, ARG2 a2, ARG3 a3): Function(f), Arg1(a1), Arg2(a2), Arg3(a3) {} 


    FPTR Function;
    Ret  Return;
    ARG1 Arg1;
    ARG2 Arg2;
    ARG3 Arg3;

    static void erase(void* ctxt) {
        delete (Self*)ctxt;
    }

    static void retval(void* ctxt, void* rvptr, const std::string& expect) {
        copyfn(rvptr, expect, ((Self*)ctxt)->Return);
    }
};




// API for deferred functioncall with a context which is
//     set up at the moment of creating the thunk/curry.
// I know it doesn't look like much of an API but hey!
// Some people think void* is the biggest thing since sliced bread
// (which it probably is) - you can pass *anything* through it :)
//
// This looks awfully type-unsafe but as long as the same
// object that creates the functionpointers lets those pointers
// point back at itself, it can be reasonably sure what the 
// context *actually* points at [typically, it is what you yourself
// decided to put in there ...].
// See below at the implementations for what I mean.
typedef void (*callfn_type)(void* context, void* usrdata);
typedef void (*erasefn_type)(void* context);
typedef void (*returnvalfn_type)(void* context, void* rvptr, const std::string& expect);
typedef void (*thunkfn_type)(void* context);

// Groups together a callfunction, an erasefunction, a returnvalue-extractor
// and a callcontext for deferred execution.
// Note: this is just a curried thing: the thunky still requires 1 argument
// to be passed in
//
// This struct basically curries the API functions of arity N to
// arity N-1 [it saves the context, leaving the user to supply
// N-1 parameters. Currying is a concept borrowed from Functional
// Programming languages. See http://en.wikipedia.org/wiki/Currying ...]
//
// When the time for making the call is there, call the "call(userdata)"
// method on this one. It will delegate to the actual function to be called
// together with the context (typically: arguments to the function to
// be called, will be decoded in the actual dispatcher) stored in this
// specific instance.
//
// When this functioncall can go, do call the erase() method, it allows
// the maker to typesafely delete the allocated context
//
// If saveable, the result of the functioncall is stored. It will be
// overwritten when this specific deferred-execution environment is
// called again (which is deviation of the Functional Programming paradigm!).
// The returnvalue can be extracted using the returnval()
// function. The caller is responsible to make sure that "rvptr" actually
// points to the correct type. The layer above this abstraction should 
// enforce that.
//
// Function-call-builders should return an item of this type and fill in
// the pointers. The context they yield will be passed back to the function
// pointers they returned.
//
// Curried functions are called with a pointer to the supplied
// argument!!!! Actual function-call dispatchers must take that
// into account!

static void nocall_fn(void*, void*) {}
static void noerase_fn(void*) {}
static void noreturn_fn(void*, void*, const std::string&) {}
static void nothunk_fn(void*) {}

template <typename T> struct curryplusarg;

struct curry_type {
    template <typename T>
    friend struct curryplusarg;
    // The default curry_type is a no-op, even
    // when extracting a returnvalue.
    curry_type() :
        context((void*)1),
        callfn(&nocall_fn), erasefn(&noerase_fn), returnvalfn(&noreturn_fn)
    {}
    curry_type(callfn_type c_fn, erasefn_type e_fn, 
                 returnvalfn_type r_fn, void* ctxt,
                 const char* atype, const char* rtype):
        context(ctxt), argtype(atype), returntype(rtype),
        callfn(c_fn), erasefn(e_fn), returnvalfn(r_fn)
    {}

    // Take any argument, do rudimentary typecheck,
    // and supply the dispatcher with a pointer to
    // the argument.
    template <typename T>
    void operator()(T t) {
        static const std::string ttype( TYPE(T) );
        if(!context)
            THROW_A_T(call_of_erased_function);
        if(!argtype.empty() && ttype!=argtype)
            THROW_A_T2(curried_fn_called_with_wrong_argumenttype,
                       argtype+" expected, got "+ttype);
        callfn(context, (void*)&t);
        return;
    }
    // if you pass in "void*" literally, skip the typechecking.
    // You're on your own there ... However if you know
    // the thing expects a pointer-type it's ok.
    void operator()(void* argptr) {
        if(!context)
            THROW_A_T(call_of_erased_function);
        callfn(context, &argptr);
        return;
    }


    void erase(void) {
        erasefn(context);
        // make sure that after an erase any other access will
        // make the program EXPLODES0RZ :)
        context = 0;
    }

    template <typename T>
    void returnval(T& t) const {
        if(!context)
            THROW_A_T(extracting_returnvalue_from_erased_function);
        returnvalfn(context, &t, TYPE(T));
        return;
    }

    // Return the "typeid(ReturnValue).name()" - can be usefull
    // for one layer up: if you accept a curried function, you
    // can at least test wether it yields what you expect it to yield
    const std::string returnvaltype(void) {
        return returntype;
    }
    const std::string& argumenttype(void) {
        return argtype;
    }
    private:
        void*            context;
        std::string      argtype;
        std::string      returntype;
        callfn_type      callfn;
        erasefn_type     erasefn;
        returnvalfn_type returnvalfn;
};

// This is a true thunk - no extra info required! All info is there
// for the taking!
struct thunk_type {
    thunk_type() :
        context((void*)1),
        thunkfn(&nothunk_fn), erasefn(&noerase_fn), returnvalfn(&noreturn_fn)
    {}

    thunk_type(thunkfn_type c_fn, erasefn_type e_fn, 
               returnvalfn_type r_fn, void* ctxt, const char* rtyp):
        context(ctxt), returntype(rtyp),
        thunkfn(c_fn), erasefn(e_fn), returnvalfn(r_fn)
    {}

    // Make this'un look like a functor with signature "void f(void)"
    void operator()( void ) {
        if(!context)
            THROW_A_T(call_of_erased_function);
        thunkfn(context);
    }

    void erase(void) {
        erasefn(context);
        // make sure that after an erase any other access will
        // make the program EXPLODES0RZ :)
        context = 0;
    }

    template <typename T>
    void returnval(T& t) const {
        if(!context)
            THROW_A_T(extracting_returnvalue_from_erased_function);
        returnvalfn(context, &t, TYPE(T));
        //returnvalfn(context, &t, typeid(T).name());
        return;
    }

    // Return the "typeid(ReturnValue).name()" - can be usefull
    // for one layer up: if you accept a curried function, you
    // can at least test wether it yields what you expect it to yield
    const std::string returnvaltype(void) {
        return returntype;
    }

    private:
        void*            context;
        std::string      returntype;
        thunkfn_type     thunkfn;
        erasefn_type     erasefn;
        returnvalfn_type returnvalfn;
};

//
//     Here begin the templated thunk/curry building functions
//

// Transform a void type into a usable type - at least one that we 
// can get an address of, if we must. It is left up to the functioncalls
// to interpret an argument of FPTR_NullType as meaning
//    "OH NOES! THERE IS NO VALUE!"
// and either ignore it
//   (when storing a returnvalue: you don't have to store a "void")
// or throw up
//   (when attempting to extract a void return value: you can't extract "void")
template <typename T>
struct Storeable {
    typedef T Type;
};
template <>
struct Storeable<void> {
    typedef FPTR_NullType Type;
};


// Capture nullary functions. They are almost "thunks" by default ;)
// This one thunks functions of signature
//      ReturnType  function(void)
// (including ReturnType==void)
template <typename ReturnType>
struct nullary {
    typedef nullary<ReturnType> Self;
    typedef ReturnType (*fptr_type)(void);
    typedef typename Storeable<ReturnType>::Type  StoreableReturnType;
    typedef tuple<fptr_type,StoreableReturnType> thunk_tuple;

    static thunk_type makefn(fptr_type fptr) {
        return thunk_type(&Self::call,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr),
                          TYPE(StoreableReturnType));
    }
    static void call(void* ctxt) {
        thunk_tuple* context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function);
    }
};

// This one captures functions of the following signature:
//     ReturnType function(Arg)
// (including ReturnType==void)
template <typename ReturnType, typename Arg>
struct callfn {
    typedef callfn<ReturnType, Arg> Self;
    typedef ReturnType (*fptr_type)(Arg);
    typedef typename Storeable<ReturnType>::Type  StoreableReturnType;


    typedef tuple<fptr_type, StoreableReturnType>      curry_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Arg> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static curry_type makefn(fptr_type fptr) {
        return curry_type(&Self::call,
                          &curry_tuple::erase, &curry_tuple::retval,
                          new curry_tuple(fptr),
                          TYPE(Arg), TYPE(StoreableReturnType) );
    }
    static thunk_type makefn(fptr_type fptr, Arg arg) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, arg),
                          TYPE(StoreableReturnType));
    }

    // curried functions are called with a pointer to Arg!
    static void call(void* ctxt, void* arg) {
        curry_tuple* context = (curry_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Arg*)arg));
    }
    static void tcall(void* ctxt) {
        thunk_tuple* context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1);
    }
};


// This one captures functions of the following signature:
//     ReturnType function(Arg1, Arg2).
// Note: the argument Arg2 is bound. Each time this function
// is called, the same value is passed to the method!
template <typename ReturnType, typename Arg1, typename Arg2>
struct callfn1 {
    // The type of function we store
    typedef callfn1<ReturnType, Arg1, Arg2> Self;
    typedef ReturnType (*fptr_type)(Arg1, Arg2);
    typedef typename Storeable<ReturnType>::Type  StoreableReturnType;

    // For the curried function the "field Arg1" in the tuple is
    // actually of "type Arg2".
    typedef tuple<fptr_type, StoreableReturnType, Arg1>       curry1_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Arg2>       curry2_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Arg1, Arg2> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static curry_type makefn(fptr_type fptr, Arg1 arg1) {
        return curry_type(&Self::call1,
                          &curry1_tuple::erase, &curry1_tuple::retval,
                          new curry1_tuple(fptr, arg1),
                          TYPE(Arg2), TYPE(StoreableReturnType) );
    }
    static curry_type makefn(fptr_type fptr, Arg2 arg2) {
        return curry_type(&Self::call2,
                          &curry2_tuple::erase, &curry2_tuple::retval,
                          new curry2_tuple(fptr, arg2),
                          TYPE(Arg1), TYPE(StoreableReturnType) );
    }
    static thunk_type makefn(fptr_type fptr, Arg1 arg1, Arg2 arg2) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, arg1, arg2),
                          TYPE(StoreableReturnType) );
    }

    static void call1(void* ctxt, void* arg) {
        curry1_tuple*  context = (curry1_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, *((Arg2*)arg));
    }
    static void call2(void* ctxt, void* arg) {
        curry2_tuple*  context = (curry2_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Arg1*)arg), context->Arg1);
    }
    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2);
    }
};
// Specialization for fn's taking the same argument twice
template <typename Ret, typename Arg>
struct callfn1<Ret,Arg,Arg> {
    typedef callfn1<Ret, Arg, Arg> Self;
    typedef Ret (*fptr_type)(Arg, Arg);
    typedef typename Storeable<Ret>::Type StoreableReturnType;

    typedef tuple<fptr_type, StoreableReturnType, Arg>      curry_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Arg, Arg> thunk_tuple;

    static curry_type makefn(fptr_type fptr, Arg arg) {
        return curry_type(&Self::call,
                          &curry_tuple::erase, &curry_tuple::retval,
                          new curry_tuple(fptr, arg),
                          TYPE(Arg), TYPE(StoreableReturnType));
    }
    static thunk_type makefn(fptr_type fptr, Arg arg1, Arg arg2) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, arg1, arg2),
                          TYPE(StoreableReturnType) );
    }

    static void call(void* ctxt, void* arg) {
        curry_tuple*  context = (curry_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, *((Arg*)arg));
    }
    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2);
    }
};
// This one captures functions of the following signature:
//     ReturnType function(Arg1, Arg2,Arg3).
template <typename ReturnType, typename Arg1, typename Arg2, typename Arg3>
struct callfn2 {
    // The type of function we store
    typedef callfn2<ReturnType, Arg1, Arg2,Arg3> Self;
    typedef ReturnType (*fptr_type)(Arg1, Arg2,Arg3);
    typedef typename Storeable<ReturnType>::Type  StoreableReturnType;

    typedef tuple<fptr_type, StoreableReturnType, Arg1, Arg2, Arg3> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static thunk_type makefn(fptr_type fptr, Arg1 arg1, Arg2 arg2, Arg3 arg3) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, arg1, arg2, arg3),
                          TYPE(StoreableReturnType) );
    }

    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1,
                   context->Arg2, context->Arg3);
    }
};

// Repeat the above code, only now for pointer-to-memberfunctions!

// This one captures nullary memberfunctions
//     ReturnType Class::memberfunction(void).
template <typename ReturnType, typename Class>
struct memfn {
    typedef memfn<ReturnType, Class> Self;
    typedef ReturnType (Class::*fptr_type)(void);
    typedef typename Storeable<ReturnType>::Type StoreableReturnType;

    typedef tuple<fptr_type, StoreableReturnType>        curry_tuple;
    typedef tuple<fptr_type, StoreableReturnType,Class*> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static curry_type makefn(fptr_type fptr) {
        return curry_type(&Self::call,
                          &curry_tuple::erase, &curry_tuple::retval,
                          new curry_tuple(fptr),
                          TYPE(Class*), TYPE(StoreableReturnType) );
    }
    static thunk_type makefn(fptr_type fptr, Class* object) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, object),
                          TYPE(StoreableReturnType) );
    }

    static void call(void* ctxt, void* arg) {
        curry_tuple*  context = (curry_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Class**)arg));
    }
    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1);
    }
};
// Memberfunctions with one argument:
//      ReturnType  Class::memberfunction(Arg)
template <typename ReturnType, typename Class, typename Arg>
struct memfn1 {
    typedef memfn1<ReturnType, Class, Arg> Self;
    typedef ReturnType (Class::*fptr_type)(Arg);
    typedef typename Storeable<ReturnType>::Type StoreableReturnType;

    typedef tuple<fptr_type, StoreableReturnType, Arg>         curry_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Class*>      sometuple;
    typedef tuple<fptr_type, StoreableReturnType, Class*, Arg> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static curry_type makefn(fptr_type fptr, Arg arg) {
        return curry_type(&Self::call,
                          &curry_tuple::erase, &curry_tuple::retval,
                          new curry_tuple(fptr, arg),
                          TYPE(Class*), TYPE(StoreableReturnType) );
    }
    static curry_type makefn(fptr_type fptr, Class* object) {
        return curry_type(&Self::bcall,
                          &sometuple::erase, &sometuple::retval,
                          new sometuple(fptr, object),
                          TYPE(Arg), TYPE(StoreableReturnType) );
    }
    static thunk_type makefn(fptr_type fptr, Class* object, Arg arg) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, object, arg),
                          TYPE(StoreableReturnType) );
    }

    static void call(void* ctxt, void* arg) {
        curry_tuple*  context = (curry_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Class**)arg), context->Arg1);
    }
    static void bcall(void* ctxt, void* arg) {
        sometuple*  context = (sometuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, *((Arg*)arg));
    }
    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2);
    }
};

template <typename ReturnType, typename Class, typename Arg>
struct memfn1const {
    typedef memfn1const<ReturnType, Class, Arg> Self;
    typedef ReturnType (Class::*fptr_type)(Arg) const;
    typedef typename Storeable<ReturnType>::Type StoreableReturnType;

    typedef tuple<fptr_type, StoreableReturnType, Arg>               curry_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Class const*>      sometuple;
    typedef tuple<fptr_type, StoreableReturnType, Class const*, Arg> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static curry_type makefn(fptr_type fptr, Arg arg) {
        return curry_type(&Self::call,
                          &curry_tuple::erase, &curry_tuple::retval,
                          new curry_tuple(fptr, arg),
                          TYPE(Class*), TYPE(StoreableReturnType) );
    }
    static curry_type makefn(fptr_type fptr, Class const* object) {
        return curry_type(&Self::bcall,
                          &sometuple::erase, &sometuple::retval,
                          new sometuple(fptr, object),
                          TYPE(Arg), TYPE(StoreableReturnType) );
    }
    static thunk_type makefn(fptr_type fptr, Class const* object, Arg arg) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, object, arg),
                          TYPE(StoreableReturnType) );
    }

    static void call(void* ctxt, void* arg) {
        curry_tuple*  context = (curry_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Class const **)arg), context->Arg1);
    }
    static void bcall(void* ctxt, void* arg) {
        sometuple*  context = (sometuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, *((Arg*)arg));
    }
    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2);
    }
};
// Memberfunctions with two arguments:
//      ReturnType  Class::memberfunction(Arg1, Arg2)
template <typename ReturnType, typename Class, typename Arg1, typename Arg2>
struct memfn2 {
    typedef memfn2<ReturnType, Class, Arg1, Arg2> Self;
    typedef ReturnType (Class::*fptr_type)(Arg1,Arg2);
    typedef typename Storeable<ReturnType>::Type StoreableReturnType;

    typedef tuple<fptr_type, StoreableReturnType, Arg1, Arg2>         obj_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Class*, Arg1>       arg2_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Class*, Arg2>       arg1_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Class*, Arg1, Arg2> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static curry_type makefn(fptr_type fptr, Arg1 arg1, Arg2 arg2) {
        return curry_type(&Self::call,
                          &obj_tuple::erase, &obj_tuple::retval,
                          new obj_tuple(fptr, arg1, arg2),
                          TYPE(Class*), TYPE(StoreableReturnType) );
    }
    static curry_type makefn(fptr_type fptr, Class* object, Arg1 arg1) {
        return curry_type(&Self::a2call,
                          &arg2_tuple::erase, &arg2_tuple::retval,
                          new arg2_tuple(fptr, object, arg1),
                          TYPE(Arg2), TYPE(StoreableReturnType) );
    }
    static curry_type makefn(fptr_type fptr, Class* object, Arg2 arg2) {
        return curry_type(&Self::a2call,
                          &arg2_tuple::erase, &arg2_tuple::retval,
                          new arg2_tuple(fptr, object, arg2),
                          TYPE(Arg2), TYPE(StoreableReturnType) );
    }
    static thunk_type makefn(fptr_type fptr, Class* object, Arg1 arg1, Arg2 arg2) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, object, arg1, arg2),
                          TYPE(StoreableReturnType) );
    }

    static void call(void* ctxt, void* arg) {
        obj_tuple*  context = (obj_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Class**)arg), context->Arg1, context->Arg2);
    }
    // in the following two contexts the Arg1 element of the context is
    // in reality the object pointer that was passed in in the c'tor
    static void a2call(void* ctxt, void* arg) {
        arg2_tuple*  context = (arg2_tuple*)ctxt;
        // in this context the Arg2 item is the original "Arg1" of the
        // template, we needed the missing "Arg2" (from the template
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2, *((Arg2*)arg));
    }
    static void a1call(void* ctxt, void* arg) {
        arg1_tuple*  context = (arg1_tuple*)ctxt;
        // in this context the Arg2 item is the original "Arg2" of the
        // template, we needed the missing "Arg1" (from the template
        reallycall(&context->Return, context->Function, context->Arg1, *((Arg1*)arg), context->Arg2);
    }
    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2, context->Arg3);
    }
};

// specialization for Arg1==Arg2
template <typename ReturnType, typename Class, typename Arg>
struct memfn2<ReturnType, Class, Arg, Arg> {
    typedef memfn2<ReturnType, Class, Arg, Arg> Self;
    typedef ReturnType (Class::*fptr_type)(Arg,Arg);
    typedef typename Storeable<ReturnType>::Type StoreableReturnType;

    typedef tuple<fptr_type, StoreableReturnType, Arg, Arg>         obj_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Class*, Arg>      arg_tuple;
    typedef tuple<fptr_type, StoreableReturnType, Class*, Arg, Arg> thunk_tuple;

    // Create a new deferred-execution-thunk-humping-bugaboo-geval! h00t!
    static curry_type makefn(fptr_type fptr, Arg arg1, Arg arg2) {
        return curry_type(&Self::call,
                          &obj_tuple::erase, &obj_tuple::retval,
                          new obj_tuple(fptr, arg1, arg2),
                          TYPE(Class*), TYPE(StoreableReturnType) );
    }
    static curry_type makefn(fptr_type fptr, Class* object, Arg arg1) {
        return curry_type(&Self::acall,
                          &arg_tuple::erase, &arg_tuple::retval,
                          new arg_tuple(fptr, object, arg1),
                          TYPE(Arg), TYPE(StoreableReturnType) );
    }
    static thunk_type makefn(fptr_type fptr, Class* object, Arg arg1, Arg arg2) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(fptr, object, arg1, arg2),
                          TYPE(StoreableReturnType) );
    }

    static void call(void* ctxt, void* arg) {
        obj_tuple*  context = (obj_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Class**)arg), context->Arg1, context->Arg2);
    }
    // in the following context the Arg1 element of the context is
    // in reality the object pointer that was passed in in the c'tor
    static void acall(void* ctxt, void* arg) {
        arg_tuple*  context = (arg_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2, *((Arg*)arg));
    }
    static void tcall(void* ctxt) {
        thunk_tuple*  context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function, context->Arg1, context->Arg2, context->Arg3);
    }

};


// Support calling "operator()" on a functor object
// Require the Functor object to tell us of its Returntype
template <typename Functor, typename Ret>
struct functor0 {
    typedef functor0<Functor,Ret>                Self;
    typedef typename Storeable<Ret>::Type       StoreableReturnType;
    typedef tuple<Functor, StoreableReturnType> thunk_tuple;

    static thunk_type makefn(Functor f) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(f),
                          TYPE(StoreableReturnType));
    }
    static void tcall(void* ctxt) {
        thunk_tuple* context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function);
    }
};

template <typename Functor, typename Ret, typename Arg>
struct functor {
    typedef functor<Functor,Ret,Arg>                 Self;
    typedef typename Storeable<Ret>::Type            StoreableReturnType;
    typedef tuple<Functor, StoreableReturnType>      curry_tuple;
    typedef tuple<Functor, StoreableReturnType, Arg> thunk_tuple;

    static curry_type makefn(Functor f) {
        return curry_type(&Self::call,
                          &curry_tuple::erase, &curry_tuple::retval,
                          new curry_tuple(f),
                          TYPE(StoreableReturnType));
    }

    static thunk_type makefn(Functor f, Arg arg) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(f, arg),
                          TYPE(StoreableReturnType));
    }
    static void call(void* ctxt, void* arg) {
        curry_tuple* context = (curry_tuple*)ctxt;
        reallycall(&context->Return, context->Function, *((Arg*)arg));
    }
    static void tcall(void* ctxt) {
        thunk_tuple* context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function);
    }
};
// specialization for void
template <typename Functor, typename Ret>
struct functor<Functor, Ret, void> {
    typedef functor<Functor,Ret, void>          Self;
    typedef typename Storeable<Ret>::Type       StoreableReturnType;
    typedef tuple<Functor, StoreableReturnType> thunk_tuple;

    static thunk_type makefn(Functor f) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &thunk_tuple::retval,
                          new thunk_tuple(f),
                          TYPE(StoreableReturnType));
    }
    static void tcall(void* ctxt) {
        thunk_tuple* context = (thunk_tuple*)ctxt;
        reallycall(&context->Return, context->Function);
    }
};

// a curry-thing + an argument = a thunktype ...
template <typename Arg>
struct curryplusarg {
    typedef curryplusarg<Arg>                           Self;
    typedef typename Storeable<void>::Type              StoreableReturnType;
    typedef tuple<curry_type, StoreableReturnType, Arg> thunk_tuple;

    static thunk_type makefn(curry_type ct, Arg arg) {
        return thunk_type(&Self::tcall,
                          &thunk_tuple::erase, &Self::retval,
                          new thunk_tuple(ct, arg),
                          ct.returnvaltype().c_str());
    }
    // thunk_tuple->Function == a curry_type.
    // The curry-thing takes care of storing the returnvalue.
    static void tcall(void* ctxt) {
        thunk_tuple* context = (thunk_tuple*)ctxt;
        context->Function(context->Arg1);
    }
    // Delegate extracting the returnvalue to the curry-type
    static void retval(void* ctxt, void* rvptr, const std::string& s) {
        thunk_tuple* context = (thunk_tuple*)ctxt;
        (context->Function.returnvalfn)(context->Function.context, rvptr, s);
    }
};

// Nice functiontemplates. 
// Makes Ur c0de more readable:
//      curry_type  cf = makethunk( .... );
//      thunk_type  tf = makethunk( ... ); 
// Feed it with a functionpointertype as first argument
// and optional arguments. If it compiles, you're doing it right :)
// (if _anything_ doesn't match up, the compiler will bark at you)
//
// You know you love c++ ... you know you do! :)
template <typename Ret>
thunk_type makethunk( Ret (*fptr)(void) ) {
    return nullary<Ret>::makefn(fptr);
}

// Function taking 1 argument in both "curried" and thunked flavours.
// "Curried" as in going from typed argument (type Arg) to void*.
template <typename Ret, typename Arg>
curry_type makethunk(Ret (*fptr)(Arg)) {
    return callfn<Ret, Arg>::makefn(fptr);
}
template <typename Ret, typename Arg>
thunk_type makethunk(Ret (*fptr)(Arg), Arg arg) {
    return callfn<Ret, Arg>::makefn(fptr, arg);
}

// Functions taking two arguments are curried to a version taking
// only one argument (or thunked to a nullary fn)
//template <typename Ret, typename Arg>
//curry_type makethunk(Ret (*fptr)(Arg, Arg), Arg arg) {
//    return callfn1<Ret, Arg, Arg>::makefn(fptr, arg);
//}
template <typename Ret, typename Arg>
curry_type makethunk(Ret (*fptr)(Arg, Arg), Arg arg) {
    return callfn1<Ret, Arg, Arg>::makefn(fptr, arg);
}
template <typename Ret, typename Arg>
thunk_type makethunk(Ret (*fptr)(Arg, Arg), Arg arg1, Arg arg2) {
    return callfn1<Ret, Arg, Arg>::makefn(fptr, arg1, arg2);
}
template <typename Ret, typename Arg1, typename Arg2>
curry_type makethunk(Ret (*fptr)(Arg1, Arg2), Arg1 arg1) {
    return callfn1<Ret, Arg1, Arg2>::makefn(fptr, arg1);
}
template <typename Ret, typename Arg1, typename Arg2>
curry_type makethunk(Ret (*fptr)(Arg1, Arg2), Arg2 arg2) {
    return callfn1<Ret, Arg1, Arg2>::makefn(fptr, arg2);
}
template <typename Ret, typename Arg1, typename Arg2>
thunk_type makethunk(Ret (*fptr)(Arg1, Arg2), Arg1 arg1, Arg2 arg2) {
    return callfn1<Ret, Arg1, Arg2>::makefn(fptr, arg1, arg2);
}

// Functionpointer to function taking three arguments
// ATM they must be all different
template <typename Ret, typename Arg1, typename Arg2, typename Arg3>
thunk_type makethunk(Ret (*fptr)(Arg1, Arg2, Arg3), Arg1 arg1, Arg2 arg2, Arg3 arg3) {
    return callfn2<Ret, Arg1, Arg2, Arg3>::makefn(fptr, arg1, arg2, arg3);
}

// Pointers to nullary memberfunctions 
//   "Curried" as in: just go from a typed this-pointer to void*
template <typename Ret, typename Class>
curry_type makethunk(Ret (Class::*fptr)() ) {
    return memfn<Ret,Class>::makefn(fptr);
}
template <typename Ret, typename Class>
thunk_type makethunk(Ret (Class::*fptr)(), Class* ptr) {
    return memfn<Ret,Class>::makefn(fptr, ptr);
}

// Memberfunctions taking an argument, both curried & thunked versions
template <typename Ret, typename Class, typename Arg1>
curry_type makethunk(Ret (Class::*fptr)(Arg1), Arg1 arg1) {
    return memfn1<Ret,Class,Arg1>::makefn(fptr, arg1);
}
template <typename Ret, typename Class, typename Arg1>
curry_type makethunk(Ret (Class::*fptr)(Arg1), Class* object) {
    return memfn1<Ret,Class,Arg1>::makefn(fptr, object);
}
template <typename Ret, typename Class, typename Arg1>
curry_type makethunk(Ret (Class::*fptr)(Arg1) const, Class const* object) {
    return memfn1const<Ret,Class,Arg1>::makefn(fptr, object);
}
template <typename Ret, typename Class, typename Arg1>
thunk_type makethunk(Ret (Class::*fptr)(Arg1), Class* ptr, Arg1 arg1) {
    return memfn1<Ret,Class,Arg1>::makefn(fptr, ptr, arg1);
}

// Memberfunctions taking two arguments
//   take care of memfuns that have 2x same type argument!
//   If you don't then the two flavours of template for
//   calling  Class::memfun(Arg1, Arg2) become ambiguous:
//
//      1. Class::memfun(Arg1,Arg2) (Class*, Arg1)
//      2. Class::memfun(Arg1,Arg2) (Class*, Arg2)
//
//   evaluate to the same. that's when the compiler gives up;
//   it can't decide which to use.
//   This is only important if one of the args is left out.
template <typename Ret, typename Class, typename Arg>
curry_type makethunk(Ret (Class::*fptr)(Arg, Arg), Class* object, Arg arg) {
    return memfn2<Ret,Class,Arg,Arg>::makefn(fptr, object, arg);
}
template <typename Ret, typename Class, typename Arg>
curry_type makethunk(Ret (Class::*fptr)(Arg, Arg), Arg arg1, Arg arg2) {
    return memfn2<Ret,Class,Arg,Arg>::makefn(fptr, arg1, arg2);
}

template <typename Ret, typename Class, typename Arg1, typename Arg2>
curry_type makethunk(Ret (Class::*fptr)(Arg1, Arg2), Arg1 arg1, Arg2 arg2) {
    return memfn2<Ret,Class,Arg1,Arg2>::makefn(fptr, arg1, arg2);
}
template <typename Ret, typename Class, typename Arg1, typename Arg2>
thunk_type makethunk(Ret (Class::*fptr)(Arg1, Arg2), Class* object, Arg1 arg1, Arg2 arg2) {
    return memfn2<Ret,Class,Arg1,Arg2>::makefn(fptr, object, arg1, arg2);
}


// A curry_type + an argument = thunk_type
//   knowledge of the returntype is lost.
template <typename Arg>
thunk_type makethunk(curry_type ct, Arg arg) {
    return curryplusarg<Arg>::makefn(ct, arg);
}

// Nullary and 1-argument functors
template <typename Functor>
thunk_type makethunk(Functor f) {
    return functor<Functor,typename Functor::Return, typename Functor::Argument>::makefn(f);
}

#endif
