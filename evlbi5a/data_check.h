#ifndef JIVE5A_DATA_CHECK_H
#define JIVE5A_DATA_CHECK_H

#include <time.h> // struct timespec

#include <headersearch.h>

struct data_check_type {
    format_type format;
    unsigned int ntrack;
    unsigned int trackbitrate;
    timespec time;
    unsigned int byte_offset;
    unsigned int vdif_frame_size;

    // to get full information for VDIF we need to see a frame number reset
    // (a second boundary), if we don't see that in the block of data,
    // trackbitrate and time.tv_nsec are invalid and this function returns true
    bool is_partial_vdif();
};

// search data, of size len, for a number of data formats
// if any is found, return true and fill format, trackbitrate and ntrack with the parameters describing the found format, fill byte_offset with byte position of the start of the first frame and time with the time in that first frame
// else return false (reference parameters will be undefined)
// VDIF notes, if the format is reported as VDIF: 
// 1) the values in result will represent the first VDIF thread found
// 2) to determine the data rate (and therefor also the trackbitrate), 
//    we would need up to 1s of data, if not enough data is available,
//    the nano second field in result.time will be set to -1 and
//    result.trackbitrate will be 0
//    if we find enough data, we assume that trackbitrate is 2**n * 10e6 ( n >= -6 )
// 3) will only check for VDIF if not strict
bool find_data_format(const unsigned char* data, size_t len, unsigned int track, bool strict, data_check_type& result);

// search data, of size len, for a data type described by format
// if found, return true and fill byte_offset with byte position of the start of the first frame and time with the time in that first frame
// else return false (byte_offset and time will be undefined)
bool is_data_format(const unsigned char* data, size_t len, unsigned int track, const headersearch_type& format, bool strict, unsigned int& byte_offset, timespec& time);

// search data, of size len, for mark4/vlba tvg pattern
// if found, return true and first_valid will be the byte offset in the buffer of the first valid TVG byte, first_invalid will be the byte_offset of the first invalid tvg byte after the first valid byte
// else return false, first_valid and first_invalid will be undefined
bool is_mark5a_tvg(const unsigned char* data, size_t len, unsigned int& first_valid, unsigned int& first_invalid);

// search data, of size len, for streamstor test pattern (fillword)
// if found, return true and first_valid will be the byte offset in the buffer of the first valid fillword, first_invalid will be the byte_offset of the first byte not fillword after the first valid byte
// else return false, first_valid and first_invalid will be undefined
bool is_ss_test_pattern(const unsigned char* data, size_t len, unsigned int& first_valid, unsigned int& first_invalid);

bool seems_like_vdif(const unsigned char* data, size_t len, data_check_type& result);

#endif
