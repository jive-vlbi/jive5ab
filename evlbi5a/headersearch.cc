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
#include <headersearch.h>

#include <string.h>

headersearch_type::headersearch_type() {
  reset();
}

void headersearch_type::reset(unsigned int tracks) {
  bytes_to_next = 0;
  bytes_found   = 0;
  nr_tracks     = tracks;
}

bool headersearch_type::operator()(unsigned char* buffer, unsigned int length) const {
    // A syncword is 4*ntrack 0xff bytes
    const unsigned int  nbyte_needed( 4*nr_tracks );
    const unsigned int  nbyte_header_before( 8*nr_tracks );
    const unsigned int  nbyte_header_after( 8*nr_tracks );
    // a static bool to indicate that (part-of) the header
    // spilled into the next buffer. This value can only be
    // set *after* we entered the function so if it's true
    // upon entering .... it means that it was pre-set to true
    // and we are looking at the next buffer and it most likely
    // contains header info. If we expect it to contain a syncword
    // as well, then that takes precedence.
    static bool         header_in_next_buffer = false;
    bool                header_in_this_buffer = header_in_next_buffer;

    // we've saved the previous value of 'header_in_next_buffer'
    // and we *must* reset it unconditionally. This is good place 
    // to do just that :)
    header_in_next_buffer = false;

    // If bytes_to_next is != 0, this implies we know where to 
    // look for the next syncword.

    // If the following condition holds this implies two things:
    // (1) we think we know where to expect the next syncword, and,
    // (2) we expect the syncword certainly NOT in this buffer.
    // However, there may still be header bytes in this buffer,
    // namely, when we expect the sync-word to appear in the
    // first 'nbyte_header_before' bytes of the next buffer
    // (because the syncword somewhat marks the middle of the 
    // header rather than the start).
    if( bytes_to_next>=length ) {
        bytes_to_next -= length;

        // And let the caller know if we think there's valuable
        // bytes in here.
        // Note: we may safely return here because we do NOT have
        // to look for a syncword: we've ascertained that we really
        // do not expect it in *this* buffer!
        return (header_in_this_buffer || (bytes_to_next<nbyte_header_before));
    }

    // If we end up here, we need to find (part-of) the syncword.
    // The loop either continues a previous search or starts
    // a new one ... 

    // See how many 0xff'en we find
    unsigned char*       cur  = buffer + bytes_to_next;
    const unsigned char* last = buffer + length;

    while( cur<last ) {
        if( *cur==0xff ) {
            if( ++bytes_found == nbyte_needed )
                break;
            // only increment *after* we check for nbyte_needed:
            // this way, cur==last signals that we did NOT
            // find a full syncword!
            cur++;
        } else {
            // Crap! No 0xff? This means that there doesn't seem to
            // be a zink-woord here. Let's restart the search from
            // the next byte.
            unsigned char*  nxt = (unsigned char*)::memchr(cur+1, 0xff, (last-(cur+1)));
            // whatever happens: this is certainly the case!
            bytes_found = 0;
            // depending on the search-result, we either continue looking
            // for the syncword or terminate the loop
            if( nxt )
                cur = nxt;
            else
                cur = (unsigned char*)last;
        }
    }

    // If cur==last, this means we still need to find
    // 0xff'en, so continue search into next buffer
    // (or start from scratch, but that makes no difference
    //  at all)
    if( cur==last )
       bytes_to_next = 0;
    else if( bytes_found==nbyte_needed ) {
        // Note: 'cur' points at the last 0xff found, so
        // there is actually one byte less left *after* the syncword!
        const unsigned int    nbyte_left = (last - (cur+1));

        // The actual amount of bytes between two syncwords
        // is 2500 bytes/track. 'nbyte_left' gives the number of
        // bytes *after* the full syncword that we just found,
        // so the tape-frame started 'nbyte_needed' before that!
        bytes_to_next = (nr_tracks * 2500) - (nbyte_left + nbyte_needed);
        bytes_found   = 0;

        // Only in this case we can have header-spill into
        // the next buffer: it occurs when less than nheader_byte_after
        // are in this buffer! Save that knowledge for the next call
        // to this function.
        header_in_next_buffer = (nbyte_left<nbyte_header_after);
    }

    // We've found (part-of) a syncword under the following 
    // circumstances: bytes_found>0 || bytes_to_next>0.
    // Also: if we expected header bytes in this buffer as well,
    // this buffer may be important ...
    return (bytes_found>0 || bytes_to_next>0 || header_in_this_buffer);
}
