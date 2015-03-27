// Find/verify/decode track headers in Mark4/VLBA/Mark5B datastreams
// Copyright (C) 2007-2010 Harro Verkouter/Bob Eldering
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
// Authors: Harro Verkouter - verkouter@jive.nl
//          Bob Eldering - eldering@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
//
// Authors: Harro Verkouter & Bob Eldering
//
//
//   NOTE NOTE NOTE  NOTE NOTE NOTE
//
//
//        Correct timestampdecoding
//        can only happen if the timezone 
//        environmentvariable has been set
//        to UTC (or to the empty string)
//
//        jive5* does that in main()
#ifndef JIVE5A_HEADERSEARCH_H
#define JIVE5A_HEADERSEARCH_H

#include <ezexcept.h>
#include <flagstuff.h>
#include <iostream>
#include <string>
#include <exception>
#include <complex>
#include <stdint.h> // for [u]int[23468]*_t

#include <time.h>   // struct timespec
#include <string.h> // ::memset()
#include <limits.h>

// exceptions that could be thrown
struct invalid_format_string:
    public std::exception
{};
struct invalid_format:
    public std::exception
{};
struct invalid_track_requested:
    public std::exception
{};
struct invalid_number_of_tracks:
    public std::exception
{};
struct invalid_track_bitrate:
    public std::exception
{};

DECLARE_EZEXCEPT(headersearch_exception)

// recognized track/frame formats
// fmt_unknown also doubles as "none"
// If you ever must make a distinction between the two
// there are a couple of places in the code that are
// affected. I've tried to mark those with [XXX]
enum format_type {
    fmt_unknown, fmt_mark4, fmt_vlba, fmt_mark5b, fmt_mark4_st, fmt_vlba_st, fmt_vdif, fmt_vdif_legacy, fmt_none = fmt_unknown
};
std::ostream& operator<<(std::ostream& os, const format_type& fmt);

// HV: 04-Jul-2013 Some of the "check_*" functions used to 
//                 take a "strict" (boolean) parameter. We 
//                 need more fine-grained control of
//                 (1) *what* to be strict about
//                 (2) *how* to handle failures
//                 One example is that for "data_check?"
//                 and "scan_check?" Mark5 commands we
//                 don't know in advance what format we're 
//                 looking at and we're just trying all 
//                 known formats. In such a case an error 
//                 should not be verbose (confuses the users) and
//                 should not throw exceptions; we're just
//                 finding one that does NOT give an error ...
namespace headersearch {

    enum strict_flag {
        chk_syncword, chk_crc, chk_consistent, chk_verbose, chk_strict, chk_default,
        // This is a DBBC/RDBE flag to allow Mark5B data from these
        // backends. In *their* Mark5B format, the subsecond field is
        // never filled in so it doesn't make sense to check if the time
        // stamp decoded from the framenumber within seconds matches the
        // time stamp decoded from the sub second field.
        // If this flag is set this test will be disabled always.
        chk_allow_dbe,
        // This flag will make the code not throw on an invalid time stamp
        // but rather set the time stamp to "(0, 0)" {.tv_sec, .tv_nsec}
        chk_nothrow
    };

    // We use the code in 'flagstuff.h' to be consistent with e.g. how 
    // we use the flags in 'ioboard.hardware()'
    typedef flagset_type<strict_flag, unsigned int, false> strict_type;
}


// simple functions for dataformats
bool is_vdif(format_type f);

// string -> formattype. case insensitive.
//  acceptable input: "mark4", "vlba", "mark5b".
// throws "invalid_format_string" exception otherwise.
format_type text2format(const std::string& s);

// helpfull global functions.
unsigned int headersize(format_type fmt, unsigned int ntrack);
unsigned int framesize(format_type fmt, unsigned int ntrack, unsigned int vdifpayloadsize = 0);

// export crc routines.

// computes the CRC12 according to Mark4 CRC12 settings over n bytes
unsigned int crc12_mark4(const unsigned char* idata, unsigned int n);
// compute CRC16 as per VLBA generating polynomial over n bytes.
// this crc code is used for VLBA and Mark5B.
unsigned int crc16_vlba(const unsigned char* idata, unsigned int n);

// The pointers should actually point at the start of the timestamp, ie the
// 8-byte BCD timecode as per Mark4 Memo 230 (Rev 1.21, Whitney, 10 Jun
// 2005)
// Mk4 timestamp decoding is dependant on track bitrate. Hoorah.
//
// NOTE NOTE NOTE: MAKE SURE 'ts' points to a buffer of at least 8 bytes
// long!
timespec decode_mk4_timestamp(unsigned char const* ts, const unsigned int trackbitrate, const headersearch::strict_type strict);
// Mk5B and VLBA-formatter-data-on-disk (VLBA rack + Mark5A recorder) have different endianness.
// At the bottom of this file there are two struct definitions that you
// could pass as template argument
template <typename HeaderLayout>
timespec decode_vlba_timestamp(HeaderLayout const* ts, const headersearch::strict_type /*strict*/);

// forward declaration of type such that we can create pointers to it
struct headersearch_type;

void encode_mk4_timestamp(unsigned char* framedata,
                          const struct timespec ts,
                          const headersearch_type* const hdr);
void encode_mk4_timestamp_st(unsigned char* framedata,
                             const struct timespec ts,
                             const headersearch_type* const hdr);
void encode_vlba_timestamp(unsigned char* framedata,
                           const struct timespec ts,
                           const headersearch_type* const hdr);
void encode_mk5b_timestamp(unsigned char* framedata,
                           const struct timespec ts,
                           const headersearch_type* const hdr);
void encode_vdif_timestamp(unsigned char* framedata,
                           const struct timespec ts,
                           const headersearch_type* const hdr);

struct decoderstate_type {
    const double    framerate; // 1/s
    const double    frametime; // ns
    uint32_t        user[4];

    decoderstate_type():
        framerate( 0 ), frametime( 0 )
    { ::memset(&user[0], 0, sizeof(user)); }
    
    decoderstate_type( unsigned int ntrack, unsigned int trackbitrate, unsigned int payloadsz ):
        framerate( ((double)ntrack*trackbitrate)/((double)payloadsz*8) ),
        frametime( (((double)payloadsz * 8)/((double)ntrack*trackbitrate))*1.0e9 )
    { ::memset(&user[0], 0, sizeof(user)); }
};


// Functionpointer to decode a frame time from a particular track.
// Some formats (Mk5B, VDIF) have a shared header and ignore the
// track/trackbitrate.
// These functions will call upon decode_*_timestamp after, potentially,
// extracting a track
typedef timespec (*timedecoder_fn)(unsigned char const* framedata,
                                   const unsigned int track,
                                   const unsigned int ntrack,
                                   const unsigned int trackbitrate,
                                   decoderstate_type* state,
                                   const headersearch::strict_type strict);

typedef void (*timeencoder_fn)(unsigned char* framedata,
                               const struct timespec ts,
                               const headersearch_type* const);

typedef bool (headersearch_type::*headercheck_fn)(const unsigned char* framedata,
                                                  const headersearch::strict_type strict, unsigned int track) const;

// When de-channelizing/splitting frames and/or accumulating frames it
// becomes necessary to keep track of what the content is.
// When splitting (operator "/") everything is reduced by the splitting factor.
//                               the payloadoffset will be set to 0, it is assumed
//                               the header will be stripped so all payload will
//                               end up at offset 0. By analogous argument
//                               syncwordsize, -offset, headersize, -offset will be 
//                               set to zero too. Since you can't en/decode time anymore
//                               we set those to zero too.
// Splitting by a complex number - operator/(complex<unsigned int>)
//                               This is indicative of: we split the
//                               original stream in .real() chunks and each
//                               chunk will contain .imag() tracks.
//                               This allows us to extract less than the
//                               default amount of channels, e.g. by
//                               dividing a 64track recording by
//                               complex(1,4) you indicate extracting only
//                               one subband
// When accumulating (operator "*") only the payloadsize and framesize will increase,
//                                  it doesn't change the content's format
headersearch_type operator/(const headersearch_type& h, unsigned int factor);
headersearch_type operator/(const headersearch_type& h, const std::complex<unsigned int>& factor);
headersearch_type operator*(const headersearch_type& h, unsigned int factor);
headersearch_type operator*(unsigned int factor, const headersearch_type& h);





// This defines a header-search entity.
// It translates known tape/disk frameformats to a generic
// set of patternmatchbytes & framesize so you should
// be able to synchronize on any of the recordingformats
// without having to know the details.
struct headersearch_type {
    // some data formats (VDIF, Mark5B as generated by RDBE and Fila10G)
    // don't contain enough information to easily compute a trackbitrate
    // to be able to describe these data formats, use UNKNOWN_TRACKBITRATE
    static const unsigned int UNKNOWN_TRACKBITRATE = UINT_MAX;

    // Overload arithmetic operators. Used for combining (multiplication) or splitting data
    // frames (division)
    friend headersearch_type operator/(const headersearch_type& h, unsigned int factor);
    friend headersearch_type operator/(const headersearch_type& h, const std::complex<unsigned int>& factor);
    friend headersearch_type operator*(const headersearch_type& h, unsigned int factor);
    friend headersearch_type operator*(unsigned int factor, const headersearch_type& h);

    // create an unitialized search-type.
    headersearch_type();

    // mark4 + vlba require the number of tracks to compute
    // the full framesize. for Mark5B it is not that important.
    // For Mark4 we MUST know the 'frametime' (in wallclock seconds) since
    // correct timestamp decoding is actually a function of 'framelength in
    // milliseconds': for 8 and 16 Mbps/track the timecode is not accurate
    // enough and we must add a correctionfactor depending on the last digit
    // in the msec field as per Table 2, p.4 in the Alan Whitney Mark4 MEMO
    // 230(.3), Rev 1.21 10 Jun 2005.
    //
    // Note: the trackbitrate is the actual bitrate in "bits per second".
    // We only support integral-bits-written-per-second ... (would be
    // bat-shit insane to not have that constraint) (* VDIF2 will challenge
    // this, still the bat-shit insane hold ;-))
    //
    // Update: 24 May 2012 - jive5ab must support VDIF as format. In VDIF
    //         the framesize is NOT determined from number of tracks and
    //         trackformat but it is a "free" parameter.
    //         Decided to add a fourth argument to the c'tor which will be
    //         the actual VDIF payload size. This argument will be ignored for
    //         all formats not being VDIF.
    //         jive5ab thus only supports VDIF streams where each datathread
    //         has the same framesize.
    headersearch_type(format_type fmt, unsigned int ntrack, unsigned int trkbitrate, unsigned int vdifpayloadsize);

    // Allow cast-to-bool
    //  19 Mar 2012 - HV: no we don't anymore. Turns out that operator
    //                    bool breaks the operator overloading for
    //                    operator*() and operator/() - it starts considering
    //                    the following:
    //                       headersearch_type h1;
    //                       headersearch_type h2 = 2*h1;
    //                    as "operator*(int, int) <builtin>"
    //
    // This is the replacement for operator bool()
    // This updated version also takes care of returning 'false'
    // when asked a split/accumulated frame if it is valid [it isn't].
    // If a headersearch_type is 'valid' it means you can use the time
    // encoder/decoder functions and use the syncwordsize/offset fields
    // for matching.
    inline bool valid( void ) const {
        return !(frameformat==fmt_none || framesize==0);
    }
    // these properties allow us to search for headers in a
    // datastream w/o knowing *anything* specific.
    // It will find a header by locating <syncwordsize> bytes with values of 
    // <syncword>[0:<syncwordsize>] at <syncwordoffset> offset in a frame of
    // size <framesize> bytes. 
    // They can ONLY be filled in by a constructor.
    const format_type          frameformat;
    const unsigned int         ntrack;
    const unsigned int         trackbitrate;
    const unsigned int         syncwordsize;
    const unsigned int         syncwordoffset;
    const unsigned int         headersize;
    const unsigned int         framesize;
    const unsigned int         payloadsize;
    const unsigned int         payloadoffset;
    const timedecoder_fn       timedecoder;
    const timeencoder_fn       timeencoder;
    const headercheck_fn       checker;
    const unsigned char* const syncword;

    // static member function - it's basically just here to sort of put it
    // into a namespace rather than make it a global function.
    // It extracts the the first 'nbit' bits from track # 'track', assuming the frame was
    // recorded with 'ntrack' tracks.
    // The caller is responsible for making sure 'dst' points to a buffer
    // which can hold at least nbit bits.
    template<bool strip_parity> static void extract_bitstream(
        unsigned char* dst,
        const unsigned int track, const unsigned int ntrack, unsigned int nbit,
        unsigned char const* frame);

    // Extract the time from the header. The tracknumber *may* be ignored,
    // depending on the actual frameformat
    timespec decode_timestamp( unsigned char const* framedata, const headersearch::strict_type strict, const unsigned int track=0 ) const;

    // Encode the timestamp in the framedata at the position where it should
    // be. User is responsible for making sure that the buffer pointed to by
    // framedata is at least headersearch_type::headersize (for the selected
    // format)
    void     encode_timestamp(unsigned char* framedata, const struct timespec ts) const;

    // Attempt to verify if we're indeed looking at a frame
    // of the type the headersearch_type is describing. This
    // may include extracting a track and perform CRC check
    // on that data. If you already verified that the syncword 
    // is where it should be you can tell this routine to skip
    // that check.
    bool     check(unsigned char const* framedata, const headersearch::strict_type checksyncword, unsigned int track) const;

    // include templated memberfunction(s) which will define the
    // actual checking functions. by making them templated we can
    // make the checking algorithm work with *any* datatype that
    // supports "operator[]" indexing and yields an unsigned char.
    // (e.g. unsigned char*, std::vector<unsigned char>, circular_buffer)
    //
    // // return true if the bytes in <byte-adressable-thingamabob>
    // // represent a valid header for the frameformat/ntrack
    // // combination in *this. 
    // bool check(<byte-addressable-thingamabob>) const;
#include <headersearch.impl>

    // In order to be able to "check" VDIF (you can't really) we 
    // still have to have a no-op function that the functionpointer
    // can point to. Always returns true.
    bool    nop_check(unsigned char const*, const headersearch::strict_type, unsigned int) const;

    private:
        // this is a copy-c'tor like construction not part of the public API
        headersearch_type(const headersearch_type& other, int factor);
        headersearch_type(const headersearch_type& other, const std::complex<unsigned int>& factor);

        mutable decoderstate_type   state;
};

std::ostream& operator<<(std::ostream& os, const headersearch_type& h);


///////////////////////////////////////////////////////////////////
////                                                           ////
////    Function to go from format string (Walter Brisken)     ////
////    to actual headersearch object.                         ////
////    Walter's format has support for decimation, we don't.  ////
////    (We parse/swallow but inform the user it's ignored)    ////
////                                                           ////
////    More info in the .cc file near the implementation      ////
////                                                           ////
///////////////////////////////////////////////////////////////////
headersearch_type* text2headersearch(const std::string& s);



// The different byte-layouts of the VLBA-tape-on-harddisk and Mark5B format
struct vlba_tape_ts {
    uint8_t  J1:4;
    uint8_t  J2:4;
    uint8_t  S4:4;
    uint8_t  J0:4;
    uint8_t  S2:4;
    uint8_t  S3:4;
    uint8_t  S0:4;
    uint8_t  S1:4;

    uint8_t  SS2:4;
    uint8_t  SS3:4;
    uint8_t  SS0:4;
    uint8_t  SS1:4;
    uint16_t CRC;
};
// "Stolen" from SFXC
struct m5b_header {
    uint32_t    syncword;
    uint32_t    frameno:15;
    uint8_t     tvg:1;
    uint16_t    user_specified; 
};
struct mk5b_ts {
    uint8_t  S0:4;
    uint8_t  S1:4;
    uint8_t  S2:4;
    uint8_t  S3:4;
    uint8_t  S4:4;
    uint8_t  J0:4;
    uint8_t  J1:4;
    uint8_t  J2:4;

    uint16_t CRC;
    uint8_t  SS0:4;
    uint8_t  SS1:4;
    uint8_t  SS2:4;
    uint8_t  SS3:4;
};

struct vdif_header {
    // Word 0
    uint32_t      epoch_seconds:30;
    uint8_t       legacy:1, invalid:1;
    // Word 1
    uint32_t      data_frame_num:24;
    uint8_t       ref_epoch:6, unused:2;
    // Word 2
    uint32_t      data_frame_len8:24;
    uint8_t       log2nchans:5, version:3;
    // Word 3
    uint16_t      station_id:16, thread_id:10;
    uint8_t       bits_per_sample:5, complex:1;

    vdif_header() {
        ::memset((void*)this, 0x0, sizeof(vdif_header));
        this->legacy = 1;
    }
#if 0
    // Word 4
    uint32_t      user_data1:24;
    uint8_t       edv:8;
  // Word 5-7
  uint32_t      user_data2,user_data3,user_data4;
#endif
};

#endif
