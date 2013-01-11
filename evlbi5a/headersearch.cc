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
#include <headersearch.h>
#include <dosyscall.h>
#include <stringutil.h>
#include <timezooi.h>
#include <string.h>
#include <stdlib.h>

#include <arpa/inet.h>

#ifdef GDBDEBUG
#include <evlbidebug.h>
#endif

using std::ostream;
using std::string;
using std::endl;
using std::complex;


bool is_vdif(format_type f) {
    return (f==fmt_vdif || f==fmt_vdif_legacy);
}


format_type text2format(const string& s) {
    const string lowercase( tolower(s) );
    if( lowercase=="mark4" )
        return fmt_mark4;
    else if( lowercase=="vlba" )
        return fmt_vlba;
    else if( lowercase=="mark5b" )
        return fmt_mark5b;
    else if( lowercase=="mark4 st" )
        return fmt_mark4_st;
    else if( lowercase=="vlba st" )
        return fmt_vlba_st;
    else if( lowercase=="vdif" )
        return fmt_vdif;
    else if( lowercase=="vdif legacy" )
        return fmt_vdif_legacy;
    throw invalid_format_string();
}

// Now always return a value. Unknown/unhandled formats get 0
// as framesize. Well, you get what you ask for I guess
// [XXX] - default behaviour may need to change if fmt_unknown/fmt_none
//         separate
unsigned int headersize(format_type fmt, unsigned int ntrack) {
    // all know formats have 8 bytes of timecode
    unsigned int  trackheadersize = 0;

    switch (fmt) {
        // VDIF and legacy VDIF have no dependency on number of tracks
        // for their headersize
        case fmt_vdif_legacy:
            ntrack          = 1;
            trackheadersize = 16;
            break;
        case fmt_vdif:
            ntrack          = 1;
            trackheadersize = 32;
            break;
            // for mark5b there is no dependency on number of tracks
        case fmt_mark5b:
            ntrack = 1;
            trackheadersize = 16;
            break;
            // both mark4/vlba have 4 bytes of syncword preceding the
            // 8 bytes of timecode (ie 12 bytes per track)
        case fmt_vlba:
        case fmt_vlba_st:
            trackheadersize = 12;
            break;
            // mark4 has 8 pre-syncword bytes
        case fmt_mark4:
        case fmt_mark4_st:
            trackheadersize = 20;
            break;
        default:
            break;
    }

    // The full header for all tracks is just the number of tracks times the
    // size-per-track ...
    // Straight through has a parity bit per 8 bits
    if( fmt==fmt_mark4_st || fmt==fmt_vlba_st )
        return (ntrack * trackheadersize) * 9 / 8;
    else
        return (ntrack * trackheadersize);
}

// Now always return a value. Unknown/unhandled formats get 0
// as framesize. Well, you get what you ask for I guess
// [XXX] - default behaviour may need to change if fmt_unknown/fmt_none
//         separate
unsigned int framesize(format_type fmt, unsigned int ntrack, unsigned int vdifframesize) {
    const unsigned int hsize = headersize(fmt, ntrack);

    switch( fmt ) {
        case fmt_mark5b:
            return hsize + 10000;
        case fmt_mark4:
            return hsize + (ntrack*2480);
        case fmt_vlba:
            return hsize + (ntrack*2508); // 2500 bytes of data and 8 bytes of aux data per track
        case fmt_mark4_st:
            return hsize + (ntrack*2480) * 9 / 8;
        case fmt_vlba_st:
            return hsize + (ntrack*2508) * 9 / 8;
        case fmt_vdif:
        case fmt_vdif_legacy:
            return hsize + vdifframesize;
        default:
            break;
    }
    //ASSERT2_COND(false, SCINFO("invalid dataformat '" << fmt << "'" << endl));
    // should be unreachable but st00pid compiler can't tell.
    return 0;
}

#define FMTKEES(os, fmt, s) \
        case fmt: os << s; break;

ostream& operator<<(ostream& os, const format_type& f) {
    switch(f) {
        FMTKEES(os, fmt_mark4,       "mark4");
        FMTKEES(os, fmt_vlba,        "vlba");
        FMTKEES(os, fmt_mark4_st,    "mark4 st");
        FMTKEES(os, fmt_vlba_st,     "vlba st");
        FMTKEES(os, fmt_mark5b,      "mark5b");
        FMTKEES(os, fmt_vdif_legacy, "VDIF (legacy)");
        FMTKEES(os, fmt_vdif,        "VDIF");
        // [XXX] if fmt_none becomes its own type - do add it here!
        FMTKEES(os, fmt_unknown,     "<unknown>");
        default:
            os << "<INVALID DATAFORMAT!>";
            break;
    }
    return os;
}

ostream& operator<<(ostream& os, const headersearch_type& h) {
    return os << "[trackformat=" << h.frameformat << ", "
              << "ntrack=" << h.ntrack << ", "
              << "syncwordsize=" << h.syncwordsize << ", "
              << "syncwordoffset=" << h.syncwordoffset << ", "
              << "headersize=" << h.headersize << ", "
              << "framesize=" << h.framesize << ", "
              << "payloadsize=" << h.payloadsize << ", "
              << "payloadoffset=" << h.payloadoffset
              << "]";
}

headersearch_type operator/(const headersearch_type& h, unsigned int factor) {
    ASSERT2_COND( factor>0, SCINFO("Cannot divide frame into 0 pieces") );
    return headersearch_type(h, -1*(int)factor);
}
headersearch_type operator/(const headersearch_type& h, const complex<unsigned int>& factor) {
    ASSERT2_COND( factor.real()>0, SCINFO("Cannot divide frame into 0 pieces") );
    return headersearch_type(h, factor);
}
headersearch_type operator*(const headersearch_type& h, unsigned int factor) {
    ASSERT2_COND( factor>0, SCINFO("Cannot multiply frame by 0") );
    return headersearch_type(h, (int)factor);
}
headersearch_type operator*(unsigned int factor, const headersearch_type& h) {
    ASSERT2_COND( factor>0, SCINFO("Cannot multiply frame by 0") );
    return headersearch_type(h, (int)factor);
}

// syncwords for the various datastreams
// note: the actual definition of the mark4syncword is at the end of the file -
// it is a wee bit large - it accommodates up to 64 tracks of syncword.
extern unsigned char mark4_syncword[];
extern unsigned char st_syncword[];
// mark5b syncword (0xABADDEED in little endian)
static unsigned char mark5b_syncword[] = {0xed, 0xde, 0xad, 0xab};

// Mark4 timestamp is 13 BCD coded digits
// YDDD HHMM SSss s     BCD
// 0 1  2 3  4 5  6     byte index
// We assume that 'ts' points at the first bit of the Y BCDigit
struct timespec decode_mk4_timestamp(unsigned char const* trackdata, const unsigned int trackbitrate) {
    // ...
    struct mk4_ts {
        uint8_t  D2:4;
        uint8_t  Y:4;
        uint8_t  D0:4;
        uint8_t  D1:4;
        uint8_t  H0:4;
        uint8_t  H1:4;
        uint8_t  M0:4;
        uint8_t  M1:4;
        uint8_t  S0:4;
        uint8_t  S1:4;
        uint8_t  SS1:4;
        uint8_t  SS2:4;
        uint8_t  CRC2:4;
        uint8_t  SS0:4;
        uint8_t  CRC0:4;
        uint8_t  CRC1:4;
    };
    struct tm       m4_time;
    mk4_ts const*   ts = (mk4_ts const*)trackdata;
    struct timespec rv = {0, 0};

    ::memset(&m4_time, 0, sizeof(struct tm));

#ifdef GDBDEBUG
    DEBUG(4, "mk4_ts: raw BCD digits " 
             << (int)ts->Y << " "
             << (int)ts->D2 << (int)ts->D1 << (int)ts->D0 << " "
             << (int)ts->H1 << (int)ts->H0 << " "
             << (int)ts->M1 << (int)ts->M0 << " "
             << (int)ts->S1 << (int)ts->S0 << " "
             << (int)ts->SS2 << (int)ts->SS1 << (int)ts->SS0 << endl);
#endif

    // Decode the fields from the timecode.
    m4_time.tm_year = ts->Y;
    m4_time.tm_mday = 100*ts->D2 + 10*ts->D1 + ts->D0;
    m4_time.tm_hour = 10*ts->H1 + ts->H0;
    m4_time.tm_min  = 10*ts->M1 + ts->M0;
    m4_time.tm_sec  = 10*ts->S1 + ts->S0;
    // We keep time at nanosecond resolution; the timestamp has milliseconds
    // So we already correct for that
    rv.tv_nsec      = 100000000*ts->SS2 + 
                      10000000 *ts->SS1 + 
                      1000000  *ts->SS0;
#ifdef GDBDEBUG
    DEBUG(4, "mk4_ts: " 
             << m4_time.tm_year << " "
             << m4_time.tm_mday << " "
             << m4_time.tm_hour << "h"
             << m4_time.tm_min << "m"
             << m4_time.tm_sec << "s "
             << "+" << (((double)rv.tv_nsec)/1.0e9) << "sec "
             << "[" << (int)ts->SS2 << ", " << (int)ts->SS1 << ", " << (int)ts->SS0 << "]" << endl);
#endif

    // depending on the actual trackbitrate we may have to apply a
    // correction - see Mark4 MEMO 230(.3)
    if( trackbitrate==8000000 || trackbitrate==16000000 ) {
        const uint8_t  ss0 = ts->SS0;
        // '9' is an invalid last digit as is '4'
        ASSERT2_COND( !(ss0==4 || ss0==9),
                      SCINFO("Invalid Mark4 timecode: last digit is "
                             << ss0
                             << " which may not occur with trackbitrate "
                             << trackbitrate << "bps"));
        // Apply the correction.
        // The table (Table 2, p.4, MEMO 230(.3)) lists *implied*
        // frame-time based on the last digit
        // e.g.:
        //  last digit       actual frametime (msec)
        //  0                0.00
        //  1                1.25
        //  2                2.50
        //  3                3.75
        //  4                <invalid>
        //  5                5.00
        //  6                6.25
        //  7                7.50
        //  8                8.75
        //  9                <invalid>
        //
        //  So the correction goes in steps of
        //  0.25 msec = 25 e-5 s
        //  We do accounting in nano-seconds
        //  so 25e-5s = 25e-5/1e-9 = 25e4 nanoseconds
        rv.tv_nsec += ((ss0 % 5) * 250000);
#ifdef GDBDEBUG
        if( ss0%5 ) {
            DEBUG(4, "mk4_ts: adding " << ((ss0%5)*250000)/1000 << "usec" << endl);
        }
#endif
    }
    // Now we can finally start computing the actual time
    // Mk4 only records the last digit of the year (ie 'year within decade')
    // so we have to try to recover the full year.
    // If the data is older than 10 years we're stuft.

    // Add our current decade to the yearnumber. If we end up in the future
    // we set it back by 10 years since the data must've come (hopefully)
    // from the previous decade.
    const int current_year = ::get_current_year();

    m4_time.tm_year = m4_time.tm_year + current_year - current_year%10;
    if( m4_time.tm_year>current_year )
        m4_time.tm_year -= 10;

    // Now that we have a properly filled in struct tm
    // it's simple to make a time_t out of it.
    // We must not forget to correct the '- 1900'
    // Make sure the timezone environment variable is set to empty or to
    // UTC!!!
    m4_time.tm_year -= 1900;
    rv.tv_sec  = ::mktime(&m4_time);

#ifdef GDBDEBUG
    char buf[32];
    ::strftime(buf, sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm%Ss", &m4_time);
    DEBUG(4, "mk4_ts: after normalization " << buf << " +" << (((double)rv.tv_nsec)*1.0e-9) << "s" << endl);
#endif
    return rv;
}

// Mark5B and the VLBA use the same logical header (the same fields) only
// the way they're stored on tape or disk are different - the Endianness is
// different between the two.
// So we implement a templated header -> timestamp decoder; the way how you
// process the digits in the header is identical but whence you get the
// digits from memory is different between the two systems.
// At the cost of one functioncall overhead you share the decoding - which
// is arguably easier to maintain/debug
template <typename Header>
timespec decode_vlba_timestamp(Header const* ts) {
    const int       current_mjd = (int)::mjdnow();
    struct tm       vlba_time;
    struct timespec rv = {0, 0};

    // clear for further use
    ::memset(&vlba_time, 0, sizeof(struct tm));

#ifdef GDBDEBUG
    DEBUG(4, "vlba_ts: current_mjd = " << current_mjd << endl);
    DEBUG(4, "vlba_ts: raw BCD digits "  
             << (int)ts->J2 << (int)ts->J1 << (int)ts->J0 << " "
             << (int)ts->S4 << (int)ts->S3 << (int)ts->S2 << (int)ts->S1 << (int)ts->S0 << " "
             << "." << (int)ts->SS3 << (int)ts->SS2 << (int)ts->SS1 << (int)ts->SS0 << endl);
#endif

    // Decode the fields from the timecode.
    vlba_time.tm_year = 70; /* == 1970 - 1900  */
    vlba_time.tm_mday = 100*ts->J2 + 10*ts->J1 + ts->J0;
    vlba_time.tm_sec  = 10000*ts->S4 + 1000*ts->S3 + 100*ts->S2 + 10*ts->S1 + ts->S0;
    vlba_time.tm_hour = vlba_time.tm_sec / 3600;
    vlba_time.tm_sec  = vlba_time.tm_sec % 3600;
    vlba_time.tm_min  = vlba_time.tm_sec / 60;
    vlba_time.tm_sec  = vlba_time.tm_sec % 60;
    // We keep time at nanosecond resolution; the timestamp has 10e-4
    // seconds resolution so we must multiply by 100,000 (1e5)
    rv.tv_nsec        = 100000000 *ts->SS3 +
                        10000000  *ts->SS2 +
                        1000000   *ts->SS1 +
                        100000    *ts->SS0;

#ifdef GDBDEBUG
    DEBUG(4, "vlba_ts: " 
             << vlba_time.tm_year << " "
             << vlba_time.tm_mday << " "
             << vlba_time.tm_hour << "h"
             << vlba_time.tm_min << "m"
             << vlba_time.tm_sec << "s "
             << "+" << (((double)rv.tv_nsec)/1.0e9) << "sec "
             << endl);
#endif

    // VLBA has Truncated Julian Day: truncated jd == jd modulo 1000.
    // Now figure out what the actual year was. If the data is older than
    // 1000 days (2.7 years) this algorithm will fail and we're stuft w/o knowing it.

    // Convert from TJD to full MJD.
    // Do that by assuming that the observing date and the current date are
    // in the same 1000 day window; that they share the same 0-point.
    // If the computed MJD is in the future we set it back 1000 days -
    // that's about the best we can do, given the 1000-day ambiguity.
    vlba_time.tm_mday = vlba_time.tm_mday + current_mjd - (current_mjd % 1000);
    // if we end up in the future, dat ain't good
    if( vlba_time.tm_mday>current_mjd )
        vlba_time.tm_mday -= 1000;
#ifdef GDBDEBUG
    DEBUG(4, "vlba_ts: compute full MJD " << vlba_time.tm_mday << endl);
#endif
    // Subtract the UNIX_MJD_EPOCH to transform it into UNIX days
    // Also correcting for "tm_mday" counting from 1, rather than from 0
    vlba_time.tm_mday = vlba_time.tm_mday - UNIX_MJD_EPOCH + 1;
#ifdef GDBDEBUG
    DEBUG(4, "vlba_ts: => UNIX days " << vlba_time.tm_mday << endl);
#endif

    // We've set the date to the "tm_mday'th of Jan, 1970".
    // Let the normalization work out the year/day-of-year from that.
    // This only works correctly if the timezone environment variable has been set to empty or "UTC"
    // (see ctime(3), tzset(3))
    rv.tv_sec  = ::mktime(&vlba_time);

#ifdef GDBDEBUG
    char buf[32];
    ::strftime(buf, sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm%Ss", &vlba_time);
    DEBUG(4, "vlba_ts: after normalization " << buf << " +" << (((double)rv.tv_nsec)*1.0e-9) << "s" << endl);
#endif
    return rv;
}



void encode_mk4_timestamp(unsigned char* framedata,
                          const struct timespec ts,
                          const headersearch_type* const hdr) {
    int                i, j;
    long               ms, frametime_ns;
    uint8_t*           frame8  = (uint8_t *)framedata;
    uint16_t*          frame16 = (uint16_t *)framedata;
    uint32_t*          frame32 = (uint32_t *)framedata;
    uint64_t*          frame64 = (uint64_t *)framedata;
    struct tm          tm;
    unsigned int       crc;
    unsigned char      header[20];
    const unsigned int ntrack       = hdr->ntrack;
    const unsigned int trackbitrate = hdr->trackbitrate;

    if( trackbitrate==0 )
        throw invalid_track_bitrate();

    gmtime_r(&ts.tv_sec, &tm);
    tm.tm_yday += 1;

    ::memset(header, 0, 8);
    header[8]  = 0xff;
    header[9]  = 0xff;
    header[10] = 0xff;
    header[11] = 0xff;
    header[12] = (unsigned char)((tm.tm_year / 1) % 10 << 4);
    header[12] = (unsigned char)(header[12] | ((tm.tm_yday / 100) % 10));
    header[13] = (unsigned char)((tm.tm_yday / 10) % 10 << 4);
    header[13] = (unsigned char)(header[13] | ((tm.tm_yday / 1) % 10));
    header[14] = (unsigned char)((tm.tm_hour / 10) % 10 << 4);
    header[14] = (unsigned char)(header[14] | ((tm.tm_hour / 1) % 10 << 0));
    header[15] = (unsigned char)((tm.tm_min / 10) % 10 << 4);
    header[15] = (unsigned char)(header[15] | ((tm.tm_min / 1) % 10));

    header[16] = (unsigned char)((tm.tm_sec / 10) % 10 << 4);
    header[16] = (unsigned char)(header[16] | ((tm.tm_sec / 1) % 10));

    // Do the millisecond stuff
    // Round off to the nearest frametime
    frametime_ns = (long)((((double)(2500 * 8))/(double)trackbitrate)*1.0e9);
    ms           = ((ts.tv_nsec / frametime_ns) * frametime_ns) / 1000000;
    header[17] = (unsigned char)( ((ms/100) % 10) << 4 );
    header[17] = (unsigned char)( header[17] | ((ms/10)  % 10) );
    header[18] = (unsigned char)( ((ms/1)   % 10) );
    // for 8 and 16 Mbps the last digit of the frametime
    // cannot be 4 or 9
    if( (trackbitrate==8000000 || trackbitrate==16000000) &&
        (header[18]==4 || header[18]==9) ) {
        header[18]-=1;
    }
    // header[18] now has the correct final millisecond digit
    // but it needs to be moved to the other nibble:
    header[18] <<= 4;
    header[19] = 0;
    crc = crc12_mark4((unsigned char *)&header, sizeof(header));
    header[18] = (unsigned char)(header[18] | ((crc >> 8) & 0x0f));
    header[19] = (unsigned char)(crc & 0xff);

    switch (ntrack) {
    case 8:
        for (i = 0; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame8[(i * 8) + j] = ~0;
                else
                    frame8[(i * 8) + j] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame16[(i * 8) + j] = ~0;
                else
                    frame16[(i * 8) + j] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame32[(i * 8) + j] = ~0;
                else
                    frame32[(i * 8) + j] = 0;
            }
        }
        break;
    case 64:
        for (i = 0; i < 20; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame64[(i * 8) + j] = ~0;
                else
                    frame64[(i * 8) + j] = 0;
            }
        }
        break;
    default:
        throw invalid_number_of_tracks();
    }
    return;
}

void encode_vlba_timestamp(unsigned char* framedata,
                           const struct timespec ts,
                           const headersearch_type* const hdr) {
    int                mjd, sec, i, j;
    long               dms; // deci-milliseconds; VLBA timestamps have 10^-4 resolution
    uint8_t            header[8];
    uint32_t           word[2];
    uint8_t*           wptr = (uint8_t*)&word[0];
    uint8_t*           frame8  = (uint8_t *)framedata;
    uint16_t*          frame16 = (uint16_t *)framedata;
    uint32_t*          frame32 = (uint32_t *)framedata;
    uint64_t*          frame64 = (uint64_t *)framedata;
    unsigned int       crc;
    const unsigned int ntrack = hdr->ntrack;

    mjd = 40587 + (ts.tv_sec / 86400);
    sec = ts.tv_sec % 86400;

    // TMJD + Integer seconds
    word[0] = 0;
    word[0] |= ((sec / 1) % 10) << 0;
    word[0] |= ((sec / 10) % 10) << 4;
    word[0] |= ((sec / 100) % 10) << 8;
    word[0] |= ((sec / 1000) % 10) << 12;
    word[0] |= ((sec / 10000) % 10) << 16;
    word[0] |= ((mjd / 1) % 10) << 20;
    word[0] |= ((mjd / 10) % 10) << 24;
    word[0] |= ((mjd / 100) % 10) << 28;

    // fractional seconds + crc
    // from nano (10^-9) to 10^-4 = 10^5 
    dms = ts.tv_nsec / 100000;
    word[1] = 0;
    word[1] |= ((dms / 1) % 10) << 16;
    word[1] |= ((dms / 10) % 10) << 20;
    word[1] |= ((dms / 100) % 10) << 24;
    word[1] |= ((dms / 1000) % 10) << 28;

    crc = crc16_vlba((unsigned char*)&word[2], 8);
    word[1] |= (crc & 0xffff);

    // Endian conversion - we stole the Mk5B header generation
    // which generates the header in the other byteorder
    for(unsigned int byte=0, widx=0; byte<8; byte++, widx=byte/4)
        header[byte] = wptr[ (widx*4) + 3 - (byte%4) ];

    switch (ntrack) {
    case 8:
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame8[(i * 8) + j + 32] = ~0;
                else
                    frame8[(i * 8) + j + 32] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame16[(i * 8) + j + 32] = ~0;
                else
                    frame16[(i * 8) + j + 32] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame32[(i * 8) + j + 32] = ~0;
                else
                    frame32[(i * 8) + j + 32] = 0;
            }
        }
        break;
    case 64:
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) {
                if (header[i] & (1 << (7 - j)))
                    frame64[(i * 8) + j + 32] = ~0;
                else
                    frame64[(i * 8) + j + 32] = 0;
            }
        }
        break;
    default:
        throw invalid_number_of_tracks();
    }
}

void encode_mk5b_timestamp(unsigned char* framedata,
                           const struct timespec ts,
                           const headersearch_type* const hdr) {
    int                mjd, sec;
    long               dms; // deci-milliseconds; VLBA timestamps have 10^-4 resolution
    long               frametime_ns;
    uint32_t*          word = (uint32_t *)framedata;
    unsigned int       crc, framenr;
    const unsigned int ntrack       = hdr->ntrack;
    const unsigned int trackbitrate = hdr->trackbitrate;

    mjd = 40587 + (ts.tv_sec / 86400);
    sec = ts.tv_sec % 86400;

    // Frames per second -> framenumber within second
    // Mk5B framesize == 10000 bytes == 80000 bits == 8.0e4 bits
    // 1s = 1.0e9 ns
    frametime_ns = (long)((double)8.0e13/(double)(ntrack*trackbitrate));
    framenr      = ts.tv_nsec/frametime_ns;
    word[1] = (framenr & 0x7fff);

    // TMJD + Integer seconds
    word[2] = 0;
    word[2] |= ((sec / 1) % 10) << 0;
    word[2] |= ((sec / 10) % 10) << 4;
    word[2] |= ((sec / 100) % 10) << 8;
    word[2] |= ((sec / 1000) % 10) << 12;
    word[2] |= ((sec / 10000) % 10) << 16;
    word[2] |= ((mjd / 1) % 10) << 20;
    word[2] |= ((mjd / 10) % 10) << 24;
    word[2] |= ((mjd / 100) % 10) << 28;

    // fractional seconds + crc
    // from nano (10^-9) to 10^-4 = 10^5 
    dms = ts.tv_nsec / 100000;
    word[3] = 0;
    word[3] |= ((dms / 1) % 10) << 16;
    word[3] |= ((dms / 10) % 10) << 20;
    word[3] |= ((dms / 100) % 10) << 24;
    word[3] |= ((dms / 1000) % 10) << 28;

    word[2] = htonl(word[2]);
    word[3] = htonl(word[3]);
    crc = crc16_vlba((unsigned char*)&word[2], 8);
    word[2] = ntohl(word[2]);
    word[3] = ntohl(word[3]);
    word[3] |= (crc & 0xffff);
}

void encode_vdif_timestamp(unsigned char* framedata,
                           const struct timespec ts,
                           const headersearch_type* const hdr) {
    // Before doing anything, check this and bail out if necessary -
    // we want to avoid dividing by zero
    if( hdr->trackbitrate==0 )
        throw invalid_number_of_tracks();
    if( hdr->ntrack==0 )
        throw invalid_track_bitrate();

    struct tm           klad;
    struct vdif_header* vdif_hdr = (struct vdif_header*)framedata;

    ::gmtime_r(&ts.tv_sec, &klad);
    const int epoch    = (klad.tm_year + 1900 - 2000)*2 + (klad.tm_mon>=6);

    // Now set the zero point of that epoch, 00h00m00s on the 1st day of
    // month 0 (Jan) or 6 (July)
    klad.tm_hour  = 0;
    klad.tm_min   = 0; 
    klad.tm_sec   = 0;
    klad.tm_mon   = (klad.tm_mon/6)*6;
    klad.tm_mday  = 1;
    const time_t  tm_epoch = ::mktime(&klad);
    unsigned int  chunk_duration_ns   = (unsigned int)((((double)hdr->payloadsize * 8)/((double)hdr->ntrack*(double)hdr->trackbitrate))*1.0e9);

    vdif_hdr->legacy          = (hdr->frameformat==fmt_vdif_legacy);
    vdif_hdr->data_frame_len8 = (unsigned int)(((hdr->payloadsize+(vdif_hdr->legacy?16:32))/8) & 0x00ffffff);
    vdif_hdr->ref_epoch       = (unsigned char)(epoch & 0x3f);
    vdif_hdr->epoch_seconds   = (unsigned int)((ts.tv_sec - tm_epoch) & 0x3fffffff);
    vdif_hdr->data_frame_num  = ts.tv_nsec/chunk_duration_ns;

    return;
}




template<bool strip_parity> timespec mk4_frame_timestamp(
        unsigned char const* framedata, const unsigned int track,
        const unsigned int ntrack, const unsigned int trackbitrate,
        decoderstate_type* ) {
    unsigned char      timecode[8];

    // In Mk4 we first have 8 bytes aux data 4 bytes 
    // of 0xff (syncword) per track (==12 bytes) and 
    // only then the actual 8-byte timecode starts.
    // At this point we don't want to decode aux+syncword so
    // let's skip that shit alltogether (== 12byte * ntrack offset).
    if (strip_parity) {
        headersearch_type::extract_bitstream<strip_parity>(
            &timecode[0],
            track, ntrack, sizeof(timecode)*8,
            framedata + 12*ntrack * 9 / 8);
    }
    else {
        headersearch_type::extract_bitstream<strip_parity>(
            &timecode[0],
            track, ntrack, sizeof(timecode)*8,
            framedata + 12*ntrack);
    }

    return decode_mk4_timestamp(&timecode[0], trackbitrate);
}

template<bool strip_parity> timespec vlba_frame_timestamp(
        unsigned char const* framedata, const unsigned int track,
        const unsigned int ntrack, const unsigned int,
        decoderstate_type* ) {
    unsigned char      timecode[8];

    // Not quite unlike Mk4, only there's only the syncword (==
    // 4 bytes of 0xff) per track to skip.
    if (strip_parity) {
        headersearch_type::extract_bitstream<strip_parity>(
            &timecode[0],
            track, ntrack, sizeof(timecode)*8,
            framedata + 4*ntrack * 9 / 8);
    }
    else {
        headersearch_type::extract_bitstream<strip_parity>(
            &timecode[0],
            track, ntrack, sizeof(timecode)*8,
            framedata + 4*ntrack);
    }
    return decode_vlba_timestamp<vlba_tape_ts>((vlba_tape_ts const*)&timecode[0]);
}

timespec mk5b_frame_timestamp(unsigned char const* framedata, const unsigned int,
                              const unsigned int, const unsigned int,
                              decoderstate_type* state) {
    struct m5b_state {
        time_t          second;
        unsigned int    frameno;
        bool            wrap;
    };
    // In Mk5B there is no per-track header. Only one header (4 32bit words) for all data
    // and it's at the start of the frame. The timecode IS a VLBA style
    // timecode which starts at word #2 in the header

    // Start by decoding the VLBA timestamp - this has at least the integer
    // second value
    timespec            vlba = decode_vlba_timestamp<mk5b_ts>((mk5b_ts const *)(framedata+8));
    m5b_state*          m5b_s  = (m5b_state*)&state->user[0];
    m5b_header*         m5b_h  = (m5b_header*)framedata;
    unsigned int        frameno  = m5b_h->frameno;
#ifdef GDBDEBUG
    long                prevnsec = 0;
#endif
    // Use the frame#-within-seconds to enhance accuracy
    // If we detect a wrap in the framenumber within the same integer
    // second: add max Mk5B framenumber
    // to the actual framenumber
    m5b_s->wrap = (vlba.tv_sec==m5b_s->second && // only if still in same integer second
                   (m5b_s->wrap ||               // seen previous wrap?
                    frameno < m5b_s->frameno));  // or do we see one now?
    if( m5b_s->wrap )
        frameno += 0x7fff; // 15 bits is maximum framenumber

    // consistency check?
    if( frameno>(unsigned int)state->framerate ) {
        std::cerr << "MARK5B FRAMENUMBER OUT OF RANGE!" << std::endl;
    } else {
        // replace the subsecond timestamp with one computed from the 
        // frametime
#ifdef GDBDEBUG
        prevnsec = vlba.tv_nsec;
#endif
        vlba.tv_nsec = (long)(state->frametime * frameno);
    }

#ifdef GDBDEBUG
    DEBUG(4, "mk5b_frame_timestamp: framerate/dur = " << state->framerate << "/" << state->frametime << std::endl <<
             "    VLBA -> " << vlba.tv_sec << "s + " << ((double)prevnsec / 1.0e9) << "s" << std::endl << 
             "    frameno hdr/used: " << m5b_h->frameno << "/" << frameno << std::endl <<
             "      => new nanosec: " << ((double)vlba.tv_nsec / 1.0e9) << std::endl);
#endif

    // update state
    m5b_s->second  = vlba.tv_sec;
    m5b_s->frameno = m5b_h->frameno;
    return vlba;
}

struct timespec  vdif_frame_timestamp(unsigned char const* framedata,
                                      const unsigned int /*track*/,
                                      const unsigned int ntrack,
                                      const unsigned int trackbitrate,
                                      decoderstate_type* /*state*/) {
    double                    frameduration;
    struct tm                 tm;
    struct timespec           rv = { 0, 0 };
    struct vdif_header const* hdr = (struct vdif_header const*)framedata;

    ASSERT2_COND(trackbitrate>0, SCINFO("Cannot do VDIF timedecoding when bitrate == 0"));

    // Get integer part of the time
    tm.tm_wday   = 0;
    tm.tm_isdst  = 0;
    tm.tm_yday   = 0;
    tm.tm_mday   = 1;
    tm.tm_hour   = 0;
    tm.tm_min    = 0;
    tm.tm_sec    = (int)hdr->epoch_seconds;
    tm.tm_year   = 100 + hdr->ref_epoch/2;
    tm.tm_mon    = 6 * (hdr->ref_epoch%2);

    rv.tv_sec    = ::mktime(&tm);

    // In order to get the actual subsecond timestamp we must do some
    // computation ...
    // dataframe_length is in units of 8 bytes, INCLUDING the header.
    // So depending on legacy or not we must subtract 16 or 32 bytes
    // for the header.
    frameduration = ((((hdr->data_frame_len8 * 8.0 /*bytes*/) - (hdr->legacy?16:32)) * 8.0 /* in bits now */) /
                    (double)(ntrack*trackbitrate)) * 1.0e9 /* in nanoseconds now*/;

    rv.tv_nsec    = (long)(hdr->data_frame_num * frameduration);
    return rv;
}


// ntrack only usefull if vlba||mark4
// [XXX] - if fmt_none becomes disctinct you may want/need to change this
//         default behaviour
const unsigned int st_tracks = 32;
#define MK4VLBA(fmt) \
    (fmt==fmt_mark4 || fmt==fmt_vlba)
#define IS_ST(fmt) \
    (fmt== fmt_mark4_st || fmt==fmt_vlba_st)
#define STRIP_ST(fmt) \
    (fmt == fmt_mark4_st ? fmt_mark4 : \
     (fmt == fmt_vlba_st ? fmt_vlba : fmt_unknown))
#define IS_VDIF(fmt) \
    (fmt==fmt_vdif || fmt==fmt_vdif_legacy)


#define SYNCWORDSIZE(fmt, n) \
    ((fmt==fmt_mark5b)?(sizeof(mark5b_syncword)):((MK4VLBA(fmt))?(n*4):0))
#define SYNCWORDSIZE_ST(fmt, n) \
    (IS_ST(fmt) ? SYNCWORDSIZE(STRIP_ST(fmt), st_tracks) * 9 / 8 : SYNCWORDSIZE(fmt,n))

#define SYNCWORDOFFSET(fmt, n) \
    ((fmt==fmt_mark4)?(8*n):0)
#define SYNCWORDOFFSET_ST(fmt, n) \
    (IS_ST(fmt) ? SYNCWORDOFFSET(STRIP_ST(fmt), st_tracks) * 9 / 8 : SYNCWORDOFFSET(fmt,n))

#define SYNCWORD(fmt) \
    ((fmt==fmt_mark5b)?(&mark5b_syncword[0]):(MK4VLBA(fmt)?(&mark4_syncword[0]):0))
#define SYNCWORD_ST(fmt) \
    (IS_ST(fmt) ? &st_syncword[0] : SYNCWORD(fmt))

#define PAYLOADSIZE(fmt, ntrk) \
    (MK4VLBA(fmt)?(2500*ntrk):((fmt==fmt_mark5b)?(10000):0))
#define PAYLOADSIZE_ST(fmt, ntrk) \
    (IS_ST(fmt) ? PAYLOADSIZE(STRIP_ST(fmt), st_tracks) * 9 / 8 : PAYLOADSIZE(fmt, ntrk))
#define PAYLOADSIZE_VDIF(fmt, ntrk, frsz) \
    (IS_VDIF(fmt) ? frsz : PAYLOADSIZE_ST(fmt, ntrk))

#define PAYLOADOFFSET_FOR_VDIF(fmt) \
    (fmt==fmt_vdif_legacy?16:32)
#define PAYLOADOFFSET(fmt, ntrk) \
    ((fmt==fmt_mark4)?(0):((fmt==fmt_mark5b)?(16):((fmt==fmt_vlba)?(12*ntrk):0)))
#define PAYLOADOFFSET_ST(fmt, ntrk) \
    (IS_ST(fmt) ? PAYLOADOFFSET(fmt, st_tracks) * 9 / 8 : PAYLOADOFFSET(fmt, ntrk))
#define PAYLOADOFFSET_VDIF(fmt, ntrk) \
    (IS_VDIF(fmt) ? PAYLOADOFFSET_FOR_VDIF(fmt) : PAYLOADOFFSET_ST(fmt, ntrk))

timedecoder_fn vlba_decoder_fn = &vlba_frame_timestamp<false>;
timedecoder_fn mark4_decoder_fn = &mk4_frame_timestamp<false>;
timedecoder_fn vlba_st_decoder_fn = &vlba_frame_timestamp<true>;
timedecoder_fn mark4_st_decoder_fn = &mk4_frame_timestamp<true>;

#define DECODERFN(fmt) \
    ((fmt==fmt_mark5b)?&mk5b_frame_timestamp: \
     ((fmt==fmt_vlba)?vlba_decoder_fn: \
      ((fmt==fmt_mark4)?mark4_decoder_fn:              \
       ((fmt==fmt_vlba_st)?vlba_st_decoder_fn: \
        ((fmt==fmt_mark4_st)?mark4_st_decoder_fn: \
         ((fmt==fmt_vdif || fmt==fmt_vdif_legacy)?vdif_frame_timestamp: \
          (timedecoder_fn)0))))))

#define ENCODERFN(fmt) \
    ((fmt==fmt_mark5b)?&encode_mk5b_timestamp: \
     ((fmt==fmt_vlba)?&encode_vlba_timestamp: \
      ((fmt==fmt_mark4)?&encode_mk4_timestamp: \
       ((fmt==fmt_vdif || fmt==fmt_vdif_legacy)?&encode_vdif_timestamp:((timeencoder_fn)0)))))

headercheck_fn  checkm4cuc   = &headersearch_type::check_mark4<const unsigned char*, false>;
headercheck_fn  checkm4cuc_st   = &headersearch_type::check_mark4<const unsigned char*, true>;
headercheck_fn  checkm5cuc   = &headersearch_type::check_mark5b<const unsigned char*>;
headercheck_fn  checkvlbacuc = &headersearch_type::check_vlba<const unsigned char*, false>;
headercheck_fn  checkvlbacuc_st = &headersearch_type::check_vlba<const unsigned char*, true>;

#define CHECKFN(fmt) \
    ((fmt==fmt_mark5b)?(checkm5cuc):\
     ((fmt==fmt_vlba)?(checkvlbacuc):\
      ((fmt==fmt_mark4)?(checkm4cuc):\
       ((fmt==fmt_vlba_st)?(checkvlbacuc_st):\
        ((fmt==fmt_mark4_st)?(checkm4cuc_st):\
         ((IS_VDIF(fmt))?(&headersearch_type::nop_check):((headercheck_fn)0)))))))

headersearch_type::headersearch_type():
    frameformat( fmt_unknown ), ntrack( 0 ),
    trackbitrate( 0 ), syncwordsize( 0 ),
    syncwordoffset( 0 ), headersize( 0 ),
    framesize( 0 ),
    payloadsize( 0 ),
    payloadoffset( 0 ),
    timedecoder( (timedecoder_fn)0 ), 
    timeencoder( (timeencoder_fn)0 ), 
    checker( (headercheck_fn)0 ),
    syncword( 0 )
{}


// Fill in the headersearch thingamabob according the parameters.
//
// Mark5B:
// * The syncwordsize + pattern are typical for mark5b
// * Mark5B diskframes have a fixed framesize, irrespective of number
//     of bitstreams recorded.
// * The syncword starts the frame, hence syncwordoffset==0
//     A total diskframe constist of 4 32bit words of header, followed
//     by 2500 32bit words of data, 32bits == 4 bytes
// * Following the syncword are 3 32bit words, making the
//     full headersize 4 times 32bit = 16 bytes
//
// MarkIV / VLBA 
// * The syncwordsize + pattern is equal between VLBA and Mk4: 
//     4 x ntrack bytes of 0xFF
// * In Mk4 the syncword starts after the AUX data (8 bytes/track),
//     in VLBA at the start of the frame (the AUX data is at the end of the frame)
// * total framesize is slightly different:
//     Mk4 is datareplacement (headerbits are written over databits)
//     VLBA is non-datareplacement
// * following the syncword are another 8 bytes of header. from
//     this we can compute the full headersize
headersearch_type::headersearch_type(format_type fmt, unsigned int tracks, unsigned int trkbitrate, unsigned int vdifframesize):
    frameformat( fmt ),
    ntrack( tracks ),
    trackbitrate( trkbitrate ),
    syncwordsize( SYNCWORDSIZE_ST(fmt, tracks) ),
    syncwordoffset( SYNCWORDOFFSET_ST(fmt, tracks) ),
    headersize( ::headersize(fmt, tracks) ),
    framesize( ::framesize(fmt, tracks, vdifframesize) ),
    payloadsize( PAYLOADSIZE_VDIF(fmt, tracks, vdifframesize) ),
    payloadoffset( PAYLOADOFFSET_VDIF(fmt, tracks) ),
    timedecoder( DECODERFN(fmt) ),
    timeencoder( ENCODERFN(fmt) ),
    checker( CHECKFN(fmt) ),
    syncword( SYNCWORD_ST(fmt) ),
    state( ntrack, trackbitrate, payloadsize )
{
    // Finish off with assertions ...
    unsigned int mintrack = (frameformat==fmt_mark5b?1:8);

    if(MK4VLBA(frameformat) || frameformat==fmt_mark5b) {
        ASSERT2_COND( ((ntrack>=mintrack) && (ntrack<=64) && (ntrack & (ntrack-1))==0),
                      SCINFO("ntrack (" << ntrack << ") is NOT a power of 2 which is >=" << mintrack << " and <=64") );
    }
    if(IS_ST(frameformat)) {
        ASSERT2_COND( ntrack==32,
                      SCINFO("ntrack (" << ntrack << ") is NOT 32 while mode is straight through") );
    }
    if(IS_VDIF(frameformat)) {
        ASSERT2_COND( (payloadsize%8)==0,
                      SCINFO("The VDIF dataarraysize is not a multiple of 8") );
    }
    // Should we check trackbitrate for sane values?
}

#define REFACTOR_ALWAYS(value, factor) ((factor<0)?(value/::abs(factor)):(value*::abs(factor)))
#define REFACTOR_DIVIDE(value, factor) ((factor<0)?(value/::abs(factor)):(value))

headersearch_type::headersearch_type(const headersearch_type& other, int factor):
    frameformat( other.frameformat ),
    ntrack( REFACTOR_DIVIDE(other.ntrack, factor) ),
    trackbitrate( other.trackbitrate ),
    syncwordsize( 0 ),
    syncwordoffset( 0 ),
    headersize( 0 ),
    framesize( 0 ),
    payloadsize( REFACTOR_ALWAYS(other.payloadsize, factor) ),
    payloadoffset( 0 ),
    timedecoder( 0 ),
    timeencoder( 0 ),
    checker( 0 ),
    syncword( 0 )
{ ASSERT2_COND( ntrack>0 && trackbitrate>0 && payloadsize>0,
                SCINFO("cannot " << ((factor<0)?("divide"):("multiply")) << " header by " << ::abs(factor) << endl) ); }


// scale the number of tracks either by the amount of chunks the input header is divided
// (if the output-nr-of-tracks is 0, which is stored in the imaginary part of the cplx number), or
// by the actual output-nr-of-tracks [cplxnumber.imag() > 0].
// From the actual amount of tracks we scale the payload as well
headersearch_type::headersearch_type(const headersearch_type& other, const complex<unsigned int>& factor):
    frameformat( other.frameformat ),
    ntrack( ((factor.imag()==0))?(other.ntrack/factor.real()):(factor.imag()) ),
    trackbitrate( other.trackbitrate ),
    syncwordsize( 0 ),
    syncwordoffset( 0 ),
    headersize( 0 ),
    framesize( 0 ),
    payloadsize( (other.ntrack==0)?((unsigned int)-1):((other.payloadsize * ntrack)/other.ntrack) ),
    payloadoffset( 0 ),
    timedecoder( 0 ),
    timeencoder( 0 ),
    checker( 0 ),
    syncword( 0 )
{ ASSERT2_COND( ntrack>0 && trackbitrate>0 && payloadsize>0 && factor.imag()<=other.ntrack && payloadsize!=(unsigned int)-1,
                SCINFO("cannot divide header by complex " << factor << endl) ); }


// This is a static function! No 'this->' available here.
template<bool strip_parity> void headersearch_type::extract_bitstream(
        unsigned char* dst,
        const unsigned int track, const unsigned int ntrack, unsigned int nbit,
        unsigned char const* frame) {
    // We do not recompute all shifted bitpositions each time
    static const unsigned int  msb      = 7; // most significant bit number, for unsigned char that is
    static const unsigned char mask[]   = { 0x1,  0x2,  0x4,  0x8,  0x10,  0x20,  0x40,  0x80};
    static const unsigned char unmask[] = {~0x1, ~0x2, ~0x4, ~0x8, ~0x10, ~0x20, ~0x40, 0x7F};
    // assert that the requested track is within our bounds
    if( track>=ntrack )
        throw invalid_track_requested();
    // (1) loopvariables, for this once keep them outside the loop and
    //     initialize them already:
    //        start reading from byte 'track/8' [==relative offset]
    //        start writing at the most significant bit in byte 0
    // (2) precompute the stepsize in bytes and the mask for the bit-within-byte
    //     that we want to extract 
    unsigned int        srcbyte( track/8 );          // (1)
    unsigned int        dstbyte( 0 ), dstbit( msb ); // (1)
    const unsigned int  bytes_per_step( ntrack/8 );  // (2)
    const unsigned char bitmask( mask[track%8] );    // (2)

    unsigned int counter = 0;
    
    // and off we go!
    while( nbit-- ) {
        // srcbyte & bitmask-for-sourcebit yields '0's for all bits that we're not
        // interested in and '0' or '1' for the bit we are interested in,
        // depending on its value. we add the EXPLICIT test for "!=0" in
        // order for the bithack below to work. <--- important, just so you know.
        // We use the value of the bit in the sourcebyte as "flag" wether
        // or not to transfer the correspoding destination bit into
        // the destination position.
        const unsigned int  f( (frame[srcbyte]&bitmask)!=0 );

        // Now we must transfer that value to dstbit in dstbyte.
        // Use a trick from the Bit Twiddling Hacks page
        //    http://graphics.stanford.edu/~seander/bithacks.html
        // 
        // Quoth '../bithacks.html#ConditionalSetOrClearBitsWithoutBranching'
        //
        // Conditionally set or clear bits without branching
        //    bool f;         // conditional flag
        //    unsigned int m; // the bit mask
        //    unsigned int w; // the word to modify:  if (f) w |= m; else w &= ~m; 
        //
        //  w ^= (-f ^ w) & m;
        //
        // OR, for superscalar CPUs:
        //  w = (w & ~m) | (-f & m);
        //
        // On some architectures, the lack of branching can more than make up for
        // what appears to be twice as many operations. For instance, informal 
        // speed tests on an AMD Athlon™ XP 2100+ indicated it was 5-10% faster.
        // An Intel Core 2 Duo ran the superscalar version about 16% faster
        // than the first. Glenn Slayden informed me of the first expression on
        // December 11, 2003. Marco Yu shared the superscalar version with me on
        // April 3, 2007 and alerted me to a typo 2 days later. adf
        // 
        //
        // ################### Note by HV 9 Jun 2010 #######################
        //
        // The hack is critically dependant on the following:
        // IF   f == true (logically) - ie the bits in m must be set in w -
        // THEN it MUST have _exactly_ one bit set AND it MUST be the LSB.
        // ie: f==true (logically) must imply
        //     f==0x1  (in machine representation)
        // The standard C/C++ "!=" and "==" operators happen to produce 
        // a result just like that!
        //
        // Also: we've replaced "~m" with unmask[] and "m" with mask[]
        //       since we've precomputed both the mask and the inverse mask,
        //       hoping that lookup is (marginally) faster than computing
        //       the bitwise not and the mask for each <dstbit> in each
        //       iteration of this loop.
        dst[dstbyte] = (unsigned char)((dst[dstbyte] & unmask[dstbit]) | (-f & mask[dstbit]));

        // Update loopvariables.
        srcbyte += bytes_per_step;
        if ( strip_parity && ( (++counter % 8) == 0 )) {
            srcbyte += bytes_per_step;
        }
        // first test then decrement since we must also process dstbit==0
        if( (dstbit--)==0 ) {
            // filled up another byte in dst, continue to write into
            // the most significant bit of the next byte
            dstbit = msb;
            dstbyte++;
        }
    }
    return;
}

// Call on the actual timedecoder, adding info where necessary
timespec headersearch_type::decode_timestamp( unsigned char const* framedata, const unsigned int track ) const {
    return timedecoder(framedata, track, this->ntrack, this->trackbitrate, &this->state);
}

void headersearch_type::encode_timestamp( unsigned char* framedata, const struct timespec ts ) const {
    return timeencoder(framedata, ts, this);
}

bool headersearch_type::check( unsigned char const* framedata, bool checksyncword, unsigned int track ) const {
    return (this->*checker)(framedata, checksyncword, track);
}

bool headersearch_type::nop_check(unsigned char const*, bool, unsigned int) const {
    return true;
}

// CRC business
//typedef unsigned short CRCtype;

template <unsigned int CRCWidth, unsigned int Key>
struct crctable_type {
    // construct the crc table for a CRC of given Width and generating
    // polynomial key
    crctable_type() {
        const uint64_t polyorderbit( 1<<CRCWidth );
        ASSERT_COND( CRCWidth<=32 && CRCWidth>=8 );
        // for all possible byte values ..
        for(unsigned int i=0; i<256; ++i) { 
            // the first shift in the for loop will make room for the poly to be XOR'ed
            // (we process 8 bits/iteration so we must make room for that)
            int reg = i<<(CRCWidth-8); 
            // for all bits in a byte
            for(unsigned int j=0; j<8; ++j) { 
                reg <<= 1;
                if( reg&polyorderbit )
                    reg ^= Key;
            }
            crc_table[i] = reg;
        }

        mask = polyorderbit - 1;
    }
    // Overload the functioncall operator. It takes a pointer
    // some databytes and the number of bytes to perform the CRC
    // computation over.
    unsigned int operator()(const unsigned char* data, unsigned int n) const {
        unsigned int  crc_register = 0;
        unsigned char top;
        while( n-- ) {
            top          = (unsigned char)(crc_register>>(CRCWidth-8));
            crc_register = ((crc_register<<8)+*data++) ^ crc_table[top];
        }
        return crc_register & mask;
    }
    static unsigned int crc_table[];
    static unsigned int mask;
};
template <unsigned int CRCWidth, unsigned int Key>
unsigned int crctable_type<CRCWidth, Key>::crc_table[256];
template <unsigned int CRCWidth, unsigned int Key>
unsigned int crctable_type<CRCWidth, Key>::mask;



// compute CRC12 (a la Mark4) on n bytes of data from idata
unsigned int crc12_mark4(const unsigned char* idata, unsigned int n) {
    // (CRC12) 100000001111 [generator polynomial]
    static const crctable_type<12, 0x80f> crc12t;

    return crc12t(idata, n); 
}

unsigned int crc16_vlba(const unsigned char* idata, unsigned int n) {
    static const crctable_type<16, 0x8005> crc16t;

    return crc16t(idata, n);
}

#if 0
// implicit definition of CRC12 for Mark4. idata must point to
// a buffer of at least 20 bytes of trackdata; the full header.
// (actually, the crc is computed over 148 bits).
unsigned int crc12_mark4(unsigned char* idata) {
    return generic_crc_check(idata, 148, 007003, 12);
}

// CRC16 for VLBA is computed only over the timecode, directly 
// following the syncword. make sure idata points at the start
// of the 6 bytes of timecode (48bits). Mark5B uses the same CRC
// as VLBA.
unsigned int crc16_vlba(unsigned char* idata) {
    return generic_crc_check(idata, 48, 040003, 16);
}

// CRC function. Copied verbatim (save lay-out) from JBall "Parse5A.c".
// This can be used to compute CRC12 (Mark4) and CRC16(VLBA/Mark5B),
// depending on the input parameters.
unsigned int generic_crc_check(unsigned char* idata, unsigned int len,
                               unsigned int mask, unsigned int cycl) {
    /* Calculate the CRC (cyclic redundancy check) of the bit stream 
     * in idata of len bits (sic) using mask and cycl.  Examples:  
     * mask = 040003 (octal) cycl = 16 for label, len = 64 bits, 
     * for VLBA frame header, len = 48 bits.  Or mask = 007003 (octal) 
     * cycl = 12 for Mark-4 frame header, len = 148 bits.  Return the 
     * answer.  Mostly copied from ARW's FORTRAN subroutine CRCC(). 
     * Revised:  2005 March 10, JAB */ 
    unsigned int idbit; /* Count bits from 1 */ 
    unsigned int istate = 0; 
    unsigned int q, ich, icb; 
    const unsigned int clear_lsb_mask( ~0x1 );

    for (idbit = 1; idbit <= len; idbit++) { /* Each of len bits */ 
        q = istate & 1; /* Output bit */ 
        /* In ARW's original, bits are numbered 1 to 16 left to right (!) 
         * in a 16-bit word.  This curious numbering is perhaps because the 
         * bytes are numbered left to right in a word in hppa.  We need to 
         * pick out the same bit given 8-bit chars.  We number bits right 
         * to left in a char as usual but process them in inverse order. */ 
        ich = (idbit-1)/8; /* Char number, 0 to (len-1)/8 */ 
        icb = 7-(idbit-1)%8; /* Bit number within a char, 0 to 7 */ 
        if ((((idata[ich] >> icb) & 1) ^ q) == 0) /* Feedback value */ 
//            istate &= -2; /* Clear LSB */ 
            istate &= clear_lsb_mask; /* Clear LSB */ 
        else { 
            istate ^= mask; /* Invert bits with 1s in mask */ 
            istate |= 1; /* Set LSB */ } 
            istate = istate >> 1 | (istate & 1) << cycl - 1; 
            /* Right rotate one bit */ 
    } /* End of for idbit */ 
    return istate; 
}

#endif



// The syncword binary patterns

// enough for 64 tracks of Mark4/VLBA syncword
unsigned char mark4_syncword[]  = {
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        };

unsigned char st_syncword[]  = {
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0x00, 0x00, 0x00, 0x00,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0x00, 0x00, 0x00, 0x00,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0x00, 0x00, 0x00, 0x00,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,

                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff,
                        0x00, 0x00, 0x00, 0x00
                        };
