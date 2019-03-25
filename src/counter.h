#ifndef EVLBI5A_COUNTER_H
#define EVLBI5A_COUNTER_H
#include <stdint.h> // for [u]int<N>_t  types
#include <iostream>

// If we compile w/o debug or with low enough debug level then all counters
// are real counters, otherwise we can completely disable memory access to
// them [this is useful for valgrind/helgrind because because of performance
// reasons access to these counters is typically done unlocked in a MT
// environment. Mainly because we know they're just statistics counters we don't care
// if they incidentally get clobbered or if we read a value that is in the
// middle of being updated by someone else.]
#if !defined(GDBDEBUG) || GDBDEBUG<2

typedef volatile int64_t  counter_type;
typedef volatile uint64_t ucounter_type;

#else

// For the memory-access less compilation we typedef the counter to a struct
// which behaves like an int64_t/uint64_t for all use cases in the code but
// translates those accesses to a no-op.
struct real_counter_type {

    explicit real_counter_type();
    real_counter_type(int);
    real_counter_type(uint64_t);
    real_counter_type(int64_t);

    template <typename T>
    real_counter_type const& operator+=(T const&) {
        return *this;
    }

    operator bool( void )     const;
    operator double( void )   const;
    operator int64_t( void )  const;
    operator uint64_t( void ) const;

    real_counter_type const& operator++( void );
    real_counter_type const& operator++( int );
};

std::ostream& operator<<(std::ostream& os, real_counter_type const&);

real_counter_type operator-(real_counter_type const&, real_counter_type const&);

template <typename T>
T operator-(T const&, real_counter_type const&) {
    return T();
}

template <typename T>
real_counter_type operator-(real_counter_type const&, T const&) {
    return real_counter_type();
}
template <typename T>
real_counter_type operator+(real_counter_type const&, T const&) {
    return real_counter_type();
}

typedef real_counter_type   counter_type;
typedef real_counter_type   ucounter_type;

#endif  // GDBDEBUG>=2


#endif
