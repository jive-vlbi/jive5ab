// handy templates for C-style arrays
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
#ifndef EVLBI5A_CARRAY_UTIL_H
#define EVLBI5A_CARRAY_UTIL_H

// handy template for finding a particular element in a 
// plain-old-C style array.
//
// Note: done like this because the STL version of
//       "std::find(carray, carray+N, element)" did not compile
//       *always* - depending on the actual size of the C-style
//       array ("N" in the example above) in combination with
//       particular optimization level(s) using gcc. It just
//       didn't always compile.
//
// This one searches the whole array:
//
// int   iarr[] = {-1, -2, 8, 42, 17};
//
// if( find_element(8, iarr) )
//      do_something();
//
template <typename T, std::size_t N>
bool find_element(T const& t, const T(&arr)[N]) {
    std::size_t       i;
    for(i=0; i<N; i++)
        if( arr[i]==t )
            break;
    return (i!=N);
}

template <typename T, std::size_t N>
std::size_t array_size(T(&)[N]) {
    return N;
}

#endif
