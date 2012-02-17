// Provide atomic updates to integral values of size of 1, 2 or 4 bytes.
#ifndef JIVE5AB_ATOMIC_H
#define JIVE5AB_ATOMIC_H
// Copyright (C) 2007-2011 Harro Verkouter
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
// Atomically update counters. All methods keep on trying until succesfull,
// except the "bool atomic<T>.update()", which tries exactly once.
// The latter's main use is when attempting to "grab a lock" - when you,
// irrespective of the current value, want to try to go atomically from "0"
// -> "1" (e.g.). This will only succeed if the current value WAS 0.
//
// What to do when the atomic update fails is left up the the caller.
//
//
// The code has been split into a typesafe part ("atomic<T>") and a
// type-unsafe but appropriate for the addressed size piece of code.
//
// Accessing non-integral types or accessing datatypes of unsupported size
// will result, in principle, in compilation errors.
//
#include <stdint.h>

struct xchg_dummy { unsigned char a[100]; };
#define XCHG_DUMMY(x)  ((struct xchg_dummy*)(x))

// Intel 32bit/64bit architecture
#if defined(__i386__) || defined(__x86_64__)

#define CAS1 "lock; cmpxchgl %b1,%b2"
#define CAS2 "lock; cmpxchgl %w1,%w2"
#define CAS4 "lock; cmpxchgl %k1,%k2"

// Atomically add 'a' to '*ptr' return prev value in 'p'
#define ATOMIC_ADD(type, ptr, o, n, a, p, instr) \
        o = *ptr; \
        n = (type)(o+a); \
        __asm__ __volatile__( instr \
                                : "=a"(p) \
                                : "q"(n), "m"(*XCHG_DUMMY(ptr)), "0"(o) \
                                : "memory" ); 

// Id. but now subtraction
#define ATOMIC_SUB(type, ptr, o, n, a, p, instr) \
        o = *ptr; \
        n = (type)(o-a); \
        __asm__ __volatile__( instr \
                                : "=a"(p) \
                                : "q"(n), "m"(*XCHG_DUMMY(ptr)), "0"(o) \
                                : "memory" );

// atomically try to set *ptr to 'n', succeeds if *ptr==o. store previous
// value in 'p'
#define ATOMIC_SET(ptr, o, n, p, instr) \
        __asm__ __volatile__( instr \
                                : "=a"(p) \
                                : "q"(n), "m"(*XCHG_DUMMY(ptr)), "0"(o) \
                                : "memory" ); 


#define MKINCFUNC(type, instr) \
    static inline type atomic_inc(type volatile* ptr) { \
        volatile type oud, nieuw, vorig; \
        do { \
            ATOMIC_ADD(type, ptr, oud, nieuw, (type)1, vorig, instr); \
        } while( vorig!=oud ); \
        return nieuw; \
    }

#define MKDECFUNC(type, instr) \
    static inline type atomic_dec(type volatile* ptr) { \
        volatile type oud, nieuw, vorig; \
        do { \
            ATOMIC_SUB(type, ptr, oud, nieuw, (type)1, vorig, instr); \
        } while( vorig!=oud ); \
        return nieuw; \
    }



MKINCFUNC(int8_t, CAS1)
MKINCFUNC(uint8_t, CAS1)
MKDECFUNC(int8_t, CAS1)
MKDECFUNC(uint8_t, CAS1)

MKINCFUNC(int16_t, CAS2)
MKINCFUNC(uint16_t, CAS2)
MKDECFUNC(int16_t, CAS2)
MKDECFUNC(uint16_t, CAS2)

MKINCFUNC(int32_t, CAS4)
MKINCFUNC(uint32_t, CAS4)
MKDECFUNC(int32_t, CAS4)
MKDECFUNC(uint32_t, CAS4)


#define MKADDFUNC(type, instr) \
    static inline type atomic_add(type volatile* ptr, const type toadd) { \
        volatile type oud, nieuw, vorig; \
        do { \
            ATOMIC_ADD(type, ptr, oud, nieuw, toadd, vorig, instr); \
        } while( vorig!=oud );\
        return nieuw; \
    }

#define MKSUBFUNC(type, instr) \
    static inline type atomic_sub(type volatile* ptr, const type tosub) { \
        volatile type oud, nieuw, vorig; \
        do { \
            ATOMIC_SUB(type, ptr, oud, nieuw, tosub, vorig, instr); \
        } while( vorig!=oud );\
        return nieuw; \
    }


MKADDFUNC(int8_t, CAS1)
MKADDFUNC(uint8_t, CAS1)
MKSUBFUNC(int8_t, CAS1)
MKSUBFUNC(uint8_t, CAS1)

MKADDFUNC(int16_t, CAS2)
MKADDFUNC(uint16_t, CAS2)
MKSUBFUNC(int16_t, CAS2)
MKSUBFUNC(uint16_t, CAS2)

MKADDFUNC(int32_t, CAS4)
MKADDFUNC(uint32_t, CAS4)
MKSUBFUNC(int32_t, CAS4)
MKSUBFUNC(uint32_t, CAS4)

#define MKTRYADDFUNC(type, instr) \
    static inline type atomic_try_add(type volatile* ptr, const type toadd) { \
        volatile type oud, nieuw, vorig; \
        ATOMIC_ADD(type, ptr, oud, nieuw, toadd, vorig, instr); \
        return( vorig==oud );\
    }

#define MKTRYSUBFUNC(type, instr) \
    static inline type atomic_try_sub(type volatile* ptr, const type tosub) { \
        volatile type oud, nieuw, vorig; \
        ATOMIC_SUB(type, ptr, oud, nieuw, tosub, vorig, instr); \
        return( vorig==oud );\
    }


MKTRYADDFUNC(int8_t, CAS1)
MKTRYADDFUNC(uint8_t, CAS1)
MKTRYSUBFUNC(int8_t, CAS1)
MKTRYSUBFUNC(uint8_t, CAS1)

MKTRYADDFUNC(int16_t, CAS2)
MKTRYADDFUNC(uint16_t, CAS2)
MKTRYSUBFUNC(int16_t, CAS2)
MKTRYSUBFUNC(uint16_t, CAS2)

MKTRYADDFUNC(int32_t, CAS4)
MKTRYADDFUNC(uint32_t, CAS4)
MKTRYSUBFUNC(int32_t, CAS4)
MKTRYSUBFUNC(uint32_t, CAS4)


#define MKSETFUNC(type, instr) \
    static inline type atomic_set(type volatile* ptr, const type toset) { \
        volatile type oud, vorig; \
        do { \
            oud = *ptr;\
            ATOMIC_SET(ptr, oud, toset, vorig, instr); \
        } while( vorig!=oud );\
        return vorig; \
    }

MKSETFUNC(int8_t, CAS1)
MKSETFUNC(uint8_t, CAS1)

MKSETFUNC(int16_t, CAS2)
MKSETFUNC(uint16_t, CAS2)

MKSETFUNC(int32_t, CAS4)
MKSETFUNC(uint32_t, CAS4)

// Attempt once to set *ptr to 'toset' on the precondition that *ptr=='oud'
// (this is for an atomic try-lock. If succeeds: you have it locked
//  if it fails someone had the lock already)
// 
// if( atomic_try_set(ptrtolock, 1, 0) ) {
//    // w00t!
// } else {
//    // bummer :-(
// }
#define MKTRYSETFUNC(type, instr) \
    static inline bool  atomic_try_set(type volatile* ptr, const type toset, const type oud) { \
        volatile type vorig; \
        ATOMIC_SET(ptr, oud, toset, vorig, instr); \
        return ( vorig==oud );\
    }

MKTRYSETFUNC(int8_t, CAS1)
MKTRYSETFUNC(uint8_t, CAS1)

MKTRYSETFUNC(int16_t, CAS2)
MKTRYSETFUNC(uint16_t, CAS2)

MKTRYSETFUNC(int32_t, CAS4)
MKTRYSETFUNC(uint32_t, CAS4)

#endif // __i386__



// Intel 64bit x86_64
#ifdef __x86_64__

#endif

#endif // JIVE5AB_ATOMIC_H
