// implementation of the constraints
// Copyright (C) 2007-2010 Harro Verkouter
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
#include <constraints.h>

using namespace std;


// prototypes of functions only defined & used inside this module
constraintset_type constraints_from_nw(const netparms_type& np);
constraintset_type constrain(const constraintset_type& in, const solution_type& solution);
constraintset_type constrain_by_blocksize(const constraintset_type&, const solution_type&);
constraintset_type constrain_by_framesize(const constraintset_type&, const solution_type&);


// Implementation of the API methods
constraintset_type constrain(const netparms_type& netparms) {
    return constrain(netparms, headersearch_type(), solution_type());
}

constraintset_type constrain(const netparms_type& netparms,
                             const solution_type& solution) {
    return constrain(netparms, headersearch_type(), solution);
}

constraintset_type constrain(const netparms_type& netparms,
                             const headersearch_type& hdr) {
    return constrain(netparms, hdr, solution_type());
}

constraintset_type constrain(const netparms_type& netparms,
                             const headersearch_type& hdr,
                             const solution_type& solution) {
    // start with the constraints imposed by the chosen networkparameters
    constraintset_type lcl( constraints_from_nw(netparms) );

    // If compression required we must constrain by
    // framesize - the compressor must be sure to
    // compress whole frames.
    // That is: only if there is a known format
    if( solution && hdr.valid() ) {
        lcl[constraints::framesize]       = hdr.framesize;
        // On the Mk5B's we skip compressing the header
        // (there's only one for all tracks)
        if( hdr.frameformat==fmt_mark5b )
            lcl[constraints::compress_offset] = hdr.headersize;
    }
    // VDIF over UDP (==VTP) means we must constrain by
    // VDIF framesize
    if( is_vdif(hdr.frameformat) && netparms.get_protocol().find("udp")!=string::npos )
       lcl[constraints::framesize] = hdr.framesize; 
    return constrain(lcl, solution);
}

//
// ###################### implementation cruft below here #################
//


// implement the constraint_error exception
namespace constraints {
    constraint_error::constraint_error(const std::string& m):
        _msg( "constraint error: "+m )
    {}

    const char* constraint_error::what( void ) const throw () {
        return _msg.c_str();
    }
    constraint_error::~constraint_error() throw () {}
}


// output in readable form
#define KEES(a)  case constraints::a : return os << #a;
std::ostream& operator<<(std::ostream& os, constraints::constraints c) {
    switch( c ) {
        KEES(framesize);
        KEES(blocksize);
        KEES(MTU);
        KEES(compress_offset);
        KEES(application_overhead);
        KEES(protocol_overhead);
        KEES(read_size);
        KEES(write_size);
        KEES(n_mtu);
        default:
            break;
    }
    return os << "<WTF!?>";
}

// memberfunctions of the constraintset
unsigned int& constraintset_type::operator[]( constraints::constraints c ) {
    constraint_container::iterator p = constraints.find( c );
    // if we need to insert new entry - make sure default value is
    // 'unconstrained'
    if( p==constraints.end() )
        p = constraints.insert(make_pair(c, constraints::unconstrained)).first;
    return p->second;
}
const unsigned int& constraintset_type::operator[]( constraints::constraints c ) const {
    constraint_container::const_iterator p = constraints.find( c );
    // if we need to insert new entry - make sure default value is
    // 'unconstrained'
    if( p==constraints.end() )
        return constraints::unconstrained;
    return p->second;
}

#define ASSERT(a) \
    if( !(a) ) { \
        std::ostringstream y_BH_t2x; \
        y_BH_t2x << "assertion '" << #a << "' fails!";\
        throw constraints::constraint_error(y_BH_t2x.str());\
    }
#define ISSET(c)           CONSTRAINED(*this, c)
#define ISUNCONSTRAINED(c) UNCONSTRAINED(*this, c)
#define GETCONSTRAINT(c)   CONSTRAINT(*this, c)

void constraintset_type::validate( void ) const {
    // those MUST have values
    ASSERT( ISSET(blocksize) );
    ASSERT( ISSET(read_size) );
    ASSERT( ISSET(write_size) );
    ASSERT( ISSET(MTU) );
    ASSERT( ISSET(protocol_overhead) );
    ASSERT( ISSET(application_overhead) );
    ASSERT( ISSET(compress_offset) );
    // these may not be 0
    ASSERT( GETCONSTRAINT(blocksize)>0 );
    ASSERT( GETCONSTRAINT(read_size)>0 );
    ASSERT( GETCONSTRAINT(write_size)>0 );
    ASSERT( GETCONSTRAINT(MTU)>0 );
    // these must be a multple of 8
    ASSERT( (GETCONSTRAINT(blocksize)%8)==0 );
    //ASSERT( (GETCONSTRAINT(read_size)%8)==0 );
    ASSERT( (GETCONSTRAINT(compress_offset)%8)==0 );
    // n_mtu may only be 1 or unconstrained
    ASSERT( GETCONSTRAINT(n_mtu)==1 || ISUNCONSTRAINED(n_mtu) );

    // blocksize must be an integral multiple of readsize (including the
    // multiple "1").
    ASSERT( GETCONSTRAINT(blocksize)>=GETCONSTRAINT(read_size) );
    ASSERT( (GETCONSTRAINT(blocksize)%GETCONSTRAINT(read_size))==0 );

    // compress_offset must be less than read_size such that there is data
    // left to (optionally) compress after reading read_size bytes and
    // skipping compress_offset bytes
    ASSERT( GETCONSTRAINT(compress_offset)<GETCONSTRAINT(read_size) );

    // write size should be less than mtu - protocol_overhead -
    // application_overhead. that is, when n_mtu!=unconstrained ...
    if( GETCONSTRAINT(n_mtu)==1 ) {
        ASSERT( (GETCONSTRAINT(protocol_overhead)+GETCONSTRAINT(application_overhead)+GETCONSTRAINT(write_size))<=GETCONSTRAINT(MTU) );
    }

    // write size must never be > read_size [when reading compressed data
    // you should read 'write_size' and allocate 'read_size' - that way the
    // definitions of the sizes and the direction of dataflow are fixed,
    // leaving little to the imagination]
    // write-size does not have to fit an integral amount of times into
    // read-size. write-size is just the size of payload after OPTIONAL
    // compression. If no compression then write-size IS read-size but
    // that's just coincidence.
    ASSERT( GETCONSTRAINT(write_size)<=GETCONSTRAINT(read_size) );

    // framesize constrained? in that case frame/block should be integral
    // multiples! we do not care which one is the bigger one either way of
    // being a multiple is good enough for us
    if( ISSET(framesize) ) {
        // if framesize set it better be non-zero and a multiple of 8
        ASSERT( GETCONSTRAINT(framesize)>0 );
        ASSERT( (GETCONSTRAINT(framesize)%8)==0 );
        // Uncompressed readsize divides an integral amount
        // of times into the framesize
        ASSERT( (GETCONSTRAINT(framesize)%GETCONSTRAINT(read_size))==0 );
// After discussion between BobE and HarroV it seems
// that the following constraints serve no purpose.
// For now we relax them. 
#if 0
        if( GETCONSTRAINT(framesize)>GETCONSTRAINT(blocksize) ) {
            ASSERT( (GETCONSTRAINT(framesize)%GETCONSTRAINT(blocksize))==0 );
        } else {
            ASSERT( (GETCONSTRAINT(blocksize)%GETCONSTRAINT(framesize))==0 );
        }
#endif
    }
    return;
}



// display the constraintset in readable form
std::ostream& operator<<(std::ostream& os, const constraintset_type& cs) {
    static const char* const             empty = "";
    static const char* const             space = " ";
    char const*                          sep = empty;
    constraint_container::const_iterator p;
    os << "[";
    for(p=cs.constraints.begin(); p!=cs.constraints.end(); sep=space, p++) {
        os << sep << "<" << p->first << ":";
        if( p->second==constraints::unconstrained )
            os << "unconstrained";
        else
            os << p->second;
        os << ">";
    }
    return os << "]";
}


// Return a constraintset with those values set that can be
// derived from the settings in netparms
constraintset_type constraints_from_nw(const netparms_type& np) {
    // IP header is 20 bytes
    unsigned int        iphdr = 20;
    // application header defaults to 0
    unsigned int        aphdr = 0;
    const std::string&  proto = np.get_protocol();
    constraintset_type  rv;

    // all flavours of tcp protocol have at least 6 4-byte words of
    // tcp header after the IP header. we assume that we do not have any
    // "OPTIONS" set - they would add to the tcp headersize. Leave n_mtu
    // unconstrained; this will indicate to the "solvers" that we're not
    // doing packet-based transfers ie the read/write sizes will span an
    // "unconstrained" number of MTUs
    //
    // all flavours of udp have 4 2-byte words of udp header after the IP
    // header. If we run udps (s=smart or sequencenumber) this means each
    // datagram contains a 64-bit sequencenumber immediately following the
    // udp header. For all UDP-based protocols we constrain the n_mtu
    // to 1 (one mtu/payload).
    if( proto.find("tcp")!=string::npos )
        iphdr += 6*4;
    else if( proto.find("udp")!=string::npos || proto.find("udt")!=string::npos ) {
        iphdr += 4*2;
        rv[constraints::n_mtu] = 1;
        if( proto=="udps" )
            aphdr = sizeof(uint64_t);
    }
    // These values get set w/o reserve.
    rv[constraints::MTU]                  = np.get_mtu();
    rv[constraints::blocksize]            = np.get_blocksize();
    rv[constraints::protocol_overhead]    = iphdr;
    rv[constraints::application_overhead] = aphdr;

    return rv;
}


// Main entry-point of solving the constraints
constraintset_type constrain(const constraintset_type& in, const solution_type& solution) {
    constraintset_type lcl( in );

    // we have already copied the argument, now assert a couple of things
    // and change - if necessary - some unconstraineds into "0" [for better
    // functioning of the arithmetic ...]

    // if compress_offset was not set yet or if it was set but the solution
    // is empty (ie no compression) then we set the compress offset to 0
    // bluntly
    if( in[constraints::compress_offset]==constraints::unconstrained || !solution )
        lcl[constraints::compress_offset] = 0;

    // now that we've asserted & fixed up things we hand over to the actual
    // constraining algo's. The major difference is in wether it is the
    // framesize or the blocksize who we have to adhere to; the framesize is
    // a fixed size, the blocksize is an upperbound, a hint if you will.
    if( lcl[constraints::framesize]==constraints::unconstrained )
        return constrain_by_blocksize(lcl, solution);
    else
        return constrain_by_framesize(lcl, solution);
}

// Compute the compressed size when a block of size uncompressedbytes is
// compressed using the given compression solution.
// Take care of units here - blocksizes passed in and returned are in units
// of bytes; the compressor works in units of WORDS of size 64bit
unsigned int compressed_size(unsigned int uncompressedbytes, const solution_type& s) {
    if( !s )
        return uncompressedbytes;
    ASSERT_COND( (uncompressedbytes%8)==0 );
    return s.outputsize( uncompressedbytes/8 )*8;
}

// Compute the uncompressed amount of bytes that would lead to a block of
// size compressedbytes number of bytes when using the compression solution
// "s". If no compression it is mighty trivial ;) 
// Remark on units applies here as well ...
unsigned int uncompressed_size(unsigned int compressedbytes, const solution_type& s) {
    if( !s )
        return compressedbytes;
    ASSERT_COND( (compressedbytes%8)==0 );
    return s.inputsize( compressedbytes/8 )*8;
}

constraintset_type constrain_by_blocksize(const constraintset_type& in, const solution_type& solution) {
    unsigned int         rd_size, wr_size;
    unsigned int         mtu, n_mtu, proto_overhead;
    unsigned int         blocksize;
    unsigned int         app_overhead, compress_offset;
    // how many bytes should we at least read? if there is a
    // compressionscheme then we must at least read the compression-cycle's
    // size worth of bytes, otherwise one word should be the absolute
    // minimum
    const unsigned int   min_read_bytes( (solution)?(solution.cycle()*8):8 );

    // solve for a blocksize which will accomodate rd_size
    mtu             = in[constraints::MTU];
    n_mtu           = in[constraints::n_mtu];
    blocksize       = in[constraints::blocksize];
    proto_overhead  = in[constraints::protocol_overhead];
    app_overhead    = in[constraints::application_overhead];
    compress_offset = in[constraints::compress_offset];

    // make sure that we do not fall into the unsigned overflow/underflow
    // trap
    if( blocksize==constraints::unconstrained )
        throw constraints::constraint_error("constraining by blocksize but it's unconstrained?! WTF?!");
    if( (compress_offset%8)!=0 )
        throw constraints::constraint_error("compress_offset must be a multiple of 8");
    if( blocksize<(compress_offset+min_read_bytes) )
        throw constraints::constraint_error("blocksize is smaller than 'compress_offset+min_read_bytes'");
    if( n_mtu!=1 && n_mtu!=constraints::unconstrained )
        throw constraints::constraint_error("n_mtu can only be 1 or unconstrained");
    if( n_mtu==1 && (mtu < (proto_overhead+app_overhead+compress_offset)) )
        throw constraints::constraint_error("mtu too small - it is < (proto_overhead+app_overhead+compress_offset");

    // the following condition signals we're using a streaming protocol and
    // we do not have to fit everything inside a MTU. makes things a lot
    // easier. truncate blocksize to multiple of 8, set the readsize equal
    // to blocksize and set the write_size equal to the compressed size
    // of the whole thing
    if( n_mtu==constraints::unconstrained ) {
        constraintset_type   rv( in );

        blocksize &= ~0x7;
        rd_size    = blocksize;
        wr_size    = compressed_size(rd_size-compress_offset, solution) + compress_offset;

        rv[constraints::blocksize]  = blocksize;
        rv[constraints::read_size]  = rd_size;
        rv[constraints::write_size] = wr_size;

        return rv;
    }

    // start with the maximum "writesize", truncate to multiple of 8,
    // and then work our way downwards to find a suitable value, if any
    wr_size = (mtu - proto_overhead - app_overhead)  & ~0x7;
    // enables us to check wether the loop came up with a solution
    rd_size = constraints::unconstrained; 

    // We set the lower limit of datatransfer as compress_offset +
    // one full compressed chunk. If more will fit in the 
    // networkpacket that's an added bonus. 
    // It should be realized that this "constraint" is not actually
    // a physical one - the compression/decompression algorithms
    // work nicely on partial blocks - rather, it is one imposed by
    // me (Harro).
    // For that matter, the absolute lower limit is, ofcourse,
    // "compress_offset + 8", namely only ONE word (8-byte word - our
    // quantum of data) of output.
    const unsigned int abs_min_wr_size = (compress_offset + compressed_size(min_read_bytes/8, solution)*8);

    while( wr_size>=abs_min_wr_size ) {
        // find out how many bytes we would have to read to end up with a
        // compressed block of size what we currently hold in wr_size
        const unsigned int tst_rd_size = uncompressed_size(wr_size-compress_offset, solution) + compress_offset;

        // Have we found a test read_size that fits in our blocksize?
        if( tst_rd_size<=blocksize ) {
            const unsigned int tst_blocksize = (blocksize - (blocksize%tst_rd_size));
            // if the (possibly truncated-to-a-multiple-of-current-test-read-size) test
            // blocksize is also a multiple of eight, we're happy!
            if( tst_blocksize>0 && (tst_blocksize%8)==0 && (tst_rd_size%8)==0) {
                rd_size   = tst_rd_size;
                blocksize = tst_blocksize;
                break;
            }
        }
        wr_size -= 8;
    }

    if( rd_size==constraints::unconstrained ) {
        std::ostringstream o;
        o << "failed to find a suitable solution for " << in;
        throw constraints::constraint_error(o.str());
    }


    // create the returnvalue - give back the input with some modified
    // fields
    constraintset_type   rv( in );
    rv[constraints::blocksize]  = blocksize;
    rv[constraints::read_size]  = rd_size;
    rv[constraints::write_size] = wr_size;
    return rv;
}

constraintset_type constrain_by_framesize(const constraintset_type& in, const solution_type& solution) {
    unsigned int         rd_size, wr_size;
    unsigned int         mtu, n_mtu, proto_overhead;
    unsigned int         blocksize, framesize;
    unsigned int         app_overhead, compress_offset;

    // solve for a blocksize which will accomodate rd_size
    mtu             = in[constraints::MTU];
    n_mtu           = in[constraints::n_mtu];
    blocksize       = in[constraints::blocksize];
    framesize       = in[constraints::framesize];
    proto_overhead  = in[constraints::protocol_overhead];
    app_overhead    = in[constraints::application_overhead];
    compress_offset = in[constraints::compress_offset];

    // things that must hold
    if( (compress_offset%8)!=0 )
        throw constraints::constraint_error("compress_offset must be a multiple of 8");

    if( n_mtu!=1 && n_mtu!=constraints::unconstrained )
        throw constraints::constraint_error("n_mtu must be 1 or unconstrained");

    if( framesize==constraints::unconstrained || framesize==0 )
        throw constraints::constraint_error("constraining by framesize but it is invalid! (0 or unconstrained)");

    // if n_mtu == unconstrained our job is much easier!
    if( n_mtu==constraints::unconstrained ) {
        unsigned int       bs( (blocksize==constraints::unconstrained)?(framesize):(blocksize&~0x7) );
        constraintset_type rv( in );

        // if blocksize/framesize do not divide into each other
        // yet, make it so.
        // If bs > framesize: truncate bs such that an integral amount
        //                    of frames will fit
        // If bs < framesize: find a divider of framesize such that 
        //                    the chunks will be <= bs
        if( bs>framesize )
            bs -= (bs%framesize);
        else {
            unsigned int i;
            for(i=(framesize/bs); i<framesize && !((framesize%bs)==0 && (bs%8)==0 && bs>compress_offset); i++) {
                    bs = framesize/i;
            }
            // did we find a solution?
            if( i>=framesize )
                throw constraints::constraint_error("failed to find a suitable blocksize");
        }
        rv[constraints::blocksize]  = bs;
        rv[constraints::read_size]  = framesize;
        rv[constraints::write_size] = compressed_size(framesize-compress_offset, solution) + compress_offset;
        return rv;
    }

    // enables us to check wether the loop came up with a solution
    rd_size = constraints::unconstrained; 
    wr_size = constraints::unconstrained;

    // very strict rules apply; only a limited set of values of
    // rd_sz/wr_sz can be tested: only those that (1) fit an integral amount
    // of times in framesize and (2) are a multiple of 8
    // Well, the 2nd constraint (multiple of 8) only applies if we're doing
    // compression - it is the compressor which requires the read_size to
    // be a multiple of 8. Consequently, if we're NOT compressing, we don't
    // care what the read_size is. 
    // The blocksize, on the other hand, MUST be a multiple of 8 since that
    // is the size of I/Os to and from the streamstor device (which dictates
    // that transfers should be sized modulo 8).
    for(unsigned int i=1; rd_size==constraints::unconstrained && i<framesize; i++) {
        // Only check if the tst_rd_size is a multiple of 8 if we're doing
        // compression (ie "solution == true")
        if( (framesize%i)==0 && (!solution || (solution && ((framesize/i)%8)==0)) ) {
            unsigned int tst_rd_sz = framesize/i;

            if( tst_rd_sz<compress_offset )
                break;
            unsigned int tst_wr_sz = compressed_size(tst_rd_sz-compress_offset, solution) + compress_offset;

            // the tst_wr_sz should better be < tst_rd_sz (if compression!)
            // otherwise there'd be no gain by compressing the data. in that
            // case it's better to come up with an error than to suggest
            // working
            if( (proto_overhead + app_overhead + tst_wr_sz) < mtu &&
                (!solution || (solution && tst_wr_sz<tst_rd_sz)) ) {
                // setting rd_size to anything other than 'unconstrained'
                // makes the outer loop terminate
                rd_size = tst_rd_sz;
                wr_size = tst_wr_sz;
            }
        }
    }
    if( rd_size==constraints::unconstrained ) {
        std::ostringstream o;
        o << "failed to find a suitable solution for " << in;
        throw constraints::constraint_error(o.str());
    }
    // fix up the value of blocksize, depending on wether it was set or not
    // and if so to which value. fact of the matter is that we want an
    // integral number of frames to fit into one block or an integral number
    // of blocks into one frame. either is OK.
    if( blocksize==constraints::unconstrained )
        blocksize=framesize;
    else if( blocksize>framesize )
        blocksize -= (blocksize%framesize);
    else if( blocksize>rd_size ) {
        // make blocksize such that rd_size divides into blocksize and
        // blocksize into framesize
        // start by truncating it to a multiple of rd_size
        blocksize -= (blocksize%rd_size);
        // now go on finding a nice multiple
        while( blocksize/rd_size>1 && (blocksize%rd_size)!=0 && (framesize%blocksize)!=0 )
            blocksize -= rd_size;
    }
    else
        throw constraints::constraint_error("blocksize too small");

    constraintset_type   rv( in );
    rv[constraints::blocksize]  = blocksize;
    rv[constraints::read_size]  = rd_size;
    rv[constraints::write_size] = wr_size;
    return rv;
}

