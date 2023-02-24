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
#ifndef JIVE5A_SCAN_CHECK_H
#define JIVE5A_SCAN_CHECK_H

#include <countedpointer.h>  // ...
#include <data_check.h>      // data_reader_type, data_check_type, &cet.
#include <ezexcept.h>
#include <limits>
#include <iostream>

DECLARE_EZEXCEPT(scan_check_except)


struct scan_check_type {
    // Magik value indicating that we don't actually know how many bytes
    // were missing (which is different from e.g. knowing there are no
    // missing bytes, i.e. the value being 0).
    // We not in c++11 happy land so the assignment (now commented) won't
    // compile since std::numeric_limits<>::*() functions aren't marked
    // constexpr outisde of c++11 happyland
    static int64_t const      UNKNOWN_MISSING_BYTES; // = std::numeric_limits<int64_t>::max();
    static uint64_t const     UNKNOWN_BYTE_OFFSET;   // = std::numeric_limits<uint64_t>::max();
    // Maximum number of data checks performed on data;
    // each time an amount of data scan_check's "bytes_to_read" is
    // "sampled", this is mostly for VDIF to help improving heuristics:
    // - more data read = improved chance of finding frame rate by catching
    //   frame-number-wrap sequence
    // - more data read = improved chance of finding the actual number of
    //   VDIF threads in the data stream
    static unsigned int const maxSample = 8;

    typedef data_check_type::threadset_t threadset_t;

    format_type      format;
    unsigned int     ntrack;
    samplerate_type  trackbitrate;
    highrestime_type start_time, end_time;
    uint64_t         start_byte_offset, end_byte_offset;
    int64_t          missing_bytes;
    // iff is_vdif(format)
    struct _m_vdif_t {
        unsigned int    frame_size; // only filled in for VDIF
        unsigned int    data_size;  // only filled in for VDIF
        threadset_t     threads;    // only filled in for VDIF
        _m_vdif_t();
    } vdif;
    // if format is mark5a_tvg || ss_test_pattern
    struct _m_test_pattern_t {
        unsigned int    first_valid;
        unsigned int    first_invalid;
        _m_test_pattern_t();
    } test_pattern;
    // Filled in for Mark5
    struct _m_mark5b_t {
        bool            tvg; // if tvg flag was set in Mark5B header
        bool            dbe; // DBEs outputting Mark5B don't fill in VLBA subsecond field
        _m_mark5b_t();
    } mark5b;

    scan_check_type();
 
    // If format != fmt_none => recognized format, i.e. valid
    operator bool( void ) const;
    bool     complete( void ) const;
};

// Output the scan-check-type contents in VSI/S format
// suitable for sending back as reply to scan_check? or file_check?;
// the output follows the following pattern:
//      <return code> : <format specific fields> ;
// and can take any of these three forms:
// completely unrecognized:
//      0 : ? ;
//  test patterns mark5a_tvg or streamstor test pattern:
//      0 : <format> : <first valid> : <first invalid> ;
//  recognized VLBI data format:
//      0 : <format> : <ntrack> : <start time> : <length> : <track bit rate> : <missing bytes> [ : <vdif frame size> ]
struct vsi_format {
    friend std::ostream& operator<<(std::ostream& os, vsi_format const& vsif);

    // Influence how the string looks like
    enum vsi_fmt_flags {
        // No flags - to be able to use (condition? <some flags(s) set> : VSI_S_NONE)
        VSI_S_NONE           = 0
        // if not set displays track bit rate
        ,VSI_S_TOTALDATARATE = 1
        // normally, scan_check? reply is tied to hardware - and "tvg"
        // indicates Mark5A on Mark5A and Mark5B on Mark5B. With this
        // flag set the format is disambiguated always:
        //    ... : tvg : Mark5A : ...
        //    ... : tvg : Mark5B : ...
        ,VSI_S_EXPANDTVG     = 2
    };

    vsi_format(scan_check_type const& sct, vsi_fmt_flags f = static_cast<vsi_fmt_flags>(0));

    private:
        scan_check_type const&  __m_sct_ref;
        vsi_fmt_flags           __m_format_flags;
};
vsi_format::vsi_fmt_flags operator|(vsi_format::vsi_fmt_flags const& l, vsi_format::vsi_fmt_flags const& r);
vsi_format::vsi_fmt_flags& operator|=(vsi_format::vsi_fmt_flags& l, vsi_format::vsi_fmt_flags const& r);
vsi_format::vsi_fmt_flags operator&(vsi_format::vsi_fmt_flags const& l, vsi_format::vsi_fmt_flags const& r);

// Summary of the scan check result in a human friendly form that isn't VSI/S
std::ostream& operator<<(std::ostream& os, scan_check_type const& sct);


// Perform a scan check on the data from the data reader, decoding blocks of
// "bytes_to_read" size at a time.
// The return type can be cast to bool to decided whether the code
// recognized any data format
//
// The call assumes that some basic checking has already been done such as
// verifying that at least bytes_to_read bytes are available
scan_check_type scan_check_fn(countedpointer<data_reader_type> data_reader, uint64_t bytes_to_read, bool strict, bool verbose, unsigned int track=4);

#endif
