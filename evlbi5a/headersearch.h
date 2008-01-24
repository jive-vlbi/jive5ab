// Finds/tracks headers in a Mark4 datastream-chunks
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
