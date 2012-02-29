// support data splitting operations - map name to functionpointer
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
#include <splitstuff.h>
#include <map>
#include <sse_dechannelizer.h>
#include <dosyscall.h>

using namespace std;

// Keep a global registry of available splitfunctions
typedef map<string, splitproperties_type> functionmap_type;

functionmap_type mk_functionmap(void);

static functionmap_type functionmap = mk_functionmap();


splitproperties_type::splitproperties_type(const string& nm, splitfunction f, unsigned int n):
    name(nm), nchunk(n), fnptr(f)
{}


splitproperties_type const* find_splitfunction(const std::string& nm) {
    splitproperties_type const*      rv = 0;
    functionmap_type::const_iterator sf = functionmap.find(nm);

    if( sf!=functionmap.end() )
        rv = &sf->second;
    return rv;
}

// Mark K's dechannelization routines have a different calling sequence than
// we do, fix that in here
void marks_2Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1) {
    extract_2Ch2bit1to2(block, d0, d1, blocksize/2);
}

void marks_4Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3) {
    extract_4Ch2bit1to2(block, d0, d1, d2, d3, blocksize/4);
}

void marks_8Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                            void* d4, void* d5, void* d6, void* d7) {
    extract_8Ch2bit1to2(block, d0, d1, d2, d3, d4, d5, d6, d7, blocksize/8);
}

void marks_8Ch2bit(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                        void* d4, void* d5, void* d6, void* d7) {
    extract_8Ch2bit(block, d0, d1, d2, d3, d4, d5, d6, d7, blocksize/8);
}

void marks_16Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                             void* d4, void* d5, void* d6, void* d7,
                                                             void* d8, void* d9, void* d10, void* d11, 
                                                             void* d12, void* d13, void* d14, void* d15) {
    extract_16Ch2bit1to2(block, d0, d1, d2, d3, d4, d5, d6, d7,
                                d8, d9, d10, d11, d12, d13, d14, d15, blocksize/16);
}



// All available splitfunctions go here
functionmap_type mk_functionmap( void ) {
    functionmap_type  rv;

    ASSERT_COND( rv.insert(make_pair("2Ch2bit1to2",
                                     splitproperties_type("extract_2Ch2bit1to2",
                                                          (splitfunction)&marks_2Ch2bit1to2,
                                                          2))).second );
    ASSERT_COND( rv.insert(make_pair("4Ch2bit1to2",
                                     splitproperties_type("extract_4Ch2bit1to2",
                                                          (splitfunction)&marks_4Ch2bit1to2,
                                                          4))).second );
    ASSERT_COND( rv.insert(make_pair("8Ch2bit1to2",
                                     splitproperties_type("extract_8Ch2bit1to2",
                                                          (splitfunction)&marks_8Ch2bit1to2,
                                                          8))).second );
    ASSERT_COND( rv.insert(make_pair("8Ch2bit",
                                     splitproperties_type("extract_8Ch2bit",
                                                          (splitfunction)&marks_8Ch2bit,
                                                          8))).second );
    ASSERT_COND( rv.insert(make_pair("16Ch2bit1to2",
                                     splitproperties_type("extract_16Ch2bit1to2",
                                                          (splitfunction)&marks_16Ch2bit1to2,
                                                          16))).second );
    ASSERT_COND( rv.insert(make_pair("16bitx2",
                                     splitproperties_type("split16bitby2",
                                                          (splitfunction)&split16bitby2,
                                                          2))).second );
    ASSERT_COND( rv.insert(make_pair("8bitx4",
                                     splitproperties_type("split8bitby4",
                                                          (splitfunction)&split8bitby4,
                                                          4))).second );

    return rv;
}

