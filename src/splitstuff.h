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

#include <jit.h>
#include <string>
#include <ezexcept.h>
#include <headersearch.h>
#include <countedpointer.h>
#include <dynamic_channel_extractor.h>

DECLARE_EZEXCEPT(spliterror)

// For a function to be considered a splitfunction it should have this
// signature.
// "The system" will call your function with 16 pointers.
// Only the registered (see below in the properties_type) amount of pointers
// actually point a adressable storage.
typedef void (*splitfunction)(void* block, unsigned int blocksize, ...);


// Define a struct keeping all important properties of a splitfunction
// together.
struct splitproperties_type {
    // Use this value to signal that you want to accumulate as many
    // chunks as this splitter splits them into
    static const unsigned int natural_accumulation = (unsigned int)-1;

    // empty splitprops - using any of the name/nchunk/fnptr will
    // most likely cause harm to innocent fluffy creatures. don't do it.
    splitproperties_type();

    // Define an entry for a splitfunction 'f' that splits a block
    // in 'n' chunks
    splitproperties_type(const std::string& nm, splitfunction f,
                         unsigned int n, jit_handle h = jit_handle());

    // Define an entry for a splitfunction 'f' that splits a block
    // in .real() chunks at .imag() tracks per chunk - this is for
    // extraction rather than splitting:
    //    split   : 16 ch input => 16 ch output
    //    extract : 16 ch input => <= 16 ch output
    // In the former case (split), the amount of tracks in the output
    // [see headersearch.h] is directly computed from inputheader divided
    // by the number of chunks the frame is split into.
    splitproperties_type(const std::string& nm, splitfunction f,
                         const extractorconfig_type& e, jit_handle h = jit_handle());

    splitfunction      fnptr( void ) {
        return impl->fnptr;
    }
    unsigned int       nchunk( void ) const {
        return (impl->config?impl->config->channels.size():impl->nchunk.real());
    }
    const std::string& name( void ) const {
        return impl->name;
    }

    // Return the resultant header when this split/accumulate is
    // applied to the given input format
    headersearch_type outheader(const headersearch_type& inheader,
                                unsigned int naccumulate=natural_accumulation ) const;

    private:
        struct spimpl_type {
            spimpl_type();
            spimpl_type(const std::string& nm, splitfunction f,
                        std::complex<unsigned int> c, jit_handle h);
            spimpl_type(const std::string& nm, splitfunction f,
                        const extractorconfig_type& e, jit_handle h);
            ~spimpl_type();

            // In case we were dynamically compiled + loaded - this object 
            // holds the code. If we are destroyed, this one will
            // automatically unload the dll
            jit_handle                        jit;
            const std::string                 name;
            const extractorconfig_type*       config;
            const std::complex<unsigned int>  nchunk;
            const splitfunction               fnptr;
        };

        countedpointer<spimpl_type> impl;
};


// Keep a global registry of defined splitfunctions, allow lookup by name.
// May return NULL / 0 if the indicated splitfunction can't be found
splitproperties_type find_splitfunction(const std::string& nm);

#endif
