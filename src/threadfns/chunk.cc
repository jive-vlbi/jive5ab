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
#include <threadfns/chunk.h>

///////////////////////////////////////////////////////////////////
//          filemetadata
///////////////////////////////////////////////////////////////////
filemetadata::filemetadata():
    fileSize( (off_t)0 )
{}

filemetadata::filemetadata(const std::string& fn, off_t sz, uint32_t csn):
    fileSize( sz ), chunkSequenceNr( csn ), fileName( fn )
{}

