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
};

// search data, of size len, for a number of data formats
// if any is found, return true and fill format, trackbitrate and ntrack with the parameters describing the found format, fill byte_offset with byte position of the start of the first frame and time with the time in that first frame
// else return false (reference parameters will be undefined)
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

#endif
