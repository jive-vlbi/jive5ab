// Finds and tracks headers in a Mark4 datastream-chunks
// Copyright (C) 2007-2008 Harro Verkouter
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
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
//
// Helps decide wether or not to (potentially) drop
// this chunk. By making sure we do not drop chunks with
// (partial) headerinformation we make sure that the
// correlator has a (much) better chance of staying 
// synchronized in lieu of packet-dropping.
//
// authors: Harro Verkouter & Bob Eldering
#ifndef JIVE5A_HEADERSEARCH_H
#define JIVE5A_HEADERSEARCH_H

// This defines a header-search entity: it keeps search state information
// between calls. You are supposed to feed it sequential chunks from
// a stream. 
// It will resynch after it seems to have lost synch (a header was
// detected and the next one is not where it expects it to be).
struct headersearch_type {

    // create an unitialized search-type.
    // Use 'reset' with appropriate #-of-tracks 
    // before starting to process chunks
    headersearch_type();

    // (re)configure for a (new) number-of-expected tracks
    // The size of the dataframe is computed from this.
    void reset(unsigned int tracks = 0);

    // returns true if buffer seems to contain
    // (part-of) a header
    bool operator()(unsigned char* buffer, unsigned int length) const;

    private:
        // Keep synch-state variables private

        // Current amount of bytes to next expected header
        mutable unsigned int bytes_to_next;
        // amount of sync-word bytes found so far. Must be
        // saved as the actual sync-word-bytes may spill into
        // next chunk.
        mutable unsigned int bytes_found;
        //  ...
        unsigned int nr_tracks;
};


#endif
