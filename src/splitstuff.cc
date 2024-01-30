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
#include <time.h>
#include <evlbidebug.h>
#include <stringutil.h>
#include <fptrhelper.h>

using namespace std;

DEFINE_EZEXCEPT(spliterror)

// Such that we can call it here 
functionmap_type functionmap = mk_functionmap();

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
        dce_fn = jit.jit_handle::function<splitfunction>("jive5ab_dce");
        SPLITASSERT2(dce_fn!=0, "could not extract symbol from dynamically loaded code?!");

        // Now we can generate the actual splitproperties_type!
        oss << extractorconfig;
        rv = splitproperties_type(oss.str(), dce_fn, extractorconfig, jit);
    }
    return rv;
}

