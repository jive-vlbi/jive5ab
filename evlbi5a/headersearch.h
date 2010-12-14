// Find/verify track headers in Mark4/VLBA/Mark5B datastreams
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
#ifndef JIVE5A_HEADERSEARCH_H
#define JIVE5A_HEADERSEARCH_H

#include <iostream>
#include <string>
#include <exception>

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

// recognized track/frame formats
// fmt_unknown also doubles as "none"
// If you ever must make a distinction between the two
// there are a couple of places in the code that are
// affected. I've tried to mark those with [XXX]
enum format_type {
	fmt_unknown, fmt_mark4, fmt_vlba, fmt_mark5b, fmt_none = fmt_unknown
};
std::ostream& operator<<(std::ostream& os, const format_type& fmt);

// string -> formattype. case insensitive.
//  acceptable input: "mark4", "vlba", "mark5b".
// throws "invalid_format_string" exception otherwise.
format_type text2format(const std::string& s);

// helpfull global functions.
//unsigned int headersize(format_type fmt); // only accepts fmt_mark5b as argument
unsigned int headersize(format_type fmt, unsigned int ntrack);
//unsigned int framesize(format_type fmt); // only accepts fmt_mark5b as argument
unsigned int framesize(format_type fmt, unsigned int ntrack);

// export crc routines.

// computes the CRC12 according to Mark4 CRC12 settings over n bytes
unsigned int crc12_mark4(const unsigned char* idata, unsigned int n);
// compute CRC16 as per VLBA generating polynomial over n bytes.
// this crc code is used for VLBA and Mark5B.
unsigned int crc16_vlba(const unsigned char* idata, unsigned int n);

void decode_mark4_timestamp(const void* hdr);
void decode_vlba_timestamp(const void* hdr);

// This defines a header-search entity.
// It translates known tape/disk frameformats to a generic
// set of patternmatchbytes & framesize so you should
// be able to synchronize on any of the recordingformats
// without having to know the details.
struct headersearch_type {

    // create an unitialized search-type.
    headersearch_type();

	// only mark5b allowed as argument here - the frameformat 
	// of mark5b is independant of the number of tracks
	//headersearch_type(format_type fmt);
	// mark4 + vlba require the number of tracks to compute
	// the full framesize
	headersearch_type(format_type fmt, unsigned int ntrack);

    // Allow cast-to-bool
    //  Returns false iff (no typo!) frameformat==fmt_none
    //  [XXX] be aware of fmt_unknown/fmt_none issues here
    inline operator bool( void ) const {
        return (frameformat==fmt_none);
    }

	// these properties allow us to search for headers in a
	// datastream w/o knowing *anything* specific.
	// It will find a header by locating <syncwordsize> bytes with values of 
	// <syncword>[0:<syncwordsize>] at <syncwordoffset> offset in a frame of
	// size <framesize> bytes. 
	// They can ONLY be filled in by a constructor.
	const format_type          frameformat;
	const unsigned int         ntrack;
	const unsigned int         syncwordsize;
	const unsigned int         syncwordoffset;
	const unsigned int         headersize;
	const unsigned int         framesize;
	const unsigned char* const syncword;

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
};

std::ostream& operator<<(std::ostream& os, const headersearch_type& h);

#endif
