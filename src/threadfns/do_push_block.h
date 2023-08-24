// free function that pushes tagged/untagged block based on argument
// Copyright (C) 2007-2023 Marjolein Verkouter
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
// Author:  Marjolein Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef JIVE5A_THREADFNS_DO_PUSH_BLOCK_H
#define JIVE5A_THREADFNS_DO_PUSH_BLOCK_H

#include <block.h>
#include <chain.h>

// Do this via a function template - that way the implementation can be in header files
// as specializations
template <typename Item>
inline bool do_push_block(outq_type<Item>*, block, unsigned int);

// Specializations
template <>
inline bool do_push_block(outq_type<block>* q, block b, unsigned int) {
    return q->push( b );
}

template <>
inline bool do_push_block(outq_type< tagged<block> >* q, block b, unsigned int t) {
    return q->push( tagged<block>(t, b) );
}

#endif
