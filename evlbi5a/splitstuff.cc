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
#include <time.h>

#include <evlbidebug.h>
#include <stringutil.h>
#include <sse_dechannelizer.h>

using namespace std;

DEFINE_EZEXCEPT(spliterror)

// Use as:
//    SPLITASSERT( <assertion> )
//    SPLITASSERT(::malloc(num)!=0 )
//    or
//    SPLITASSERT2( <assertion>, <string/stream> );
//    SPLITASSERT2(::malloc(num)!=0, "failed to allocate " << num << " bytes");
#define SPLITASSERT(a)     EZASSERT(a, spliterror)
#define SPLITASSERT2(a, e) EZASSERT2(a, spliterror, EZINFO(e))

// Keep a global registry of available splitfunctions
typedef map<string, splitproperties_type> functionmap_type;

functionmap_type mk_functionmap(void);

static functionmap_type functionmap = mk_functionmap();

splitproperties_type::splitproperties_type():
    impl( new spimpl_type() )
{}

splitproperties_type::splitproperties_type(const string& nm, splitfunction f,
                                           unsigned int n, jit_handle h):
    impl( new spimpl_type(nm, f, complex<unsigned int>(n,0), h) )
{ SPLITASSERT2(n>0, "Cannot create splitter for 0 pieces"); }

splitproperties_type::splitproperties_type(const string& nm, splitfunction f,
                                           const extractorconfig_type& e, jit_handle h):
    impl( new spimpl_type(nm, f, e, h) )
{}

// natural accumulation is the number of input frames that need to be 
// accumulated to have an outputsize in the each chunk equal to the 
// number of bytes in an inputframe.
// If the splitproperties was created from an extractorconfig we must
// take into account a scaling of extractor-inputbits to
// inputheader-numberoftracks; the extractor extracts 'bitsperchannel' bits
// from 'bitsperinputword' amount of bits. if the inputheader claims to
// have 'ntrack' bits it does not follow that 'ntrack' ==
// 'bitsperinputword'
headersearch_type splitproperties_type::outheader( const headersearch_type& inheader, 
                                                   unsigned int naccumulate ) const {
    complex<unsigned int>       chunk  = impl->nchunk;

    // if the .real() part == 0, this implies we were constructed from an 
    // extractorconfig [the c'tor asserts that .real()>0 when creating from
    // NOT an extractorconfig].
    // The .real() part tells how many output buffers this splitter is
    // filling.
    if( impl->config ) {
        const extractorconfig_type& config( *impl->config );
        chunk = complex<unsigned int>(config.channels.size(), 
                                      (inheader.ntrack * config.bitsperchannel) / config.bitsperinputword);
    }
    // now we can form the split header
    const headersearch_type splithdr = inheader/chunk;

    // and account for accumulation
    return splithdr * ((naccumulate==natural_accumulation)?(inheader.payloadsize/splithdr.payloadsize):naccumulate);
}

splitproperties_type::spimpl_type::spimpl_type():
    config(0), nchunk(0), fnptr((splitfunction)0)
{}

splitproperties_type::spimpl_type::spimpl_type(const string& nm, splitfunction f,
                                               complex<unsigned int> c, jit_handle h):
    jit(h), name(nm), config(0), nchunk(c), fnptr(f)
{}

splitproperties_type::spimpl_type::spimpl_type(const string& nm, splitfunction f,
                                               const extractorconfig_type& e, jit_handle h):
    jit(h), name(nm), config(new extractorconfig_type(e)), fnptr(f)
{}

splitproperties_type::spimpl_type::~spimpl_type() {
    delete config;
}


splitproperties_type find_splitfunction(const std::string& nm) {
    splitproperties_type             rv;
    functionmap_type::const_iterator sf = functionmap.find(nm);

    if( sf!=functionmap.end() ) {
        rv = sf->second;
    } else {
        // see if we can parse the name as a channeldefinition
        string               dynamic_channel_extractor_code;
        jit_handle           jit;
        ostringstream        oss;
        splitfunction        dce_fn;
        extractorconfig_type extractorconfig( parse_dynamic_channel_extractor(nm) );

        // The parsing went ok, now generate the code
        dynamic_channel_extractor_code = generate_dynamic_channel_extractor(extractorconfig, "jive5ab_dce");
        DEBUG(4, "splitproperties_type: generated DynamicChannelExtractor code:" << endl <<
                 dynamic_channel_extractor_code << endl);

        // Now 'all we need to do' is compile, link + load the generated C
        // code ...
        jit = jit_c_compile( dynamic_channel_extractor_code );

        // Hoorah! Now extract the symbol 'jive5ab_dce' It will be converted
        // to type 'splitfunction'
        dce_fn = jit.function<splitfunction>("jive5ab_dce");
        SPLITASSERT2(dce_fn!=0, "could not extract symbol from dynamically loaded code?!");

        // Now we can generate the actual splitproperties_type!
        oss << extractorconfig;
        rv = splitproperties_type(oss.str(), dce_fn, extractorconfig, jit);
    }
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
void harros_16Ch2bit1to2(void* block, unsigned int blocksize, void* d0, void* d1, void* d2, void* d3, 
                                                              void* d4, void* d5, void* d6, void* d7,
                                                              void* d8, void* d9, void* d10, void* d11, 
                                                              void* d12, void* d13, void* d14, void* d15) {
    extract_16Ch2bit1to2_hv(block, blocksize/16, d0, d1, d2, d3, d4, d5, d6, d7,
                                                 d8, d9, d10, d11, d12, d13, d14, d15);
}

// All available splitfunctions go here
functionmap_type mk_functionmap( void ) {
    functionmap_type  rv;

    SPLITASSERT( rv.insert(make_pair("2Ch2bit1to2",
                                     splitproperties_type("extract_2Ch2bit1to2",
                                                          (splitfunction)&marks_2Ch2bit1to2,
                                                          2))).second );
    SPLITASSERT( rv.insert(make_pair("4Ch2bit1to2",
                                     splitproperties_type("extract_4Ch2bit1to2",
                                                          (splitfunction)&marks_4Ch2bit1to2,
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("8Ch2bit1to2",
                                     splitproperties_type("extract_8Ch2bit1to2",
                                                          (splitfunction)&marks_8Ch2bit1to2,
                                                          8))).second );
    SPLITASSERT( rv.insert(make_pair("8Ch2bit1to2_hv",
                                     splitproperties_type("extract_8Ch2bit1to2_hv",
                                                          (splitfunction)&extract_8Ch2bit1to2_hv/*harros_8Ch2bit1to2*/,
                                                          8))).second );
    SPLITASSERT( rv.insert(make_pair("8Ch2bit",
                                     splitproperties_type("extract_8Ch2bit",
                                                          (splitfunction)&marks_8Ch2bit,
                                                          8))).second );
    SPLITASSERT( rv.insert(make_pair("8Ch2bit_hv",
                                     splitproperties_type("extract_8Ch2bit_hv",
                                                          (splitfunction)&extract_8Ch2bit_hv,
                                                          8))).second );
    SPLITASSERT( rv.insert(make_pair("16Ch2bit1to2",
                                     splitproperties_type("extract_16Ch2bit1to2",
                                                          (splitfunction)&marks_16Ch2bit1to2,
                                                          16))).second );
    SPLITASSERT( rv.insert(make_pair("16Ch2bit1to2_hv",
                                     splitproperties_type("extract_16Ch2bit1to2_hv",
                                                          (splitfunction)&harros_16Ch2bit1to2,
                                                          16))).second );
    SPLITASSERT( rv.insert(make_pair("16bitx2",
                                     splitproperties_type("split16bitby2",
                                                          (splitfunction)&split16bitby2,
                                                          2))).second );
    SPLITASSERT( rv.insert(make_pair("16bitx4",
                                     splitproperties_type("split16bitby4",
                                                          (splitfunction)&split16bitby4,
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("8bitx4",
                                     splitproperties_type("split8bitby4",
                                                          (splitfunction)&split8bitby4,
                                                          4))).second );
    SPLITASSERT( rv.insert(make_pair("32bitx2",
                                     splitproperties_type("split32bitby2",
                                                          (splitfunction)&split32bitby2,
                                                          2))).second );

    return rv;
}


