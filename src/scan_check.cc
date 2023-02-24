// Provide generic scan_check function
// Copyright (C) 2007-2022 Marjolein Verkouter
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
// Author:  Marjolein Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#include <scan_check.h>
#include <stringutil.h>
#include <mk5command/mk5.h>  // for XLR_Buffer
#include <vector>
#include <algorithm>

DEFINE_EZEXCEPT(scan_check_except)

// See comment in scan_check_type.h ...
const int64_t  scan_check_type::UNKNOWN_MISSING_BYTES = std::numeric_limits<int64_t>::max();
const uint64_t scan_check_type::UNKNOWN_BYTE_OFFSET   = std::numeric_limits<uint64_t>::max();

// c++ doesn't like "local type used as template argument" = rather preferred to
// have this one defined inline below in the context where it be used.
// We not in c++11 happyland yet; otherwise a lambda would be THE BOMB!
struct _lt_frame_nr {
    bool operator()(data_check_type const& l, data_check_type const& r) const {
        return l.frame_number < r.frame_number;
    }
};


vsi_format::vsi_fmt_flags operator|(vsi_format::vsi_fmt_flags const& l, vsi_format::vsi_fmt_flags const& r) {
    return static_cast<vsi_format::vsi_fmt_flags>( static_cast<unsigned int>(l) | static_cast<unsigned int>(r) );
}
vsi_format::vsi_fmt_flags& operator|=(vsi_format::vsi_fmt_flags& l, vsi_format::vsi_fmt_flags const& r) {
    reinterpret_cast<unsigned int&>(l) |= static_cast<unsigned int>(r);
    return l;
}
vsi_format::vsi_fmt_flags operator&(vsi_format::vsi_fmt_flags const& l, vsi_format::vsi_fmt_flags const& r) {
    return static_cast<vsi_format::vsi_fmt_flags>( static_cast<unsigned int>(l) & static_cast<unsigned int>(r) );
}

vsi_format::vsi_format(scan_check_type const& sct, vsi_fmt_flags f):
    __m_sct_ref( sct ), __m_format_flags( f )
{}


std::ostream& operator<<(std::ostream& os, vsi_format const& vsif) {
    scan_check_type const&  sct( vsif.__m_sct_ref );

    if( sct ) {
        // we did recognize the format so we can show what we found.
        // May need to change the actual detected format to a slightly different
        // display format (e.g. only insert extra TVG format info if indicated)
        format_type display_type( sct.format );

        if( (sct.format==fmt_mark5a_tvg || (is_mark5b(sct.format) && sct.mark5b.tvg))
            && !(vsif.__m_format_flags&vsi_format::VSI_S_EXPANDTVG) ) {
                display_type = fmt_tvg;
        }
        os << " : " << display_type;

        // handle test pattern format reply different
        if( is_test_pattern(sct.format) ) {
            os << " : " << sct.test_pattern.first_valid << " : " << sct.test_pattern.first_invalid << " ;";
        } else {
            // Known data type; for VDIF we multiply the number of tracks
            // (which is per thread) by the number of threads
            os << " : " << sct.ntrack * (is_vdif(sct.format) ? sct.vdif.threads.size() : 1)
                  << " : " << tm2vex(sct.start_time);

            // duration with .**** if we didn't know the actual data rate
            // and trackbitrate in Mbps - if known
            if( sct.trackbitrate!=headersearch_type::UNKNOWN_TRACKBITRATE ) {
                os << " : " << boost::rational_cast<double>(sct.end_time - sct.start_time) << "s"
                      << " : " << boost::rational_cast<double>(sct.trackbitrate / 1000000) *
                                  ((vsif.__m_format_flags&vsi_format::VSI_S_TOTALDATARATE) ?
                                   (sct.ntrack * (is_vdif(sct.format) ? sct.vdif.threads.size() : 1)) :
                                   1)
                      << "Mbps";
            } else {
                os << " : " << (sct.end_time.tv_sec - sct.start_time.tv_sec) << ".****s"
                      << " : ?";
            }

            // missing bytes
            if( sct.missing_bytes!=scan_check_type::UNKNOWN_MISSING_BYTES )
                os << " : " << sct.missing_bytes;
            else
                os << " : ?";

            // For VDIF, append the found data array length
            if ( is_vdif(sct.format) ) {
                if( sct.vdif.frame_size>0 )
                    os << " : " << sct.vdif.frame_size;
                else
                    os << " : ?";
            }
            // if we saw mark5b from a DBE, add that info
            if( is_mark5b(sct.format) && sct.mark5b.dbe )
                os << " : likely DBE source";
            os << " ;";
        }
    } else {
        // No *idea* what we be looking at
        os << " : ? ;";
    }
    return os;
}


std::ostream& operator<<(std::ostream& os, scan_check_type const& sct) {
    os << sct.format;
    if( sct ) {
        // the start/end time
        os << " from " << tm2vex(sct.start_time) << " to " << tm2vex(sct.end_time);
        // was a format!
        os << " ntrack=" << sct.ntrack;
        os << " bitrate/track=";
        if( sct.trackbitrate==headersearch_type::UNKNOWN_TRACKBITRATE )
            os << "***Mbps";
        else
            os << boost::rational_cast<double>(sct.trackbitrate / 1000000) << "Mbps";

        if( is_vdif(sct.format) ) {
            ostream_prefix_inserter<std::string, std::ostream>  thread_disp(os, ", ");
            os << " framesize=" << sct.vdif.frame_size << " dataarray=" << sct.vdif.data_size;
            os << " threads={";
            std::copy(sct.vdif.threads.begin(), sct.vdif.threads.end(), thread_disp);
            os << "}";
        }
        if( is_test_pattern(sct.format) ) {
            os << " first valid=" << sct.test_pattern.first_valid
               << " first invalid=" << sct.test_pattern.first_invalid;
        }
        os << " missing_bytes=";
        if( sct.missing_bytes==scan_check_type::UNKNOWN_MISSING_BYTES )
            os << "***";
        else
            os << sct.missing_bytes;
    }
    return os;
}

scan_check_type::_m_vdif_t::_m_vdif_t() :
    frame_size( 0 ), data_size( 0 ), threads()
{}
scan_check_type::_m_test_pattern_t::_m_test_pattern_t() :
    first_valid( static_cast<unsigned int>(-1) ), first_invalid( static_cast<unsigned int>(-1) )
{}
scan_check_type::_m_mark5b_t::_m_mark5b_t() :
    tvg( false ), dbe( false )
{}

scan_check_type::scan_check_type() :
    format( fmt_none ), ntrack( 0 ), trackbitrate( headersearch_type::UNKNOWN_TRACKBITRATE ),
    start_byte_offset( UNKNOWN_BYTE_OFFSET ), end_byte_offset( UNKNOWN_BYTE_OFFSET ),
    missing_bytes( UNKNOWN_MISSING_BYTES )
{}

scan_check_type::operator bool( void ) const {
    return format != fmt_none;
}

bool scan_check_type::complete( void ) const {
    return !((start_time.tv_subsecond == highrestime_type::UNKNOWN_SUBSECOND) ||
             (end_time.tv_subsecond   == highrestime_type::UNKNOWN_SUBSECOND) );
}


scan_check_type scan_check_fn(countedpointer<data_reader_type> data_reader, uint64_t bytes_to_read,
                              bool strict, bool verbose, unsigned int track)
{
    // What do we need ...
    int64_t const                fSize( data_reader->length() );
    unsigned int                 nSample( static_cast<unsigned int>(-1) ); // be sure to SIGSEGV...
    scan_check_type              rv;
    countedpointer<XLR_Buffer>   buffer(new XLR_Buffer(bytes_to_read));
    std::vector<data_check_type> checklist( scan_check_type::maxSample );
    data_check_type&             first( checklist[0] );

    // We cannot determine read_inc until we've peeked at the data
    // If we believe we're looking at VDIF, the read_inc must be an integer
    // multiple of the frame size
    //
    // Note: we only start searching multiple positions within the file IF
    // we believe we're looking at VDIF; for any of the tape based formats
    // (VLBA, MkIV) or Mark5B it's enough to peek at start & end.

    // 1.) Read data at start and see what we can make of it.
    //     Nothing recognizable is not an option, really - well, that is,
    //     find_data_format() does not look for mark5a_tvg nor ss_test_pattern.
    data_reader->read_into( (unsigned char*)buffer->data, 0, bytes_to_read );
    const bool found_a_format = find_data_format((unsigned char*)buffer->data, bytes_to_read, track, strict, verbose, checklist[0]);
    const bool vdif = is_vdif( checklist[0].format );

    DEBUG(4, "scan_check[1/*] = " << checklist[0] << std::endl);
    // Do our math on how often and where to sample the rest of the
    // recording
    if( vdif ) {
        uint64_t  read_inc;
        // If reading a moderate amount of bytes that is a (very) small
        // fraction of the total data size, increase the number of samplings
        // to > 2. Round off to integer number of VDIF frames
        bytes_to_read = (bytes_to_read / first.vdif_frame_size) * first.vdif_frame_size;
        nSample       = ((bytes_to_read<5*MB) && (fSize>100*static_cast<int64_t>(bytes_to_read))) ? scan_check_type::maxSample : 2;
        read_inc      = ((fSize-bytes_to_read) / (nSample-1) / first.vdif_frame_size) * first.vdif_frame_size;
        // precompute the byte-offsets where to read each sample from.
        // we make sure the last sample read extends to end-of-file
        for( unsigned int s=1; s<nSample-1; s++)
            checklist[s].byte_offset = checklist[s-1].byte_offset + read_inc;
        checklist[nSample-1].byte_offset = fSize - bytes_to_read;
    } else if( found_a_format ) {
        // Skip to end of file immediately
        nSample      = 2;
        checklist[1].byte_offset = fSize - bytes_to_read;
    } else {
        // no format recognized?
        // will test mark5a_tvg/ss_test later;
        nSample  = 1;
    }

    // Convenience reference + actual header format to look for
    data_check_type&  last( checklist[nSample-1] );
    headersearch_type found_format( first.format, first.ntrack,
                                    vdif ? headersearch_type::UNKNOWN_TRACKBITRATE : first.trackbitrate,
                                    vdif ? first.vdif_frame_size - headersize(first.format, 1): 0 );

    for( unsigned int s=1; s<nSample; s++) {
        // save pre-computed byte offset where to read this sample from
        uint64_t const   read_offset( checklist[s].byte_offset );

        // Read in next hump of data
        data_reader->read_into( (unsigned char*)buffer->data, read_offset, bytes_to_read );
        DEBUG(4, "scan_check[" << s+1 << "/" << nSample << "] read " << bytes_to_read << "b from offset = " << read_offset << "b" << " (" << fSize-read_offset << " to EOF)" << std::endl);

        // And check we find the same stuff as before
        // Note: this will overwrite the ".byte_offset" member of checklist[s]!
        // (which is why we saved it at the start of this loop)
        if( !is_data_format((unsigned char*)buffer->data, bytes_to_read, track, found_format, strict, verbose, checklist[s]) )
            THROW_EZEXCEPT(scan_check_except, "scan_check[" << s+1 << "/" << nSample << "] expect " << first << " at " << read_offset << " found " << checklist[s]);

        // For Mark5B, if the TVG flag is set in the first header, it must also
        // be set in later frames. The "is_data_format()" does not /check/ this
        // but returns the flag in the data_check_type given to it.
        if( found_format.frameformat==fmt_mark5b ) {
            EZASSERT(first.tvg_flag==checklist[s].tvg_flag, scan_check_except);
        }

        // The "is_data_format()" reports a byte offset /within the current data
        // block/ i.e. the one that we read starting at read_offset bytes into
        // the recording
        checklist[s].byte_offset += read_offset;
        DEBUG(4, "scan_check[" << s+1 << "/" << nSample << "] = " << checklist[s] << std::endl);
    }

    // 2.) By the time we end up here need to do some analyzing
    //     Let's see if there's any non-partial decoder status;
    //     we must also accumulate as many vdif threads (if any)
    //     *before* trying to combine the results
    bool         combine_ok      = false;
    unsigned int complete_decode = nSample;

    for( unsigned int s=0; s<nSample; s++ ) {
        // need to aggregate all the VDIF threads we've found - if any -
        if( s>0 )
            first.vdif_threads.insert( checklist[s].vdif_threads.begin(), checklist[s].vdif_threads.end() );

        if( complete_decode==nSample && !checklist[s].is_partial() )
            complete_decode = s;
    }

    // If we have a complete decode we're basically there already almost
    if( complete_decode!=nSample ) {
        // propagate it to first & last immediately
        combine_ok = ( combine_data_check_results(first, checklist[complete_decode] ) &&
                       combine_data_check_results(checklist[complete_decode], last) );
    } else {
        // No full timestamp decoded, let's see if we can find one with the
        // highest frame number b/c that info also helps better constraining the
        // data rate
        std::vector<data_check_type>::iterator max_framenum_ptr = std::max_element( checklist.begin(),
                                                                                    checklist.end(),
                                                                                    _lt_frame_nr() );
        combine_ok = true;
        // Attempt to link as many frames as possible
        if( max_framenum_ptr!=checklist.begin() )
            combine_ok = combine_ok && combine_data_check_results(*checklist.begin(), *max_framenum_ptr);
        if( max_framenum_ptr!=checklist.end() )
            combine_ok = combine_ok && combine_data_check_results(*max_framenum_ptr, *checklist.rbegin());
        // Final combination
        combine_ok = combine_ok && combine_data_check_results(first, last);
    }

    if( first.format!=fmt_unknown ) {
        rv.format            = first.format;
        rv.ntrack            = first.ntrack;
        rv.start_time        = first.time;
        rv.end_time          = last.time;
        rv.start_byte_offset = first.byte_offset;
        rv.end_byte_offset   = last.byte_offset;

        if( vdif ) {
            rv.vdif.frame_size = first.vdif_frame_size;
            rv.vdif.data_size  = first.vdif_data_size;
            // At this point we translate from threadmap => threadset;
            // having verified VDIF is simple enough so only number of
            // threads is inneresting for the higher level up
            for( threadmap_type::const_iterator p=first.vdif_threads.begin(); p!=first.vdif_threads.end(); p++)
                rv.vdif.threads.insert( p->first );
        }
        if( is_test_pattern(first.format) ) {
            rv.test_pattern.first_valid   = first.frame_number;
            rv.test_pattern.first_invalid = last.frame_number;
        }
        if( first.format==fmt_mark5b ) {
            rv.mark5b.tvg = first.tvg_flag;
            rv.mark5b.dbe = first.dbe_flag;
        }

        if( combine_ok ) {
            // can compute missing bytes
            rv.trackbitrate = first.trackbitrate;

            // compute missing bytes
            unsigned int const      vdif_threads = (vdif ? first.vdif_threads.size() : 1);
            // track frame period should be per vdif thread!
            samplerate_type const   track_frame_period  = (found_format.payloadsize * 8) / (first.ntrack * first.trackbitrate);
            // last time stamp is at _start_ of frame; file size is including that last frame (at least that's not unreasonable to assume)
            highresdelta_type const time_diff           = last.time - first.time + track_frame_period.as<highresdelta_type>();
            int64_t const           expected_bytes_diff = boost::rational_cast<int64_t>(
                                             (time_diff * found_format.framesize * vdif_threads)/
                                              track_frame_period.as<highresdelta_type>() );

            // Likewise: the last byte offset was at _start_ of the frame;
            // we assume that last frame is actually written in full in the file so those bytes should be taken into account as well
            rv.missing_bytes = -((int64_t)last.byte_offset + found_format.framesize - (int64_t)first.byte_offset - expected_bytes_diff);
        } else {
            // Set invalid trackbitrate to also indicate if we know exactly what we
            // be looking at
            rv.trackbitrate  = headersearch_type::UNKNOWN_TRACKBITRATE;
            rv.missing_bytes = scan_check_type::UNKNOWN_MISSING_BYTES;
        }
        return rv;
    }

    // Now what
    // end up here if no data format found at all
    if( is_mark5a_tvg((unsigned char*)buffer->data, bytes_to_read, rv.test_pattern.first_valid, rv.test_pattern.first_invalid) ) {
        rv.format = fmt_mark5a_tvg;
    } else if( is_ss_test_pattern((unsigned char*)buffer->data, bytes_to_read, rv.test_pattern.first_valid, rv.test_pattern.first_invalid) ) {
        rv.format = fmt_ss_test_pattern;
    }
    // could still be fmt_unknown ... let caller handle this
    return rv;
}
