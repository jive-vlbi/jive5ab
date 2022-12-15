#include <data_check.h>

#include <boyer_moore.h>
#include <dosyscall.h>
#include <evlbidebug.h>
#include <headersearch.h>
#include <stringutil.h>
#include <auto_array.h>
#include <libvbs.h>
#include <countedpointer.h>

#include <stdint.h> // for [u]int[23468]*_t
#include <map>
#include <vector>
#include <algorithm>
#include <cstdlib> // for abs
#include <cmath>
#include <set>
#include <time.h>

using namespace std;

DEFINE_EZEXCEPT(data_check_except)
DEFINE_EZEXCEPT(streamstor_reader_bounds_except)
DEFINE_EZEXCEPT(file_reader_except)
DEFINE_EZEXCEPT(vbs_reader_except)

struct vdif_header_summary {
    vdif_header_summary(struct vdif_header const &vh):
        __m_vh( vh )
    {}
    // In C++11 happyland this one would probably not be needed
    vdif_header_summary(vdif_header_summary const& o):
        __m_vh( o.__m_vh )
    {}

    struct vdif_header const&  __m_vh;
private:
    vdif_header_summary();
    vdif_header_summary& operator=(vdif_header_summary const&);
};

std::ostream& operator<<(std::ostream& os, struct vdif_header_summary const& vs) {
    return os << (1<<(int)vs.__m_vh.log2nchans) << "ch x " << ((int)vs.__m_vh.bits_per_sample)+1 << "bits/" << ((int)vs.__m_vh.complex?"complex ":"") << "sample";
}

std::ostream& operator<<(std::ostream& os, struct vdif_header const& vh) {
    return os << "VDIF[.legacy=" << (int)vh.legacy << " .invalid=" << (int)vh.invalid << " .data_frame_len8=" << (int)vh.data_frame_len8 << " .ref_epoch=" << (int)vh.ref_epoch << " .epoch_seconds=" << (int)vh.epoch_seconds << " .data_frame_num=" << (int)vh.data_frame_num << " .thread_id=" << (int)vh.thread_id << " .log2nchans=" << (int)vh.log2nchans << " .bits_per_sample=" << (int)vh.bits_per_sample << " .complex=" << (int)vh.complex << "]";
}

ostream& operator<<(ostream& os, which_frame const& which) {
    switch( which ) {
        case capture_first:
            os << "capture_first";
            break;
        case capture_last:
            os << "capture_last";
            break;
        default:
            os << "<unknown which_frame>";
            break;
    }
    return os;
}

data_check_type::data_check_type() :
    format( fmt_unknown ), trackbitrate( headersearch_type::UNKNOWN_TRACKBITRATE ),
    time(0, highrestime_type::UNKNOWN_SUBSECOND)
{}

bool data_check_type::is_partial() {
    return (trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) ||
        (time.tv_subsecond == highrestime_type::UNKNOWN_SUBSECOND);
}

std::ostream& operator<<( std::ostream& os, data_check_type const& d ) {
    bool comma( false );

    os << d.format << "x" << d.ntrack << "@" << (d.trackbitrate==headersearch_type::UNKNOWN_TRACKBITRATE ? 0 : d.trackbitrate)
                          << " V:" << d.vdif_frame_size << "x" << d.vdif_threads.size() << "thrds {";
    for( threadmap_type::const_iterator p=d.vdif_threads.begin(); p!=d.vdif_threads.end(); comma=true, p++)
        os << (comma?", ":"") << "thrd#" << p->first << "=" << vdif_header_summary(p->second);
    return os << "} => " << tm2vex( d.time ) << " " << d.byte_offset
                         << "b #" << d.frame_number;
}

// assumes data has the proper encoding (nrz-m or not)
bool check_data_format(const unsigned char* data, size_t len, unsigned int track,
                       const headersearch_type& format, bool strict, bool verbose,
                       data_check_type& data_type);
                       //unsigned int& byte_offset, highrestime_type& time, unsigned int& frame_number);

countedpointer< vector<uint32_t> > generate_nrzm(const unsigned char* data, size_t len) {
    countedpointer< vector<uint32_t> > nrzm_data (new vector<uint32_t>(len / sizeof(uint32_t)));
    uint32_t const*              data_pointer = (uint32_t const*)data;

    // first word is only valid if this is the first word of the recording
    (*nrzm_data)[0] = data_pointer[0];
    for (unsigned int i = 1; i < nrzm_data->size(); i++) {
        (*nrzm_data)[i] = data_pointer[i] ^ data_pointer[i-1];
    }
    return nrzm_data;
}

struct tvg_pattern_type {
    tvg_pattern_type() {
        uint32_t current_pattern = 0xbfff4000;
        for (unsigned int current_index = 0; current_index < pattern_size; current_index++) {
            index[current_pattern] = current_index;
            pattern[current_index] = current_pattern;
            // next
            current_pattern = next(current_pattern);
        }
    }
    static const unsigned int pattern_size = 32767;
    uint32_t pattern[pattern_size];
    typedef map<uint32_t, unsigned int> index_type;
    index_type index;

    private:
        uint32_t next(uint32_t current) {
            uint32_t ret = 0;

            // next bit i in [0 .. 13] are current bit i + 1 xor current bit i + 2
            ret = ((current >> 1) ^ (current >> 2)) & 0x3fff;

            // next bit 14 is current bit 1 ^ current bit 2 ^ current bit 15, collect in LSB
            uint32_t bit14 = ((current >> 1) ^ (current >> 2) ^ (current >> 15)) & 1;

            // next bit 15 is current bit 1 ^ current bit 3, collect in LSB
            uint32_t bit15 = ((current >> 1) ^ (current >> 3)) & 1;
            ret |= (bit14 << 14);
            ret |= (bit15 << 15);

            // bit i in [16 .. 31] are the inverse of bit i - 16
            ret |= ((~(ret << 16)) & 0xffff0000);
            return ret;
        }
};

// search data, of size len, for a number of data formats if any is found,
// return true and fill format, trackbitrate and ntrack with the parameters
// describing the found format, fill byte_offset with byte position of the
// start of the first frame and time with the time in that first frame else
// return false (reference parameters will be undefined)
// VDIF notes, if the format is reported as VDIF:
// 1) the values in result will represent the first VDIF thread found
// 2) to determine the data rate (and therefor also the trackbitrate),
//    we would need up to 1s of data, if not enough data is available,
//    the nano second field in result.time will be set to -1 and
//    result.trackbitrate will be UNKNOWN_TRACKBITRATE
//    if we find enough data, we assume that trackbitrate is 2**n * 10e6 ( n >= -6 )
// 3) the number of VDIF threads reported will only make sense if all VDIF threads are of the same "shape", otherwise it will be 0

bool find_data_format(const unsigned char* data, size_t len, unsigned int track, bool strict, bool verbose, data_check_type& result) {
    const headersearch_type formats[] = {
        headersearch_type(fmt_mark4, 8, 2000000, 0),
        headersearch_type(fmt_mark4, 8, 4000000, 0),
        headersearch_type(fmt_mark4, 8, 8000000, 0),
        headersearch_type(fmt_mark4, 8, 16000000, 0),
        headersearch_type(fmt_mark4, 16, 2000000, 0),
        headersearch_type(fmt_mark4, 16, 4000000, 0),
        headersearch_type(fmt_mark4, 16, 8000000, 0),
        headersearch_type(fmt_mark4, 16, 16000000, 0),
        headersearch_type(fmt_mark4, 32, 2000000, 0),
        headersearch_type(fmt_mark4, 32, 4000000, 0),
        headersearch_type(fmt_mark4, 32, 8000000, 0),
        headersearch_type(fmt_mark4, 32, 16000000, 0),
        headersearch_type(fmt_mark4, 64, 2000000, 0),
        headersearch_type(fmt_mark4, 64, 4000000, 0),
        headersearch_type(fmt_mark4, 64, 8000000, 0),
        headersearch_type(fmt_mark4, 64, 16000000, 0),

        headersearch_type(fmt_mark4_st, 32, 2000000, 0),
        headersearch_type(fmt_mark4_st, 32, 4000000, 0),
        headersearch_type(fmt_mark4_st, 32, 8000000, 0),
        headersearch_type(fmt_mark4_st, 32, 16000000, 0),

        headersearch_type(fmt_vlba, 8, 2000000, 0),
        headersearch_type(fmt_vlba, 8, 4000000, 0),
        headersearch_type(fmt_vlba, 8, 8000000, 0),
        headersearch_type(fmt_vlba, 16, 2000000, 0),
        headersearch_type(fmt_vlba, 16, 4000000, 0),
        headersearch_type(fmt_vlba, 16, 8000000, 0),
        headersearch_type(fmt_vlba, 32, 2000000, 0),
        headersearch_type(fmt_vlba, 32, 4000000, 0),
        headersearch_type(fmt_vlba, 32, 8000000, 0),
        headersearch_type(fmt_vlba, 64, 2000000, 0),
        headersearch_type(fmt_vlba, 64, 4000000, 0),
        headersearch_type(fmt_vlba, 64, 8000000, 0),

        headersearch_type(fmt_vlba_st, 32, 2000000, 0),
        headersearch_type(fmt_vlba_st, 32, 4000000, 0),
        headersearch_type(fmt_vlba_st, 32, 8000000, 0),

        // for mark5b we can't see the difference between more bitstreams and a higher sample rate
        // this will return the highest number of bitstream (ie, the lowest sample rate)
        headersearch_type(fmt_mark5b, 1, 2000000, 0),
        headersearch_type(fmt_mark5b, 2, 2000000, 0),
        headersearch_type(fmt_mark5b, 4, 2000000, 0),
        headersearch_type(fmt_mark5b, 8, 2000000, 0),
        headersearch_type(fmt_mark5b, 16, 2000000, 0),
        headersearch_type(fmt_mark5b, 32, 2000000, 0),
        headersearch_type(fmt_mark5b, 32, 4000000, 0),
        headersearch_type(fmt_mark5b, 32, 8000000, 0),
        headersearch_type(fmt_mark5b, 32, 16000000, 0),
        headersearch_type(fmt_mark5b, 32, 32000000, 0),
        headersearch_type(fmt_mark5b, 32, 64000000, 0)
    };

    // straight through data is encoded in NRZ-M, undo that encoding
    countedpointer< vector<uint32_t> > nrzm_data = generate_nrzm(data,len);

    for (unsigned int i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        const unsigned char* data_to_use;
        unsigned int len_of_data;
        if (formats[i].frameformat == fmt_mark4_st || formats[i].frameformat == fmt_vlba_st) {
            data_to_use = (const unsigned char*)&(*nrzm_data)[0];
            len_of_data = nrzm_data->size() * sizeof(uint32_t);
        }
        else {
            data_to_use = data;
            len_of_data = len;
        }

        if ( check_data_format(data_to_use, len_of_data, track, formats[i], strict, verbose,
                               result) ) {
                               //result.byte_offset, result.time, result.frame_number) ) {
            result.format       = formats[i].frameformat;
            result.ntrack       = formats[i].ntrack;
            result.trackbitrate = formats[i].trackbitrate;

            return true;
        }
    }

    // see if it might be VDIF
    if ( seems_like_vdif(data, len, result, capture_first) ) {
        return true;
    }

    // Mark5B as generated by RDBE and Fila10G doesn't contain subsecond information
    // try this "format" last
    if ( check_data_format(data, len, track, headersearch_type(fmt_mark5b, 32, headersearch_type::UNKNOWN_TRACKBITRATE, 0),
                           strict, verbose, result) ) {
                           //strict, result.byte_offset, result.time, result.frame_number) ) {
        result.format            = fmt_mark5b;
        result.ntrack            = 32;
        result.trackbitrate      = headersearch_type::UNKNOWN_TRACKBITRATE;
        result.time.tv_subsecond = highrestime_type::UNKNOWN_SUBSECOND;
        return true;
    }
    return false;
}

bool is_data_format(const unsigned char* data, size_t len, unsigned int track, const headersearch_type& format,
                    bool strict, bool verbose, data_check_type& data_type) {

    if (format.frameformat == fmt_mark4_st || format.frameformat == fmt_vlba_st) {
        // straight through data is encoded in NRZ-M, undo that encoding
        countedpointer< vector<uint32_t> > nrzm_data = generate_nrzm(data,len);
        return check_data_format((const unsigned char*)&(*nrzm_data)[0], nrzm_data->size() * sizeof(uint32_t),
                                 track, format, strict, verbose, data_type);
                                 //track, format, strict, data_type.byte_offset, data_type.time, data_type.frame_number);
    }
    else if ( is_vdif(format.frameformat) ) {
        bool success = seems_like_vdif(data, len, data_type, capture_last);
        // cannot guarantee that we find the same threads at start as at end
        //             (vdif_threads == data_type.vdif_threads) &&
        if ( success &&
             (format.frameformat == data_type.format) &&
             (format.ntrack == data_type.ntrack) &&
             (format.framesize == data_type.vdif_frame_size) &&
             ((format.trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) ||
              (format.trackbitrate == data_type.trackbitrate)) ) {
            //byte_offset = data_type.byte_offset;
            //time = data_type.time;
            //frame_number = data_type.frame_number;
            return true;
        }
        else {
            return false;
        }
    }
    else {
        return check_data_format(data, len, track, format, strict, verbose, data_type);
    }
}

bool might_be_dbe( const mk5b_ts& timestamp ) {
    return ((timestamp.SS0 == 0) &&
            (timestamp.SS1 == 0) &&
            (timestamp.SS2 == 0) &&
            (timestamp.SS3 == 0));
}

bool check_data_format(const unsigned char* data, size_t len, unsigned int track, const headersearch_type& format,
                       bool strict, bool verbose, data_check_type& data_type) {

    boyer_moore                syncwordsearch(format.syncword, format.syncwordsize);
    unsigned int               next_position;
    headersearch::strict_type  strict_e;

    // even without strict we still check consistency, we already have an
    // UNKNOWN_TRACKBITRATE format in combination with allow_dbe for
    // inconsistent data formats
    if( strict )
        strict_e = headersearch::strict_type(headersearch::chk_crc) | headersearch::chk_consistent |
                   headersearch::chk_strict | headersearch::chk_allow_dbe ;
    else
        strict_e = headersearch::strict_type(headersearch::chk_consistent) | headersearch::chk_allow_dbe;

    // Add verbosity if the debug level requires that
    if( dbglev_fn()>=4 && verbose )
        strict_e |= headersearch::chk_verbose;
    if( !verbose )
        strict_e |= headersearch::chk_nodebug;

    // search for first header
    const unsigned char* sync_word = syncwordsearch(data, len);

    while ( sync_word ) {
        data_type.byte_offset = (unsigned int)(sync_word - data);
        if (data_type.byte_offset < format.syncwordoffset) {
            next_position = data_type.byte_offset + format.syncwordsize;
        }
        else {
            data_type.byte_offset -= format.syncwordoffset;
            if (data_type.byte_offset + format.headersize > len) {
                // no full header
                return false;
            }
            const unsigned char* start_of_frame = data + data_type.byte_offset;
            if ( format.check(start_of_frame, strict_e, track) ) {
                try {
                    data_type.time = format.decode_timestamp(start_of_frame, strict_e, track);
                    break;
                }
                catch ( const exception& e ) {
                }
            }
            // the format check failed, try to find next sync word
            next_position = data_type.byte_offset + format.syncwordoffset + format.syncwordsize;
        }
        // If there are not enough bytes left to check for a syncword, there
        // can not be a syncword
        if (next_position + format.syncwordsize < len)
            sync_word = syncwordsearch(data + next_position, len - next_position);
        else
            sync_word = NULL;
    }
    if ( !sync_word ) {
        return false;
    }

    // fill in the frame number
    if ( format.frameformat == fmt_mark5b ) {
        const m5b_header& header = *(const m5b_header*)( data + data_type.byte_offset );
        data_type.frame_number = header.frameno;
        data_type.tvg_flag     = header.tvg;
    }
    if (format.trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) {
        // no point in searching for the next frame if we don't care about the bitrate
        data_type.time.tv_subsecond = highrestime_type::UNKNOWN_SUBSECOND;
        // only fill in in case of success
        data_type.format       = format.frameformat;
        data_type.ntrack       = format.ntrack;
        data_type.trackbitrate = format.trackbitrate;
        return true;
    }

    // we found a first header (at byte position, search for next header to verify data rate
    unsigned int            frame_inc        = 1;
    unsigned int            max_frame_number = data_type.frame_number;
    const samplerate_type   datarate_fmt     = format.ntrack * format.trackbitrate; // x bits per y seconds

    // some "Mark5B data" produced in the field doesn't fill in the VLBA
    // timestamp field.  this format is so common by now that we do need to
    // detect it. The only way to detect which data rate this type of data
    // is, is to see a reset of the frame number and figure it out from the
    // max frame number seen. initialize variable to prevent warnings
    //bool         data_might_be_dbe = false;
    data_type.dbe_flag = false;

    if ( format.frameformat == fmt_mark5b ) {
        if ( datarate_fmt >= 1000000000 ) {
            // for Mark5B data at 1Gbps or bigger the time decoder might deceive us,
            // because the time resolution of the VLBA subsecond time field is too small
            // 3 frames should be enough to distinguish between 1 and 2 Gbps
            frame_inc = 3;
        }
        const mk5b_ts& timestamp = *(const mk5b_ts*)( data + data_type.byte_offset + sizeof(m5b_header) );
        data_type.dbe_flag       = might_be_dbe(timestamp);
    }

    do {
        unsigned int         next_frame = data_type.byte_offset + format.framesize * frame_inc;
        const unsigned char* frame_pointer = data + next_frame;

        // If header of next frame extends outside buffer ... we're done
        if ( next_frame + format.headersize >= len )
            break;

        // Look at frame
        try {
            if ( !strict || format.check(frame_pointer, strict_e, track) ) {
                const highrestime_type  next_time = format.decode_timestamp(frame_pointer, strict_e, track);
                // This is the delta wall clock time in the data
                const highresdelta_type dt_data = next_time - data_type.time;
                // now compute, based on format's data rate, what the elapsed time
                // would be for the current format
                const samplerate_type   dt_fmt  = (frame_inc * format.payloadsize * 8) / datarate_fmt;
                const highresdelta_type ddt_fmt = dt_fmt.as<highresdelta_type>();

                // HV: We don't have to consider the possibility that
                //     the next frame's time is equal to the current
                //     (it could be cornerturned VDIF with >1 thread; in
                //     which case you could find #-of-thread equal time stamps
                //     successively).
                //     This bit of code does not get called if it is
                //     VDIF.
                if( dt_data - ddt_fmt )
                    return false;

                // If the expected time difference and measured time
                // difference match, we're done,
                // unless it was DBE/Mark5B format, then we need to do
                // more
                if ( format.frameformat != fmt_mark5b ) {
                    // only fill in in case of success
                    data_type.format       = format.frameformat;
                    data_type.ntrack       = format.ntrack;
                    data_type.trackbitrate = format.trackbitrate;
                    return true;
                }

                // handle the possibility of DBE formatted data
                // (no timestamp filled in)
                const m5b_header& header = *(const m5b_header*)( data + next_frame );
                const mk5b_ts&    timestamp = *(const mk5b_ts*)( data + next_frame + sizeof(m5b_header) );

                // no DBE, no need to check frame numbers
                //if( !(data_might_be_dbe && might_be_dbe(timestamp)) )
                if( !(data_type.dbe_flag && might_be_dbe(timestamp)) ) {
                    // only fill in in case of success
                    data_type.format       = format.frameformat;
                    data_type.ntrack       = format.ntrack;
                    data_type.trackbitrate = format.trackbitrate;
                    return true;
                }
                // for DBE data we need to see a frame number reset to be able to verify the data rate
                if ( header.frameno < max_frame_number ) {
                    samplerate_type format_fps = format.get_state().framerate;

                    // Mark5B data rate are increased in
                    // steps of factor 2 so check whether
                    // the largest frame we've seen is
                    // larger than the data rate one step
                    // lower would allow and not bigger than
                    // we expect to see for this data rate

                    const bool r = ((max_frame_number * 2 >= format_fps) && (max_frame_number < format_fps));
                    if( r ) {
                        // only fill in in case of success
                        data_type.format       = format.frameformat;
                        data_type.ntrack       = format.ntrack;
                        data_type.trackbitrate = format.trackbitrate;
                    }
                    return r;
                }
                // otherwise, we can't decide yet if this is the format we're looking for
                max_frame_number = header.frameno;
            } // end of if format.check()
        }
        catch ( const exception& e ) {
        }
        frame_inc++;
    } while (true);

    // we didn't find a matching second header or a DBE frame number wrap
    return false;
}

// search data, of size len, for mark4/vlba tvg pattern if found, return
// true and first_valid will be the byte offset in the buffer of the first
// valid TVG byte, first_invalid will be the byte_offset of the first
// invalid tvg byte after the first valid byte else return true, first_valid
// and first_invalid will be undefined
bool is_mark5a_tvg(const unsigned char* data, size_t len, unsigned int& first_valid, unsigned int& first_invalid) {
    static const tvg_pattern_type tvg_pattern;

    const uint32_t* buffer = (const uint32_t*)data;
    const unsigned int step = sizeof(buffer[0]);

    first_valid = 0;
    tvg_pattern_type::index_type::const_iterator iter;
    while ( (first_valid + step <= len ) &&
            ((iter = tvg_pattern.index.find(buffer[first_valid / step])) ==
             tvg_pattern.index.end()) ) {
        first_valid += step;
    }
    if ( first_valid + step > len ) {
        return false;
    }
    first_invalid = first_valid + step;
    unsigned int index = iter->second;
    while ( first_invalid + step <= len ) {
        index = (index + 1) % tvg_pattern.pattern_size;
        if ( buffer[first_invalid / step] != tvg_pattern.pattern[index] ) {
            break;
        }
        first_invalid += step;
    }
    return true;
}

// search data, of size len, for streamstor test pattern (fillword) if
// found, return true and first_valid will be the byte offset in the buffer
// of the first valid fillword, first_invalid will be the byte_offset of the
// first byte not fillword after the first valid byte else return false,
// first_valid and first_invalid will be undefined
bool is_ss_test_pattern(const unsigned char* data, size_t len, unsigned int& first_valid, unsigned int& first_invalid) {
    const unsigned char fillword_bytes[] = {0x11, 0x22, 0x33, 0x44};
    const uint32_t* fillword_pointer = (const uint32_t*)&fillword_bytes[0];
    const uint32_t fillword = *fillword_pointer;
    const uint32_t* buffer = (const uint32_t*)data;
    const unsigned int step = sizeof(buffer[0]);

    first_valid = 0;
    while ( (first_valid + step <= len ) &&
            (buffer[first_valid / step] != fillword) ) {
        first_valid += step;
    }
    if ( first_valid + step > len ) {
        return false;
    }
    first_invalid = first_valid + step;
    while ( (first_invalid + step <= len) &&
            (buffer[first_invalid / step] == fillword)) {
        first_invalid += step;
    }
    return true;
}

// assuming frame1 and frame2 are from the same VDIF data stream and thread,
// return true iff the values of the headers make sense
bool same_vdif_thread_sanity_check( const vdif_header& frame1, const vdif_header& frame2 ) {
    return
        (frame1.legacy == frame2.legacy) &&
        (frame1.ref_epoch == frame2.ref_epoch) &&
        (frame1.version == frame2.version) &&
        (frame1.log2nchans == frame2.log2nchans) &&
        (frame1.data_frame_len8 == frame2.data_frame_len8) &&
        (frame1.complex == frame2.complex) &&
        (frame1.bits_per_sample == frame2.bits_per_sample) &&
        (frame1.station_id == frame2.station_id);
}

bool simple_vdif_check(const vdif_header& thread1, const vdif_header& threadN) {
    return
        (thread1.legacy == threadN.legacy) &&
        (thread1.version == threadN.version) &&
        (thread1.log2nchans == threadN.log2nchans) &&
        (thread1.data_frame_len8 == threadN.data_frame_len8) &&
        (thread1.complex == threadN.complex) &&
        (thread1.bits_per_sample == threadN.bits_per_sample);
}

// This decodes the VDIF integer time stamp only
// Necessary for cases where we don't know the VDIF frame rate (yet)
// Takes into account the epoch and thus leap seconds.
struct unknownVDIFRateDecoder_type {

    unknownVDIFRateDecoder_type() :
        __m_hst(fmt_vdif, 0, headersearch_type::UNKNOWN_TRACKBITRATE, 0)
    {}

    highrestime_type operator()(vdif_header const& vdifh) const {
        return  __m_hst.decode_timestamp((unsigned char const*)&vdifh, headersearch::strict_type());
    }

    const headersearch_type  __m_hst;
};

static const unknownVDIFRateDecoder_type unknownVDIFRateDecoder;


// Keep per VDIF thread header such that we can:
// 1.) validate VDIF stream invariants:
//      - same header size ...
// 2.) validate per VDIF thread invariants:
//      - same ref epoch
//      -  ..  #-of-channels
//      -  ..  #-of-bits-per-channel
//      -  ..  sampling
//      -  ..  frame size
//
// After gathering all threads we can check wether we're looking at a
// 'simple VDIF' stream: all threads have the same *shape* (frame size,
// #-of-channels, #-bits-per-sample)

// return a pointer to the next VDIF header in the data stream
// of the same thread as base_frame, NULL if none found
// data points to the "current" header
// will add any newly encountered VDIF threads to vdif_threads,
// but will clear vdif_thread when a new thread has a different "shape"
const vdif_header* find_next_vdif_thread_header( const unsigned char* data,
                                                 const unsigned char* data_end,
                                                 const vdif_header& base_frame,
                                                 const time_t base_time,
                                                 threadmap_type& vdif_threads) {
    const vdif_header*       next_frame( (const vdif_header*)data );
    const unsigned char*     data_pointer = data;
    highrestime_type         data_time;
    threadmap_type::iterator thrdptr;

    while ( true ) {
        // Frames can never be of length zero
        if ( next_frame->data_frame_len8 == 0 )
            return NULL;

        // next frame still in available data
        data_pointer += next_frame->data_frame_len8 * 8;
        if ( data_pointer + sizeof(vdif_header) > data_end )
            return NULL;
        next_frame = (const vdif_header*)data_pointer;

        // within a data stream all frames should be the same concerning legacy
        if ( next_frame->legacy != base_frame.legacy )
            return NULL;

        // if the time in the headers is more than a day apart, we don't believe it's VDIF
        // (although it is legal VDIF, we want to rule out that we're
        //  decoding garbage as if it was VDIF)
        // Only use the integer second value of the time stamp for this
        // comparison because we don't know the frame rate at this point
        data_time = unknownVDIFRateDecoder(*next_frame);

        if( ::labs(base_time - (time_t)data_time.tv_sec)>86400 )
            return NULL;

        // Blindly insert the current thread-id. Duplicate insertion will
        // automatically 'fail' (i.e. a new header will not overwrite a
        // previously inserted header)
        thrdptr = vdif_threads.insert( make_pair(next_frame->thread_id, *next_frame) ).first;

        // Now we can test if the per thread invariants still hold
        if ( !same_vdif_thread_sanity_check(thrdptr->second, *next_frame) ) {
            // in case we encounter differently shaped VDIF threads, we return 0 in vdif_threads
            vdif_threads.clear();
            return NULL;
        }
        if ( next_frame->thread_id == base_frame.thread_id )
            break;
    }
    return next_frame;
}

bool seems_like_vdif(const unsigned char* data, size_t len, data_check_type& result, which_frame which) {
    if ( len < sizeof(vdif_header) )
        return false;
    const unsigned char*    data_end = data + len;
    const vdif_header&      base_frame( *(const vdif_header*)data );
    const time_t            base_time = unknownVDIFRateDecoder(base_frame).tv_sec;
    threadmap_type&         vdif_threads( result.vdif_threads );

    vdif_threads.insert( make_pair(base_frame.thread_id, base_frame) );

    // find next VDIF frame of the same thread
    vdif_header const* next_frame = find_next_vdif_thread_header( data, data_end, base_frame, base_time, vdif_threads );

    if ( !next_frame )
        return false;

    // from here on we believe to have found VDIF data
    result.format = (base_frame.legacy ? (base_frame.complex ? fmt_vdif_legacy_complex : fmt_vdif_legacy) :
                                         (base_frame.complex ? fmt_vdif_complex : fmt_vdif) );
    result.ntrack =
        (1 << base_frame.log2nchans) *
        (base_frame.bits_per_sample + 1) *
        (base_frame.complex + 1);
    result.trackbitrate = headersearch_type::UNKNOWN_TRACKBITRATE;
    result.time.tv_subsecond = highrestime_type::UNKNOWN_SUBSECOND;
    result.vdif_frame_size = base_frame.data_frame_len8 * 8;
    result.vdif_data_size = result.vdif_frame_size - ::headersize(result.format, result.ntrack);
    // Need to initialize
    result.byte_offset  = 0;
    result.time.tv_sec  = base_time;
    result.frame_number = base_frame.data_frame_num;

    // now try to find sensible values for tv_nsec and trackbitrate
    // find data frames in different seconds,
    // recording the maximum data frame number,
    // then assume this maximum is close to the actual max
    uint32_t           max_data_frame_num = max( base_frame.data_frame_num, next_frame->data_frame_num );
    vdif_header const* frmPtr             = next_frame;

    while ( next_frame->epoch_seconds == base_frame.epoch_seconds ) {
        // Let frmPtr point at last-known-good frame
        frmPtr     = next_frame;
        next_frame = find_next_vdif_thread_header( (const unsigned char*)next_frame,
                                                   data_end,
                                                   base_frame,
                                                   base_time,
                                                   vdif_threads );
        // if no next frame found, we give up and
        if( !next_frame )
            break;
        max_data_frame_num = max( max_data_frame_num, next_frame->data_frame_num );
    }
    // Copy over most of what we got and time/offset values based on wether
    // we need to capture first or last frame
    if( which==capture_first )
        frmPtr = &base_frame; // put back to first frame if so requested
    result.byte_offset  = (const unsigned char*)frmPtr - (const unsigned char*)&base_frame;
    result.time.tv_sec  = unknownVDIFRateDecoder(*frmPtr).tv_sec;
    result.frame_number = frmPtr->data_frame_num;

    // Test for 'simple VDIF'
    threadmap_type::iterator    basethread = vdif_threads.begin();
    threadmap_type::iterator    nxtthread  = basethread;

    while( nxtthread!=vdif_threads.end() ) {
        if( !simple_vdif_check(basethread->second, nxtthread->second) ) {
            // not simple VDIF. Return only number of threads; no #-of-tracks
            // and unknown bitrate
            result.ntrack            = 0;
            return true;
        }
        nxtthread++;
    }

    // precompute time span between two successive frame periods.
    const uint64_t  dutsec  = (next_frame==0 ? 0 : (next_frame->epoch_seconds - base_frame.epoch_seconds));
    unsigned int    payload = (base_frame.data_frame_len8 * 8) - (base_frame.legacy ? 16 : 32);

    switch( dutsec ) {
        // If 0  we know it's VDIF but cannot compute a rate because both
        // frames are from the same UT second
        case 0:
            //result.trackbitrate      = headersearch_type::UNKNOWN_TRACKBITRATE;
            //result.time              = unknownVDIFRateDecoder(base_frame);
            break;

        // If 1  we assume it's standard VDIF (2**n * 1E6 bits per second)
        case 1:
            {
                // as max_data_frame_num is a lower bound, we round up to the nearest power of 2
                const double trackbitrate       = (max_data_frame_num + 1) * payload * 8.0 / result.ntrack / 1e6;
                const double trackbitrate_power = ::ceil( ::log(trackbitrate)/::log(2.0) );
                // if n < -6, return without sensible subsecond time/data rate
                if ( trackbitrate_power < -6.0 )
                    return true;

                result.trackbitrate = samplerate_type( (uint64_t)::round(::pow(2, trackbitrate_power) * 1e6) );
                const headersearch_type  hst(fmt_vdif, result.ntrack, result.trackbitrate, payload);
                // recompute frame time of requested frome now that we
                // 'know' what the trackbitrate is
                result.time         = hst.decode_timestamp((unsigned char const*)frmPtr, headersearch::strict_type());
            }
            break;

        // If >1 we assume it's 'VDIF2' => integer # of frames per integer # of seconds
        // and we assume we've seen the max frame number. We don't assume
        // powers of two data rate here
        default:
            {
                result.trackbitrate = samplerate_type((max_data_frame_num + 1) * payload * 8, dutsec * result.ntrack);

                const headersearch_type  hst(fmt_vdif, result.ntrack, result.trackbitrate, payload);
                result.time         = hst.decode_timestamp((unsigned char const*)frmPtr, headersearch::strict_type());
            }
            break;
    }
    return true;
}

// Based on frame rate * framenumber compute and verify subsecond value
bool copy_subsecond(const data_check_type& source, data_check_type& destination) {
    // returns false if an inconsistency is detected in the destination
    // otherwise returns true
    destination.trackbitrate = source.trackbitrate;

    const unsigned int payloadSz = ::framesize(destination.format, destination.ntrack, destination.vdif_data_size) -
                                   ::headersize(destination.format, destination.ntrack);

    subsecond_type nsec = (destination.frame_number * payloadSz * 8) / (destination.ntrack * destination.trackbitrate);
    // sanity check: if the subsecond value ends up being >1 second, it's
    // not really a subsecond value
    // HV: 3 Oct 2016 ... well, for VDIF that has integer number of frames
    //                per integer number of seconds where the latter != 1
    //                nsec can become > 1
#if 0
    if (nsec >= 1)
        return false;
#endif
    destination.time = highrestime_type(destination.time.tv_sec, nsec);
    return true;
}

// tries to combine the data check types, filling in unknown subsecond
// information in either field using the other (assuming both are in fact
// the same data type) 'byte_offset' is the amount of bytes between the read
// offset of 'first' and 'last' return true iff both first and last are
// complete (is_partial() returns false)
bool combine_data_check_results(data_check_type& first, data_check_type& last, uint64_t byte_offset) {
    if ( !first.is_partial() && !last.is_partial() ) {
        // nothing to do
        return true;
    }
    if ( (first.format != last.format) || (first.ntrack != last.ntrack) ) {
        // different formats
        return false;
    }
    if ( is_vdif(first.format) &&
         ((first.vdif_threads.size() == 0) ||  (first.vdif_frame_size != last.vdif_frame_size))
         ) {
        // 15 Sep 2022 MV: It may be that we find different threads and/or a
        //                 different number of threads at begin/end of the
        //                 recording - as long as they're the same shape
        //                 we're ok with that.
        return false;
    }
    if ( !first.is_partial() ) {
        return copy_subsecond( first, last );
    }
    if ( !last.is_partial() ) {
        return copy_subsecond( last, first );
    }
    // so both are partial, from here on assume that the trackbitrate is
    // 2^n * 1e6 ( n >= -6; trackbitrate is a natural number )
    // we can only guestimate n if 'first' and 'last' are from a different second
    if ( first.time.tv_sec >= last.time.tv_sec ) {
        return false;
    }

    // Need to create the union of thread-ids that we've found
    // We can do this blindly - if it's not VDIF the sets are empty and
    // unused, otherwise they'll be non-empty and checked before use
    // and make sure that the last.vdif_threads is the same such that it
    // shouldn't matter if the caller uses first or last
    first.vdif_threads.insert( last.vdif_threads.begin(), last.vdif_threads.end() );
    last.vdif_threads = first.vdif_threads;

    // compute the byte difference between 'first' and 'last' at their respective 0 frame within second
    const unsigned int    vdif_threads = (is_vdif(first.format) ? first.vdif_threads.size() : 1);
    const unsigned int    frameSz      = ::framesize(first.format, first.ntrack, first.vdif_data_size);
    const unsigned int    payloadSz    = frameSz - ::headersize(first.format, first.ntrack);

    int64_t byte_diff = (int64_t)byte_offset +
                        (last.byte_offset  - (int64_t)last.frame_number  * frameSz * vdif_threads) -
                        (first.byte_offset - (int64_t)first.frame_number * frameSz * vdif_threads);

    if ( byte_diff <= 0 ) {
        return false;
    }

    // trackbitrate * ntrack * dt(whole secs) = byte_diff
    const double trackbitrate_power =
        ::round( ::log( double(8 * byte_diff) / 1e6 / (first.ntrack * vdif_threads) / (last.time.tv_sec - first.time.tv_sec) ) /
                 ::log(2.0) );

    // we have extra information that we don't use at the moment
    // the track bit rate has a minimum value of max(first.frame_number, last.frame_number) * payloadSz
    // can use that as an extra constraint on better guesstimating the actual track bit rate
    const double minimum_trackbitrate_power =
        ::ceil( ::log((double(8 * payloadSz) * std::max(first.frame_number, last.frame_number)) / 1e6 / first.ntrack) / ::log(2.0) );

    // TODO: add extra check: when checking VDIF and we haven't seen all
    //       threads (e.g. only 3 out of 4) we could figure out if an
    //       integer number of threads is a better match to the 2^n data rate than scaling by 2^m

    // if n < -6, give up
    if ( trackbitrate_power < -6.0 )
        return false;

    first.trackbitrate = samplerate_type((uint64_t)::round( ::pow(2, std::max(trackbitrate_power, minimum_trackbitrate_power)) * 1e6));
    subsecond_type nsec = (first.frame_number * payloadSz * 8) / (first.ntrack * first.trackbitrate);

    // If the subsecond value ends up being >1 it's not really a subsecond
    // HV: 09 Oct 2016 - Weeeellllll ... if we have VDIF that is integer
    //                   frames per integer number of seconds where the
    //                   latter != 1, nsec could be >1
    //                   Must create highrestime type in order to give it
    //                   the possibility to normalize
#if 0
    if (nsec >= 1)
        return false;
#endif
    first.time = highrestime_type(first.time.tv_sec, nsec);

    // filled in first, copy to last
    return copy_subsecond( first, last );
}

file_reader_type::file_reader_type( const string& filename ) {
    file.exceptions( ifstream::goodbit | ifstream::failbit | ifstream::badbit );
    try {
        file.open(filename.c_str(), ios::in|ios::binary );
    }
    catch (...) {
        THROW_EZEXCEPT(file_reader_except, "Failed to open file '" << filename << "' for reading");
    }
    const streampos begin = file.tellg();
    file.seekg( 0, ios::end );
    const streampos end = file.tellg();
    file_length = (int64_t)( end - begin );
}

uint64_t file_reader_type::read_into( unsigned char* buffer, uint64_t offset, uint64_t len ) {
    file.seekg( offset, ios::beg );
    file.read( (char*)buffer, len );
    return offset;
}

int64_t file_reader_type::length() const {
    return file_length;
}

streamstor_reader_type::streamstor_reader_type( SSHANDLE h, const playpointer& s, const playpointer& e ) :
    sshandle(h), start(s), end(e)
{
    // make sure SS is ready for reading
    XLRCALL( ::XLRSetMode(sshandle, SS_MODE_SINGLE_CHANNEL) );
    XLRCALL( ::XLRClearChannels(sshandle) );
    XLRCALL( ::XLRBindOutputChannel(sshandle, 0) );
    XLRCALL( ::XLRSelectChannel(sshandle, 0) );
}

uint64_t streamstor_reader_type::read_into( unsigned char* XLRCODE(buffer), uint64_t offset, uint64_t len ) {
    XLRCODE( S_READDESC readdesc; );
    playpointer read_pointer( start );
    read_pointer += offset;
    if ( end - read_pointer < (int64_t)len ) {
        THROW_EZEXCEPT(streamstor_reader_bounds_except, "Read request goes " << (len - (end - read_pointer)) << " beyond bounds");
    }
    XLRCODE(
            readdesc.XferLength = len;
            readdesc.AddrHi     = read_pointer.AddrHi;
            readdesc.AddrLo     = read_pointer.AddrLo;
            readdesc.BufferAddr = (READTYPE*)buffer;
            );
    XLRCALL( ::XLRRead(sshandle, &readdesc) );
    return read_pointer - start;
}

int64_t streamstor_reader_type::length() const {
    return end - start;
}

std::ostream& operator<<(std::ostream& os, const vbs_reader_base::try_format& fmt) {
    switch( fmt ) {
        case vbs_reader_base::try_both:
            return os << "Mk6/VBS";
        case vbs_reader_base::try_mk6:
            return os << "Mk6";
        case vbs_reader_base::try_vbs:
            return os << "VBS";
        case vbs_reader_base::try_none:
            return os << "NONE";
        default:
            throw vbs_reader_except("operator<<(): unrecognized try_format enum");
            break;
    }
    // keep compilert happy ...
    return os;
}

vbs_reader_base::vbs_reader_base( string const& recname, mountpointlist_type const& mps,
                                  off_t s, off_t e, try_format f ):
    fd( -1 ), start( s ), end( e )
{
    // Initialize libvbs
    // To that effect we must transform the mountpoint list into an array of
    // char*
    auto_array<char const*>             vbsdirs( new char const*[ mps.size()+1 ] );
    mountpointlist_type::const_iterator curmp = mps.begin();

    // Get array of "char*" and add a terminating 0 pointer
    for(unsigned int i=0; i<mps.size(); i++, curmp++)
        vbsdirs[i] = curmp->c_str();
    vbsdirs[ mps.size() ] = 0;

    // Now we can (try to) open the recording and get the length by seeking
    // to the end. Do not forget to put file pointer back at start doofus!
    int        fd1 = (f==try_both || f==try_mk6) ? ::mk6_open(recname.c_str(), &vbsdirs[0]) : -1;
    int        fd2 = (f==try_both || f==try_vbs) ? ::vbs_open(recname.c_str(), &vbsdirs[0]) : -1;
    const bool fd1ok( fd1>=0 ), fd2ok( fd2>=0 );

    // Exactly one of those fd's should be non-negative
    if( fd1ok==fd2ok ) {
        ostringstream oss;
        // Either neither or both exist, neither of which is a sign of Good
        if( fd1ok ) {
            ::vbs_close( fd1 );
            ::vbs_close( fd2 );
            oss << "'" << recname << "' exists in both " << try_both << " formats";
        } else {
            oss << "'" << recname << "' does not exist in " << f << " format";
        }
        // only throw up in case this is desirable
        if( f!=try_none )
            throw vbs_reader_except(oss.str());
    }

    // Pick the file descriptor that succesfully opened
    fd = fd1ok ? fd1 : fd2;

    // If end left at default, insert current recording length
    // we do not reset the file pointer to start of file because
    // on each 'read_into()' the file pointer will be repositioned
    if( end==0 )
        end = (int64_t)::vbs_lseek(fd, 0, SEEK_END);
}

uint64_t vbs_reader_base::read_into( unsigned char* buffer, uint64_t offset, uint64_t len) {
    off_t   off = start + (off_t)offset;
    ::vbs_lseek(fd, off, SEEK_SET);
    ::vbs_read(fd, buffer, (size_t)len);
    return offset;
}

int64_t vbs_reader_base::length() const {
    return (int64_t)end - (int64_t)start;
}

vbs_reader_base::~vbs_reader_base() {
    ::vbs_close( fd );
}

vbs_reader_type::vbs_reader_type( string const& recname, mountpointlist_type const& mps, off_t s, off_t e):
    vbs_reader_base(recname, mps, s, e, vbs_reader_base::try_vbs)
{}

mk6_reader_type::mk6_reader_type( string const& recname, mountpointlist_type const& mps, off_t s, off_t e):
    vbs_reader_base(recname, mps, s, e, vbs_reader_base::try_mk6)
{}

null_reader_type::null_reader_type( void ):
    vbs_reader_base("null", mountpointlist_type(), 0, std::numeric_limits<int64_t>::max(), vbs_reader_base::try_none)
{}

uint64_t null_reader_type::read_into( unsigned char* buffer, uint64_t offset, uint64_t len) {
    ::memset(buffer, 0x0, len);
    return offset;
}
