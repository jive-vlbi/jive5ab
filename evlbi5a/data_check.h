#ifndef JIVE5A_DATA_CHECK_H
#define JIVE5A_DATA_CHECK_H

#include <time.h> // struct timespec
#include <fstream>

#include <headersearch.h>
#include <xlrdevice.h>
#include <playpointer.h>
#include <ezexcept.h>


struct data_check_type {
    format_type format;
    unsigned int ntrack;
    unsigned int trackbitrate;
    timespec time;
    unsigned int byte_offset;
    unsigned int vdif_frame_size; // only filled in for VDIF
    unsigned int vdif_threads; // only filled in for VDIF
    unsigned int frame_number; // only filled in for formats which have frame numbers

    // to get full information for VDIF and Mark5B generated by some hardware (RDBE, Fila10G)
    // we need to see a frame number reset (a second boundary), 
    // if we don't see that in the block of data,
    // trackbitrate and time.tv_nsec are invalid and this function returns true
    bool is_partial();

    friend std::ostream& operator<<( std::ostream& os, const data_check_type& d );
};

// search data, of size len, for a number of data formats
// if any is found, return true and fill format, trackbitrate and ntrack with the parameters describing the found format, fill byte_offset with byte position of the start of the first frame and time with the time in that first frame
// else return false (reference parameters will be undefined)
// VDIF notes, if the format is reported as VDIF: 
// 1) the values in result will represent the first VDIF thread found
// 2) to determine the data rate (and therefor also the trackbitrate), 
//    we would need up to 1s of data, if not enough data is available,
//    the nano second field in result.time will be set to -1 and
//    result.trackbitrate will be UNKNOWN_TRACKBITRATE
//    if we find enough data, we assume that trackbitrate is 2**n * 10e6 ( n >= -6 )
// 3) the number of VDIF threads reported will only make sense if all VDIF threads are of the same "shape"
bool find_data_format(const unsigned char* data, size_t len, unsigned int track, bool strict, data_check_type& result);

// search data, of size len, for a data type described by format
// if found, return true and fill byte_offset with byte position of the start of the first frame and time with the time in that first frame
// else return false (byte_offset and time will be undefined)
bool is_data_format(const unsigned char* data, size_t len, unsigned int track, const headersearch_type& format, bool strict, unsigned int vdif_threads, unsigned int& byte_offset, timespec& time, unsigned int& frame_number);

// search data, of size len, for mark4/vlba tvg pattern
// if found, return true and first_valid will be the byte offset in the buffer of the first valid TVG byte, first_invalid will be the byte_offset of the first invalid tvg byte after the first valid byte
// else return false, first_valid and first_invalid will be undefined
bool is_mark5a_tvg(const unsigned char* data, size_t len, unsigned int& first_valid, unsigned int& first_invalid);

// search data, of size len, for streamstor test pattern (fillword)
// if found, return true and first_valid will be the byte offset in the buffer of the first valid fillword, first_invalid will be the byte_offset of the first byte not fillword after the first valid byte
// else return false, first_valid and first_invalid will be undefined
bool is_ss_test_pattern(const unsigned char* data, size_t len, unsigned int& first_valid, unsigned int& first_invalid);

bool seems_like_vdif(const unsigned char* data, size_t len, data_check_type& result);

// tries to combine the data check types, filling in unknown subsecond information in either field using the other (assuming both are in fact the same data type)
// 'byte_offset' is the amount of bytes between the read offset of 'first' and 'last'
// return true iff both first and last are complete (is_partial() returns false)
bool combine_data_check_results(data_check_type& first, data_check_type& last, uint64_t byte_offset);

class data_reader_type {
 public:
    virtual uint64_t read_into( unsigned char* buffer, uint64_t offset, uint64_t len ) = 0;
    virtual int64_t length() const = 0;
};

class file_reader_type : public data_reader_type {
 public:
    file_reader_type( const std::string& filename );
    uint64_t read_into( unsigned char* buffer, uint64_t offset, uint64_t len );
    int64_t length() const;
 private:
    file_reader_type();
    int64_t file_length;
    std::ifstream file;
};

class streamstor_reader_type : public data_reader_type {
 public:
    streamstor_reader_type( SSHANDLE sshandle, const playpointer& start, const playpointer& end );
    uint64_t read_into( unsigned char* buffer, uint64_t offset, uint64_t len );
    int64_t length() const;
    
 private:
    streamstor_reader_type();
    SSHANDLE sshandle;
    playpointer start;
    playpointer end;
};

DECLARE_EZEXCEPT(streamstor_reader_bounds_except)
#endif
