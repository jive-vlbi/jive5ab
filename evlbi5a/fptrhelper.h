// support casting pointer-to-void to pointer-to-function [it's not standardized!]
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
#ifndef JIVE5A_FPTRHELPER_H
#define JIVE5A_FPTRHELPER_H

#include <stdint.h>

// Helper stuff for converting a "void*"
// into a pointer-to-function.
// Apparently, there is no (official) standardized way to convert the
// returnvalue of "::dlsym()" (which is 'void*') into a functionpointer!
// Most compilers accept it silently, however, if you ask the compiler to be
// standardscompliant ('-pedantic') it will refuse to do it.
//
// The trick is to get the 'void*' returnvalue from ::dlsym() into an
// appropriately sized integral type which then can be casted into a
// pointer-to-function.
//
// So, what we do is two assertions:
//    * sizeof(void*) == sizeof( pointer-to-function )
//      (if this can't be guaranteed we're stuft)
//    * get an integral type such that
//      sizeof(integral-type) == sizeof(void*)
//      (such that we can - hopefully - transfer the bytes between the two
//      w/o reserve. this depends on the platform having the same bytelayout
//      in a void*, an integral type and a pointer-to-function. this may not
//      always be the case ...)

// The 'ptr_int_type' struct is templated on the size of a 'void*'. It
// defines an integral type containing the same amount of bytes. Currently
// we support 32bit and 64bit pointers
template <size_t> 
struct ptr_int_type { };

template <>
struct ptr_int_type<4> {
    typedef uint32_t size_type;
};

template <>
struct ptr_int_type<8> {
    typedef uint64_t size_type;
};

// The fptr_helper is templated on a bool and is only defined for the 'true'
// case. You _should_ use it as:
//   fptrhelper_type< sizeof(void*)==sizeof(func_ptr) >
// such that it asserts that 'void*' and a functionpointer are basically the
// same size. 
template <bool>
struct ptrequal_type {};

template <>
struct ptrequal_type<true> {
    typedef ptr_int_type<sizeof(void*)>::size_type size_type;
};

template <typename FPTR>
struct fptrhelper_type { 
    typedef typename ptrequal_type< sizeof(void*)==sizeof(FPTR) >::size_type size_type;
};


#endif
