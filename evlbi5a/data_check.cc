#include <data_check.h>

#include <boyer_moore.h>
#include <dosyscall.h>
#include <evlbidebug.h>

#include <stdint.h> // for [u]int[23468]*_t
#include <map>
#include <vector>
#include <memory>

using namespace std;

int64_t ns_diff(const timespec& start, const timespec& end) {
    const int64_t ns_p_sec = 1000000000;
    return ((int64_t)end.tv_sec - (int64_t)start.tv_sec) * ns_p_sec + 
        (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
}

unsigned int max_time_error_ns(format_type data_format) {
    switch( data_format ) {
    case fmt_mark5b:
        return 5000;
    case fmt_mark4:
        return 500000;
    case fmt_vlba:
        return 50000;
    case fmt_mark4_st:
        return 500000;
    case fmt_vlba_st:
        return 50000;
    default:
        break;
    }
    ASSERT2_COND(false, SCINFO("invalid dataformat '" << data_format << "'" << endl));
    return 0;
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

// search data, of size len, for a number of data formats
// if any is found, return true and fill format, trackbitrate and ntrack with the parameters describing the found format, fill byte_offset with byte position of the start of the first frame and time with the time in that first frame
// else return false (reference parameters will be undefined)
bool find_data_format(const unsigned char* data, size_t len, unsigned int track, bool strict, data_check_type& result) {
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
    auto_ptr< vector<uint32_t> > nrzm_data (new vector<uint32_t>(len / sizeof(uint32_t)));
    const uint32_t* data_pointer = (uint32_t*)data;

    // first word is only valid if this is the first word of the recording
    (*nrzm_data)[0] = data_pointer[0];
    for (unsigned int i = 1; i < nrzm_data->size(); i++) {
        (*nrzm_data)[i] = data_pointer[i] ^ data_pointer[i-1];
    }

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
        if ( is_data_format(data_to_use, len_of_data, track, formats[i], strict, result.byte_offset, result.time) ) {
            result.format = formats[i].frameformat;
            result.ntrack = formats[i].ntrack;
            result.trackbitrate = formats[i].trackbitrate;
            
            return true;
        }
    }

    return false;

}

// search data, of size len, for a data type described by format
// if found, return true and fill byte_offset with byte position of the start of the first frame and time with the time in that first frame
// else return false (byte_offset and time will be undefined)
bool is_data_format(const unsigned char* data, size_t len, unsigned int track, const headersearch_type& format, bool strict, unsigned int& byte_offset, timespec& time) {
    boyer_moore  syncwordsearch(format.syncword, format.syncwordsize);
    unsigned int next_position;
    headersearch::strict_type  strict_e = (strict ?
                             headersearch::strict_type(headersearch::chk_crc) | headersearch::chk_consistent | headersearch::chk_strict :
                             headersearch::strict_type());
    
    if( dbglev_fn()>=4 )
        strict_e |= headersearch::chk_verbose;

    // search for first header
    const unsigned char* sync_word = syncwordsearch(data, len);
    while ( sync_word ) {
        byte_offset = (unsigned int)(sync_word - data);
        if (byte_offset < format.syncwordoffset) {
            next_position = byte_offset + format.syncwordsize;
        }
        else {
            byte_offset -= format.syncwordoffset;
            const unsigned char* start_of_frame = data + byte_offset;
            if ( !strict || format.check(start_of_frame, strict_e, track) ) {
                try {
                    time = format.decode_timestamp(start_of_frame, strict_e, track);
                    break;
                }
                catch ( const exception& e ) {
                }
            }
            // the format check failed, try to find next sync word
            next_position = byte_offset + format.syncwordoffset + format.syncwordsize;
        }
        if (next_position + format.syncwordsize < len) {
            sync_word = syncwordsearch(data + next_position, len - next_position);
            
        }
        else {
            return false;
        }
    }
    if ( !sync_word ) {
        return false;
    }
    
    // we found a first header (at byte position, search for next header to verify data rate
    const unsigned int max_error = max_time_error_ns(format.frameformat);
    unsigned int frame_inc = 1;
    do {
        unsigned int next_frame = byte_offset + format.framesize * frame_inc;
        const unsigned char* frame_pointer = data + next_frame;
        if ( next_frame + format.headersize < len ) {
            // within buffer
            try {
                if ( !strict || format.check(frame_pointer, strict_e, track) ) {
                    const timespec next_time = format.decode_timestamp(frame_pointer, strict_e, track);
                    const int64_t diff = ns_diff(time, next_time);
                    // check if frame time difference is within time error margin
                    // do computation in ns and bit (so multiply by 8000000000)
                    // as both the first and second header can have an error in timing, multiply the error by 2
                    if ( ((frame_inc * format.payloadsize * 8000000000ll) >= (format.trackbitrate * format.ntrack) * (diff - 2 * max_error)) && 
                         ((frame_inc * format.payloadsize * 8000000000ll) <= (format.trackbitrate * format.ntrack) * (diff + 2 * max_error))) {
                        return true;
                    }
                    else {
                        // found a valid header, but data rate is not as expected
                        return false;
                    }
                }
            }
            catch ( const exception& e ) {
            }
        }
        else {
            // out of data
            break;
        }
        frame_inc++;
    } while (true);

    // we didn't find a matching second header
    return false;
}

// search data, of size len, for mark4/vlba tvg pattern
// if found, return true and first_valid will be the byte offset in the buffer of the first valid TVG byte, first_invalid will be the byte_offset of the first invalid tvg byte after the first valid byte
// else return true, first_valid and first_invalid will be undefined
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

// search data, of size len, for streamstor test pattern (fillword)
// if found, return true and first_valid will be the byte offset in the buffer of the first valid fillword, first_invalid will be the byte_offset of the first byte not fillword after the first valid byte
// else return false, first_valid and first_invalid will be undefined
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
