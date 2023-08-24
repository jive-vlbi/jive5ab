// Description of a chunk of data
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
//
#ifndef JIVE5A_THREADFNS_CHUNK_H
#define JIVE5A_THREADFNS_CHUNK_H

#include <string>
#include <iostream>
#include <sys/types.h>
#include <threadfns.h>


// fileName should be a relative path "<scan>/<scan>.<number>"
// such that it can be appended to any old mountpoint or root
struct filemetadata {
    off_t        fileSize;
    uint32_t     chunkSequenceNr;
    std::string  fileName;

    filemetadata();
    filemetadata(const std::string& fn, off_t sz, uint32_t csn);
};

template <typename CharT, typename Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, struct filemetadata const& fmd) {
    return os << fmd.fileName << " (" << fmd.chunkSequenceNr << ")";
}

// A chunk consists of some meta data + the contents
typedef taggeditem<filemetadata, block>  chunk_type;

#endif
