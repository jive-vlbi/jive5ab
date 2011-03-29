// Very thin wrapper around a struct iov_block
// Copyright (C) 2007-2008 Harro Verkouter
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
#ifndef JIVE5A_BLOCK_H
#define JIVE5A_BLOCK_H

#include <sys/types.h>
#include <sys/socket.h>  // for struct iovec 
#include <stddef.h>


struct block {
    // empty block: iov_len == 0 and iov_base == 0
    block();

    block(const struct iovec iov);

    // initialized block: point at sz bytes starting from
    // base
    block(void* base, size_t sz);

    // convenience function that return true iff the
    // block is empty by what we take to mean empty,
    // ie iov_base==0 AND iov_len==0
    bool empty( void ) const;

    void*   iov_base;
    size_t  iov_len;
};

#endif
