// chunkmaker steps translate blocks into chunk_type so they can be striped
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
#ifndef JIVE5A_THREADFNS_CHUNKMAKERS_H
#define JIVE5A_THREADFNS_CHUNKMAKERS_H

#include <map>
#include <string>
#include <limits>

#include <chain.h>
#include <block.h>
#include <threadfns.h>
#include <threadfns/chunk.h>
//#include <threadfns/per_sender.h>
//#include <threadfns/do_push_block.h>

//#include <list>
#include <map>
#include <string>
#include <utility>
#include <sstream>
//#include <iostream>


//////////////////////////////////////////////////////////
/// transparently support tagged/untagged blocks
//////////////////////////////////////////////////////////
inline block strip_tag(block const& b) {
    return b;
}
inline block strip_tag(tagged<block> const& b) {
    return b.item;
}

inline off_t block_size(block const& b) {
    return b.iov_len;
}
inline off_t block_size(tagged<block> const& b) {
    return b.item.iov_len;
}

// untagged blocks get invalid stream id
// (but can be used in the code below Just Fine)
inline unsigned int get_tag(block const&) {
    return std::numeric_limits<unsigned int>::max();
}
inline unsigned int get_tag(tagged<block> const& b) {
    return b.tag;
}

///////////////////////////////////////////////////////////////////
//          chunkmakerargs_type
///////////////////////////////////////////////////////////////////
struct chunkmakerargs_type {

    runtime*    rteptr;
    std::string recording_name;

    // asserts that rte != null && rec != empty 
    chunkmakerargs_type(runtime* rte, std::string const& rec);

    private:
#if __cplusplus >= 201103L
        chunkmakerargs_type() = delete;
#else
        // no default c'tor
        // [declared but not defined - we're not in C11 yet :-(]
        chunkmakerargs_type();
#endif
};

// There will be several types of chunkmakers:
// - tagged
//     - based on datastreams
//          - mk6
//          - vbs 
//     - based on net_port
//          - mk6
//          - vbs
// - non-tagged blocks
//     - mk6
//     - vbs
//
// For tags there's two source:
// - rte.mk6info.datastreams
// - rte.netparms

// Need to keep track of block sequence number per tag
//      but also the actual file name per tag so they only
//      need to be computed once
struct bsn_entry {
    uint32_t          blockSeqNr;
    std::string const baseName;

    bsn_entry(std::string const& basename, uint32_t n=0);

    private:
#if __cplusplus >= 201103L
        bsn_entry() = delete;
#else
        // declare, but do not define
        bsn_entry();
#endif
};
typedef std::map<unsigned int, bsn_entry> tag2bsn_type;


//////////////////////////////////////////////////////////
/// transparently support tagged/untagged blocks
//////////////////////////////////////////////////////////

struct NoSuffix {
    NoSuffix( chunkmakerargs_type const& );

    std::string operator()( unsigned int ) const;
};

struct DataStreamSuffix {
    DataStreamSuffix( chunkmakerargs_type const& cma );

    std::string operator()( unsigned int t) const;

    datastream_mgmt_type const& __m_datastreams;
};

struct NetPortSuffix {
    NetPortSuffix( chunkmakerargs_type const& cma );

    std::string operator()( unsigned int t) const;

    netparms_type const& __m_netparms;
};


#if 0
// generic template don't do nothing
template <typename Item, template TagSource>
struct SequenceNumberCounter {};

// Untagged blocks = just count
template <typename SuffixSource>
struct SequenceNumberCounter<block, SuffixSource> {

    SequenceNumberCounter<block, SuffixSource>( chunkmakerargs_type const& ) :
        __m_bsn_count( 0 )
    {}

    // no intelligence just count them blocks
    bsn_entry operator()(block const&) {
        return bsn_entry("", __m_bsn_count++);
    }

    uint32_t          __m_bsn_count;
};

template <typename SuffixSource>
struct SequenceNumberCounter< tagged<block>, SuffixSource> {

    SequenceNumberCounter< tagged<block>, SuffixSource >( chunkmakerargs_type const& cma ):
        __m_suffix_source( cma )
    {}

    // Use block's tag to look up suffix + block sequence number
    bsn_entry operator()(tagged<block> const& b) {
        tag2bsn_type::iterator cur = __m_bsn_map.find( b.tag );
        if( cur==__m_bsn_map.end() ) {
            std::string const                       suffix( __m_suffix_source(b.tag) );
            EZASSERT2( !suffix.empty(), std::runtime_error, 
                       EZINFO("Empty suffix defined for tag/stream#" << b.tag) );
            std::pair<bsnmap_type::iterator, bool>  insres = __m_bsn_map.insert( bsnmap_type::value_type("_ds"+suffix) );

            EZASSERT2( insres.second, std::runtime_error, 
                       EZINFO("Failed to add entry to data stream tag to Block Sequence Number counter?!") );
            cur = insres.first;
        }
        return bsn_entry(cur->second.baseName, cur->second.blockSeqNr++);
    }

    tag2bsn_type __m_bsn_map;
    SuffixSource __m_suffix_source;
};
#endif


template <typename SuffixSource>
struct VBSNamer {

    VBSNamer( chunkmakerargs_type const& cma ):
        __m_suffix_source( cma ), __m_recname( cma.recording_name )
    {}

    // VBS naming scheme is:
    // <recname>[_dsSUFFIX]/<recname>[_dsSUFFIX].XXXXXXXX
    template <typename Item>
    filemetadata operator()(Item const& item) {
        unsigned int const     tag( get_tag(item) );
        std::ostringstream     bsn_s;
        tag2bsn_type::iterator curBSN = __m_bsn_map.find( tag );

        if( curBSN==__m_bsn_map.end() ) {
            // Tag not in cache yet. Ask suffix_source for suffix
            std::string const                       suffix( __m_suffix_source(tag) );
            std::ostringstream                      oss;
            std::pair<tag2bsn_type::iterator, bool> insres;

            // Now we can form the VBS basename of chunks for this stream
            // Note: bsn_entry's .baseName attribute is "mis"used here to mean the suffix
            oss << __m_recname << suffix << "/" << __m_recname << suffix << ".";
            insres = __m_bsn_map.insert( tag2bsn_type::value_type(tag, oss.str()) );
            EZASSERT2( insres.second, std::runtime_error, 
                       EZINFO("Failed to add entry to data stream tag to Block Sequence Number counter?!") );
            curBSN = insres.first;
        }

        // Now we can form the proper block name
        bsn_s << curBSN->second.baseName << format("%08u", curBSN->second.blockSeqNr);
        return maybe_print(filemetadata(bsn_s.str(), (off_t)block_size(item), curBSN->second.blockSeqNr++));
    }

    tag2bsn_type       __m_bsn_map;
    SuffixSource       __m_suffix_source;
    std::string const  __m_recname;
};

template <typename SuffixSource>
struct MK6Namer {

    MK6Namer( chunkmakerargs_type const& cma ):
        __m_suffix_source( cma ), __m_recname( cma.recording_name )
    {}

    // Mk6 naming scheme is:
    // <recname>[_dsSUFFIX]
    template <typename Item>
    filemetadata operator()(Item const& item) {
        unsigned int const     tag( get_tag(item) );
        std::ostringstream     bsn_s;
        tag2bsn_type::iterator curBSN = __m_bsn_map.find( tag );

        if( curBSN==__m_bsn_map.end() ) {
            // Tag not in cache yet. Ask suffix_source for suffix
            std::string const                       suffix( __m_suffix_source(tag) );
            std::ostringstream                      oss;
            std::pair<tag2bsn_type::iterator, bool> insres;

            // Now we can form the VBS basename of chunks for this stream
            // Note: bsn_entry's .baseName attribute is "mis"used here to mean the suffix
            oss << __m_recname << suffix;
            insres = __m_bsn_map.insert( tag2bsn_type::value_type(tag, oss.str()) );
            EZASSERT2( insres.second, std::runtime_error, 
                       EZINFO("Failed to add entry to data stream tag to Block Sequence Number counter?!") );
            curBSN = insres.first;
        }

        // Now we can form the proper block name
        return maybe_print(filemetadata(curBSN->second.baseName, (off_t)block_size(item), curBSN->second.blockSeqNr++));
    }

    tag2bsn_type                        __m_bsn_map;
    SuffixSource                        __m_suffix_source;
    std::string const                   __m_recname;
};


template <typename Item, template<typename> class Namer, typename SuffixSource>
void chunkmakert(inq_type<Item>* inq, outq_type<chunk_type>* outq, sync_type<chunkmakerargs_type>* args) {
    Item                b;
    Namer<SuffixSource> n( *(args->userdata) );

    while( inq->pop(b) ) {
        if( outq->push(chunk_type(n(b), strip_tag(b)))==false )
            break;
        // release block immediately
        b = Item();
    }
}

#if 0
//////////////////////////////////////////////////////////
//                  chunkmaker 
// Assign chunk sequence numbers and generate the correct
// file name for the chunk based on the scan name
//////////////////////////////////////////////////////////
void chunkmaker(inq_type<block>* inq, outq_type<chunk_type>* outq, sync_type<chunkmakerargs_type>* args) {
    block           b;
    uint32_t        chunkCount = 0;
    const string&   scanName( args->userdata->recording_name );

    while( inq->pop(b) ) {
        ostringstream   fn_s;

        // Got a new block. Come up with the correct chunk name
        fn_s << scanName << "/" << scanName << "." << format("%08u", chunkCount);

        DEBUG(4, "chunkmaker: created chunk " << fn_s.str() << " (size=" << b.iov_len << ")" << endl);

        if( outq->push(chunk_type(filemetadata(fn_s.str(), (off_t)b.iov_len, chunkcount), b))==false )
            break;
        // Maybe, just *maybe* it might we wise to increment the chunk count?! FFS!
        chunkCount++;
        b = block();
    }
}

// For Mark6 the file name is just the scan name with ".mk6" appended (...)
// The multiwriter will open <mountpoint>/<fileName> and dump the chunk in there
void mk6_chunkmaker(inq_type<block>* inq, outq_type<chunk_type>* outq, sync_type<chunkmakerargs_type>* args) {
    block           b;
    uint32_t        chunkCount = 0;
    const string    fileName( args->userdata->recording_name/*+".mk6"*/ );

    while( inq->pop(b) ) {
        ostringstream   fn_s;

        if( outq->push(chunk_type(filemetadata(fileName, (off_t)b.iov_len, chunkCount), b))==false )
            break;
        chunkCount++;
        b = block();
    }
}

//////////////////////////////////////////////////////////
//                  chunkmaker 
// Assign chunk sequence numbers and generate the correct
// file name for the chunk based on the scan name
//////////////////////////////////////////////////////////

// Need to keep track of block sequence number per tag
//      but also the actual file name per tag so they only
//      need to be computed once
struct bsn_entry {
    uint32_t          blockSeqNr;
    std::string const baseName;

    bsn_entry(std::string const& basename):
        blockSeqNr( 0 ), baseName( basename )
    {}

    private:
        bsn_entry();
};
typedef std::map<unsigned int, bsn_entry> tag2bsn_type;






void chunkmaker_stream(inq_type< tagged<block> >* inq, outq_type<chunk_type>* outq, sync_type<chunkmakerargs_type>* args) {
    tag2bsn_type                tag2bsn;
    tagged<block>               b;
    const string&               scanName( args->userdata->recording_name );
    tag2bsn_type::iterator      curBSN;
    datastream_mgmt_type const& datastreams( args->userdata->rteptr->mk6info.datastreams );

    while( inq->pop(b) ) {
        // Got a new block. Come up with the correct chunk name

        // Go find the block-sequence-number state for this data stream
        curBSN = tag2bsn.find( b.tag );

        // Did we see this tag before?
        if( curBSN==tag2bsn.end() ) {
            // Need to generate new basename
            // so, based on the tag, find the data stream
            ostringstream      bn_s;
            std::string const& stream_name( datastreams.streamid2name(b.tag) );

            // As per spec, empty name == no data stream definition assigned to
            // this tag?!
            EZASSERT2( !stream_name.empty(), std::runtime_error,
                       EZINFO("No datastream name for datastream_id " << b.tag) );

            // Now we can form the proper basename
            //   <recording>_<stream_id>/<recording>_<stream_id>.
            // the per-block code will append the "XXXXXXXX" block sequence number
            bn_s << scanName << "_ds" << stream_name << "/" << scanName << "_ds" << stream_name << ".";

            // (attempt to) store in the mapping
            pair<tag2bsn_type::iterator, bool> insres = tag2bsn.insert( make_pair(b.tag, bsn_entry(bn_s.str())) );

            EZASSERT2( insres.second, std::runtime_error, 
                       EZINFO("Failed to add entry to data stream to Block Sequence Number mapping?!") );
            curBSN = insres.first;
        }

        // Format the current block-sequence-number according to vbs
        // specification
        ostringstream      bsn_s;

        bsn_s << format("%08u", curBSN->second.blockSeqNr);

        DEBUG(4, "chunkmaker_stream: created chunk " << curBSN->second.baseName << bsn_s.str() << " " <<
                 "(size=" << b.item.iov_len << " stream_id=" << b.tag << ")" << endl);

        if( outq->push(chunk_type(filemetadata(curBSN->second.baseName+bsn_s.str(), (off_t)b.item.iov_len, curBSN->second.blockSeqNr),
                       b.item))==false )
            break;
        // Maybe, just *maybe* it might we wise to increment the chunk count?! FFS!
        curBSN->second.blockSeqNr++;
        b.item = block();
    }
}

// For Mark6 the file name is just the scan name with ".mk6" appended (...)
// The multiwriter will open <mountpoint>/<fileName> and dump the chunk in there
void mk6_chunkmaker_stream(inq_type< tagged<block> >* inq, outq_type<chunk_type>* outq, sync_type<chunkmakerargs_type>* args) {
    tag2bsn_type                tag2bsn;
    tagged<block>               b;
    const string&               scanName( args->userdata->recording_name );
    tag2bsn_type::iterator      curBSN;
    datastream_mgmt_type const& datastreams( args->userdata->rteptr->mk6info.datastreams );

    while( inq->pop(b) ) {
        // Go find the block-sequence-number state for this data stream
        curBSN = tag2bsn.find( b.tag );

        // Did we see this tag before?
        if( curBSN==tag2bsn.end() ) {
            // Need to generate new basename
            // so, based on the tag, find the data stream
            ostringstream      bn_s;
            std::string const& stream_name( datastreams.streamid2name(b.tag) );

            // As per spec, empty name == no data stream definition assigned to
            // this tag?!
            EZASSERT2( !stream_name.empty(), std::runtime_error,
                       EZINFO("No datastream name for datastream_id " << b.tag) );

            // Now we can form the proper basename
            //   <recording>_<stream_id>
            // the Mk6 writer will append ".m6" or something like that
            bn_s << scanName << "_ds" << stream_name;

            // (attempt to) store in the mapping
            pair<tag2bsn_type::iterator, bool> insres = tag2bsn.insert( make_pair(b.tag, bsn_entry(bn_s.str())) );

            EZASSERT2( insres.second, std::runtime_error, 
                       EZINFO("Failed to add entry to data stream to Block Sequence Number mapping?!") );
            curBSN = insres.first;
        }

        DEBUG(4, "mk6_chunkmaker_stream: created chunk " << curBSN->second.baseName << " " <<
                 "(size=" << b.item.iov_len << " stream_id=" << b.tag << " seqnr=" << curBSN->second.blockSeqNr << ")" << endl);

        if( outq->push(chunk_type(filemetadata(curBSN->second.baseName, (off_t)b.item.iov_len, curBSN->second.blockSeqNr), b.item))==false )
            break;
        curBSN->second.blockSeqNr++;
        b.item = block();
    }
}
#endif


#endif
