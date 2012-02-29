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
//
//
// HV: Splitfunctions take a block of data and split it into up to 16 parts
//     (yes that is a hard limit at the moment).
//     
#ifndef JIVE5A_SPLITSTUFF_H
#define JIVE5A_SPLITSTUFF_H

#include <string>

// For a function to be considered a splitfunction it should have this
// signature.
// "The system" will call your function with 16 pointers.
// Only the registered (see below in the properties_type) amount of pointers
// actually point a adressable storage.
typedef void (*splitfunction)(void* block, unsigned int blocksize, ...);


// Define a struct keeping all important properties of a splitfunction
// together.
struct splitproperties_type {
    // Define an entry for a splitfunction 'f' that splits a block
    // in 'n' chunks
    splitproperties_type(const std::string& nm, splitfunction f, unsigned int n);

    const std::string   name;
    const unsigned int  nchunk;
    const splitfunction fnptr;
};


// Keep a global registry of defined splitfunctions, allow lookup by name.
// May return NULL / 0 if the indicated splitfunction can't be found
splitproperties_type const* find_splitfunction(const std::string& nm);
#endif
