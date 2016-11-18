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
#include <stringutil.h>
#include <timezooi.h>
#include <regular_expression.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <utility>    // for make_pair()

#include <arpa/inet.h>

//#ifdef GDBDEBUG
#if 1
#include <dosyscall.h>
#include <evlbidebug.h>
#endif

using std::ostream;
using std::ostringstream;
using std::istringstream;
using std::string;
using std::endl;
using std::complex;
using std::make_pair;


DEFINE_EZEXCEPT(headersearch_exception)

const uint64_t headersearch_type::UNKNOWN_TRACKBITRATE;

// The check flag stuff
bool do_strict_map_init( void ) {
    typedef headersearch::strict_type::flag_descr_type fdescr;

    headersearch::strict_type::flag_map_type   sfmap; // s(trict)f(lag)map

    if( headersearch::strict_type::get_flag_map().empty()==false )
        return false;

    // 1) Fill in the mapping of enum => actual bitwise flags

    // Bit 0x1   == "do check syncword"
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_syncword, fdescr(0x1, "Check SYNCWORD"))).second, 
              headersearch_exception );
    // Bit 0x2  == "do check CRC"
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_crc, fdescr(0x2, "Check CRC"))).second, 
              headersearch_exception );

    // Bit 0x4  == "check for consistency"
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_consistent, fdescr(0x4, "Check consistency"))).second, 
              headersearch_exception );

    // Bit 0x8  == "be verbose"
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_verbose, fdescr(0x8, "Be verbose"))).second, 
              headersearch_exception );

    // Bit 0x10 == "be strict"
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_strict, fdescr(0x10, "Be strict"))).second, 
              headersearch_exception );

    // Bit 0x20 == allow DBE Mark5B format
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_allow_dbe, fdescr(0x20, "Allow DBE Mark5B time stamps"))).second, 
              headersearch_exception );

    // Bit 0x40 == do not throw upon invalid time stamp
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_nothrow,
                                     fdescr(0x40, "Do not throw on invalid time stamp but return {0,0}"))).second, 
              headersearch_exception );

    // The "default" maps to all of those, apart from being verbose
    EZASSERT( sfmap.insert(make_pair(headersearch::chk_default, fdescr(0x1|0x2|0x4|0x10, "Default set"))).second, 
              headersearch_exception );

    // 2) Store this map in the static data member of the flagset
    headersearch::strict_type::set_flag_map( sfmap );

    return true;
}

// trigger initialization!
bool strictmapinit = do_strict_map_init();



//////////////////////////////////////////////////////////////////////
//
//   The decoder_state object
//
//////////////////////////////////////////////////////////////////////


offset_lut_type compute_offset_lut(const samplerate_type& frametime) {
    // frametime is numerator() / denominator() seconds. So our lookup table
    // will be of size numerator(). std::vector()'s constructor takes a
    // size_t so we better make sure we stay within the limits of that type.
    const uint64_t   period_u64 = frametime.numerator();
    const uint64_t   maxTime    = std::numeric_limits<time_t>::max();
    const uint64_t   maxSize    = std::numeric_limits<size_t>::max();

    DEBUG(4, "compute_offset_lut: frametime = " << frametime << endl);
    EZASSERT2(period_u64<=std::min(maxTime, maxSize), headersearch_exception,
              EZINFO("the frame period " << period_u64 << " is longer than a time_t or size_t can represent, " <<
                      maxTime << " and " << maxSize << ", respectively"));

    // Now we can safely create variables of the appropriate types
    const time_t      period( (time_t)period_u64 );
    offset_lut_type   lut( period );
    highrestime_type  tm( (time_t)0, frametime );
    highresdelta_type dt( frametime.as<highresdelta_type>() );

    for(time_t t0 = 0; t0<period; t0++) {
        // The fraction will be automatically reduced by boost::rational<>
        // but we need to compute the modulo of the numerator of the
        // fraction where the denominator is the same as the original frame time
        const subsecond_type& ss( tm.tv_subsecond );
        const uint64_t        tmp_num = ss.numerator() * (frametime.denominator() / ss.denominator());
        const unsigned int    modulo  = (unsigned int)(tmp_num % period_u64);
        // Compute the module for the current second-within-period and
        // write the current integer offset there
        lut[ modulo ] = -tm.tv_sec;

        // compute first frame of the next second
        tm += dt * (boost::rational_cast<unsigned int>(((t0+1) - tm)/dt) + 1);
    }
    return lut;
}


decoderstate_type::decoderstate_type():
    framerate( 0 ), frametime( 0 )
{ 
    // clear user data
    ::memset(&user[0], 0, sizeof(user));
}

// When compiled with debugging information we do ouput more info
#ifdef GDBDEBUG
bool valid_samplerate(const samplerate_type& sr) {
    DEBUG(-1, "valid_samplerate_test: " << sr << endl);
    DEBUG(-1, "    == 0                   : " << (sr==0) << endl);
    DEBUG(-1, "    == UNKNOWN_TRACKBITRATE: " << (sr==headersearch_type::UNKNOWN_TRACKBITRATE) << endl);
    DEBUG(-1, "     ==> " << !(sr==headersearch_type::UNKNOWN_TRACKBITRATE || sr==0) << endl);
    return !(sr==headersearch_type::UNKNOWN_TRACKBITRATE || sr==0);
}
#define VALID_SAMPLERATE(x) (valid_samplerate(x))

#else

#define VALID_SAMPLERATE(x) (!(x==headersearch_type::UNKNOWN_TRACKBITRATE || x==0))

#endif

decoderstate_type::decoderstate_type( unsigned int ntrack, const samplerate_type& trackbitrate, unsigned int payloadsz ):
    framerate( (payloadsz && VALID_SAMPLERATE(trackbitrate)) ? ((ntrack*trackbitrate)/(payloadsz * 8)) : 0 ),
    frametime( (ntrack && VALID_SAMPLERATE(trackbitrate)) ? (samplerate_type(payloadsz * 8)/(ntrack*trackbitrate)) : 0 ),
    // framerate is numerator() frames per denominator() seconds. Only
    // need to compute the offset lookup table if denominator()!=1
    offset_lut( (VALID_SAMPLERATE(frametime) && VALID_SAMPLERATE(framerate) && framerate.denominator()!=1) ?
                ::compute_offset_lut(frametime) : offset_lut_type() )
{
    ::memset(&user[0], 0, sizeof(user));
}


// Local utilities

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
unsigned int framesize(format_type fmt, unsigned int ntrack, unsigned int vdifpayloadsize) {
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
            return hsize + vdifpayloadsize;
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
        FMTKEES(os, fmt_mark5b,      "Mark5B"); // capitals for Mark5A compatibility
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
    os << "[" << h.ntrack << "x" << h.frameformat << "@";
    if (h.trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) {
        os << " Invalid bitrate ";
    }
    else {
        os << h.trackbitrate << "bps ";
    }
    os << "SYNC: " << h.syncwordsize << "@" << h.syncwordoffset << " "
       << "HDR: " << h.headersize << "/FRM: " << h.framesize << " "
       << "PAY: " << h.payloadsize << "@" << h.payloadoffset
       << "]";
    return os;
}

headersearch_type operator/(const headersearch_type& h, unsigned int factor) {
    EZASSERT2( factor>0, headersearch_exception, EZINFO("Cannot divide frame into 0 pieces") );
    return headersearch_type(h, -1*(int)factor);
}
headersearch_type operator/(const headersearch_type& h, const complex<unsigned int>& factor) {
    EZASSERT2( factor.real()>0, headersearch_exception, EZINFO("Cannot divide frame into 0 pieces") );
    return headersearch_type(h, factor);
}
headersearch_type operator*(const headersearch_type& h, unsigned int factor) {
    EZASSERT2( factor>0, headersearch_exception, EZINFO("Cannot multiply frame by 0") );
    return headersearch_type(h, (int)factor);
}
headersearch_type operator*(unsigned int factor, const headersearch_type& h) {
    EZASSERT2( factor>0, headersearch_exception, EZINFO("Cannot multiply frame by 0") );
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
highrestime_type decode_mk4_timestamp(unsigned char const* trackdata, const samplerate_type& trackbitrate,
                                      const headersearch::strict_type strict, decoderstate_type* decoder) {
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
    struct tm        m4_time;
    mk4_ts const*    ts = (mk4_ts const*)trackdata;
    highrestime_type frametime;

    ::memset(&m4_time, 0, sizeof(struct tm));

#ifdef GDBDEBUG
    DEBUG(4, "mk4_ts: attempt decoding, trackbitrate=" << trackbitrate << " frametime=" << decoder->frametime << endl);
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
    // HV: 05 Nov 2015 OK, now it's really just ticks of a clock running at
    //                 1000 ticks per second!
    frametime.tv_subsecond = subsecond_type(100*ts->SS2 + 10*ts->SS1 + ts->SS0, 1000);
#ifdef GDBDEBUG
    DEBUG(4, "mk4_ts: " 
             << m4_time.tm_year << " "
             << m4_time.tm_mday << " "
             << m4_time.tm_hour << "h"
             << m4_time.tm_min << "m"
             << m4_time.tm_sec << "s "
             << "+" << frametime.tv_subsecond << "sec "
             << "[" << (int)ts->SS2 << ", " << (int)ts->SS1 << ", " << (int)ts->SS0 << "]" << endl);
#endif

    // Last digit of '4' or '9' is invalid for _any_ track bit rate
    const uint8_t  ss0     = ts->SS0;

    // Check what to do on finding an illegal last digit
    if( ss0==4 || ss0==9 ) {
        ostringstream  msg;

        msg << "Invalid Mark4 timecode: last digit is "
            << (unsigned int)ss0
            << " which may not occur with ANY trackbitrate.";
        // Depending on flags in the strict value, do things
        if( strict & headersearch::chk_verbose )
            std::cerr << msg.str() << endl;
        if( strict & headersearch::chk_strict ) {
            if( strict & headersearch::chk_nothrow ) {
                frametime = highrestime_type();
                return frametime;
            }
            EZASSERT2( !(ss0==4 || ss0==9), headersearch_exception, EZINFO(msg.str()) );
        }
    }

    // depending on the actual trackbitrate we may have to apply a
    // correction - see Mark4 MEMO 230(.3)
    if( trackbitrate==8000000 || trackbitrate==16000000 ) {
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
        //
        //  Update 05 Nov 2015: 0.25 ms = 250 microseconds
        //                              = 25 * 10^-5
        //                              = 25 / 100000
#ifdef GDBDEBUG
        const subsecond_type  oldss( frametime.tv_subsecond );
#endif
        frametime.tv_subsecond += ((ss0 % 5) * subsecond_type(25, 100000));
#ifdef GDBDEBUG
        DEBUG(4, "mk4_ts: adding " << ((ss0%5)*250) << "usec => " << oldss << " became " << frametime.tv_subsecond << endl);
#endif
    }

    // Given the track bit rate we can compute the actual frame duration in
    // nano seconds. Then we compare the frame time stamp modulo frame
    // duration to verify that the frame time stamp is consistent with
    // the track bit rate.
    // But only if strict checking is required, obviously
    // We do the computations in uint64_t arithmetic because we
    // have to inspect the full seconds + milliseconds frame time
    // stamp in nano seconds because for the lowest two (valid) MarkIV
    // track data rates (0.125Mbps and 0.250Mbps) the frame durations are
    // such that there are NOT an integer amount of frames per second but
    // there are an integer amount of frames per minute.
    // As per Memo 230 the formatter-to-frame time phasing is such that
    // a frame boundary _always_ is present on a minute boundary.
    if( (strict & headersearch::chk_strict) &&
        (trackbitrate != headersearch_type::UNKNOWN_TRACKBITRATE) ) {
        // MarkIV frame = 2500 bytes = 2500 x 8 bits, divided by
        // track bit rate = length-of-frame in seconds, times 1.0e9 = 
        // length-of-frame in nano seconds
        // HV: 05 Nov 2015  With rational time stamps this computation should
        //                  become better

        // uh-oh ...
        if( !isModulo(frametime, decoder->frametime) ) {
            ostringstream  msg;

            msg << "Invalid Mark4 timecode: frame time stamp "
                << frametime.tv_subsecond
                << " is inconsistent with trackbitrate "
                << trackbitrate << "bps, should be multiple of "
                << decoder->frametime;
            // Depending on flags in the strict value, do things
            if( strict & headersearch::chk_verbose )
                std::cerr << msg.str() << endl;
            if( strict & headersearch::chk_strict ) {
                if( strict & headersearch::chk_nothrow ) {
                    frametime = highrestime_type();
                    return frametime;
                }
                EZASSERT2( isModulo(frametime, decoder->frametime) , headersearch_exception, EZINFO(msg.str()) );
            }
        }
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
    frametime.tv_sec  = ::mktime(&m4_time);

#ifdef GDBDEBUG
    char buf[32];
    ::strftime(buf, sizeof(buf), "%d-%b-%Y (%j) %Hh%Mm%Ss", &m4_time);
    DEBUG(4, "mk4_ts: after normalization " << buf << " +" << frametime.tv_subsecond << "s" << endl);
#endif
    return frametime;
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
highrestime_type decode_vlba_timestamp(Header const* ts, const headersearch::strict_type /*strict*/) {
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
                          const highrestime_type& ts,
                          const headersearch_type* const hdr) {
    int                   i, j;
    uint8_t*              frame8  = (uint8_t *)framedata;
    uint16_t*             frame16 = (uint16_t *)framedata;
    uint32_t*             frame32 = (uint32_t *)framedata;
    uint64_t*             frame64 = (uint64_t *)framedata;
    struct tm             tm;
    unsigned int          crc;
    unsigned char         header[20];
    const unsigned int    ntrack       = hdr->ntrack;
    const samplerate_type trackbitrate = hdr->trackbitrate;

    if( (trackbitrate==0) || (trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) || trackbitrate.denominator()!=1 )
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
    // Some data rates produce .25 ms time stamps but those are implied
    // and are ignored in the encoder [see the decoding routine which
    // adds a correction]

    // our subsecond value is in units of seconds
    // HV: 11-Nov-2015 Now that our time stamps are rational numbers we can do
    //                 better and easier:
    //                  At the highest MarkIV/VLBA rates, the frame times are 2.50ms or 1.25ms but we 
    //                  truncate those values. The decoder knows of the *implied* frame time stamp.
    //                  Note that the check wether or not this time stamp was representable is
    //                  simply a check wether or not the current subsecond value is an integral 
    //                  multiple of the mark4 frame time
    const unsigned int    ms  = boost::rational_cast<unsigned int>(ts.tv_subsecond * 1000);
    const samplerate_type mod = ts.tv_subsecond / hdr->get_state().frametime;

    EZASSERT2( mod.denominator()==1, headersearch_exception,
               EZINFO("time stamp " << ts << " is not representable as MarkIV time stamp with frame time of " <<
                      hdr->get_state().frametime));

    header[17] = (unsigned char)( ((ms/100) % 10) << 4 );
    header[17] = (unsigned char)( header[17] | ((ms/10)  % 10) );
    header[18] = (unsigned char)( (ms       % 10) );
    // for 8 and 16 Mbps the last digit of the frametime
    // cannot be 4 or 9
    // HV: 11-Nov-2015 I don't think that '4' and '9' are valid at *any* data rate
    EZASSERT2(header[18]!=4 && header[18]!=9, headersearch_exception,
              EZINFO("Valid MarkIV time stamps can *never* end in '4' or '9', subsecond = " << ts.tv_subsecond));

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

// Encode the Mark4 timestamp for ST (straight through)
void encode_mk4_timestamp_st(unsigned char* framedata,
                          const highrestime_type& ts,
                          const headersearch_type* const hdr) {
    // The maximum header size for Mark4/ST is 32 tracks * 20 bytes header / track
    // (Note: the c'tor of "headersearch_type" enforces this
    uint8_t            header[ 32*20 ];
    uint32_t*          header32 = (uint32_t*)&header[0]; 
    uint32_t*          frame32  = (uint32_t*)framedata;

    // Generate the standard Mk4 header in our local buffer
    encode_mk4_timestamp(&header[0], ts, hdr);

    // Should copy to caller's buffer first, inserting the parity word after
    // each 8th word (ie bit)
    // Note: do the bit counting inside the loop so we get to properly
    // insert the last parity bit
    for(unsigned int i=0, j=0, ones=0; i<160; j++) {
        frame32[j] = header32[i];
        if( header32[i] )
            ones++;
        i++;
        if( i%8==0 ) {
            // insert extra parity word
            j          = j+1;
            frame32[j] = ((ones&0x1)==0x1) ? ~0x0 : 0x0;
            // syncword has opposite parity
            if( i>=64 && i<96 )
                frame32[j] = ~frame32[j];
            ones       = 0;
        }
    }

    // Now encode it into the caller's buffer in NRZM encoding
    for(unsigned int i=1; i<160; i++)
        frame32[i] = frame32[i] ^ frame32[i-1];
    return;
}

void encode_vlba_timestamp(unsigned char* framedata,
                           const highrestime_type& ts,
                           const headersearch_type* const hdr) {
    int                mjd, sec, i, j;
    uint8_t            header[8];
    uint32_t           word[2];
    uint8_t*           wptr = (uint8_t*)&word[0];
    uint8_t*           frame8  = (uint8_t *)framedata;
    uint16_t*          frame16 = (uint16_t *)framedata;
    uint32_t*          frame32 = (uint32_t *)framedata;
    uint64_t*          frame64 = (uint64_t *)framedata;
    unsigned int       crc;
    unsigned int       dms; // deci-milliseconds; VLBA timestamps have 10^-4 resolution
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
    // 06 Nov 2015 subsecond is now in units of seconds
    //             so we multiply by 10^4 and round to 
    //             int for the value of deci milliseconds
    dms = boost::rational_cast<unsigned int>(ts.tv_subsecond * 10000);
    word[1] = 0;
    word[1] |= ((dms / 1) % 10) << 16;
    word[1] |= ((dms / 10) % 10) << 20;
    word[1] |= ((dms / 100) % 10) << 24;
    word[1] |= ((dms / 1000) % 10) << 28;

    // 19Aug2015: eBob found an out-of-bounds error here:
    //            we were adressing "&word[2]" whilst "word"
    //            is only two elements long. The crc needs to computed
    //            over the 8 bytes of the time stamp.
    //            This is probably due to a verbatim copy-paste from
    //            "encode_mark5b_timestamp".
    crc = crc16_vlba((unsigned char*)&word[0], 8);
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
                           const highrestime_type& ts,
                           const headersearch_type* const hdr) {
    int                   mjd, sec;
    long                  dms; // deci-milliseconds; VLBA timestamps have 10^-4 resolution
    uint32_t*             word = (uint32_t *)framedata;
    unsigned int          crc, framenr;
    const samplerate_type trackbitrate = hdr->trackbitrate;

    if( (trackbitrate==0) || (trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) || trackbitrate.denominator()!=1) {
        throw invalid_track_bitrate();
    }
    
    mjd = 40587 + (ts.tv_sec / 86400);
    sec = ts.tv_sec % 86400;

    // Frames per second -> framenumber within second
    // Mk5B framesize == 10000 bytes == 80000 bits == 8.0e4 bits
    // 1s = 1.0e9 ns
    const samplerate_type frametime( hdr->get_state().frametime );
    const subsecond_type  framenum( ts.tv_subsecond / frametime );

    EZASSERT2(framenum.denominator()==1, headersearch_exception,
              EZINFO("time stamp " << ts << " is not representable as multiple of frame time " << frametime));
    EZASSERT2(framenum.numerator()<UINT_MAX, headersearch_exception,
              EZINFO("time stamp " << ts << " results in frame number > 2^32-1 for frame time " << frametime));
    framenr      = (unsigned int)framenum.numerator();
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
    // 06 Nov 2015 subsecond is now in units of seconds
    //             so we multiply by 10^4 and round to 
    //             int for the value of deci milliseconds
    dms = boost::rational_cast<unsigned int>(ts.tv_subsecond * 10000);

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
                           const highrestime_type& ts,
                           const headersearch_type* const hdr) {
    // Before doing anything, check this and bail out if necessary -
    // we want to avoid dividing by zero
    if( (hdr->trackbitrate==0) || (hdr->trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) )
        throw invalid_track_bitrate();
    if( hdr->ntrack==0 )
        throw invalid_number_of_tracks();

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
    const time_t          tm_epoch = ::mktime(&klad);
    const samplerate_type frametime( hdr->get_state().frametime );
    const subsecond_type  framenum( ts.tv_subsecond / frametime );

#ifdef GDBDEBUG
    DEBUG(4, "encode_vdif_ts: ts=" << ts << " (subsecond=" << ts.tv_subsecond << ") " << endl <<
             "                frametime=" << frametime << " => framenum=" << framenum << endl);
#endif

    EZASSERT2(framenum.denominator()==1, headersearch_exception,
              EZINFO("time stamp " << ts << " is not representable as multiple of frame time " << frametime));
    EZASSERT2(framenum.numerator()<((0x1<<24) - 1), headersearch_exception,
              EZINFO("time stamp " << ts << " results in 24bit frame number overflow with frame time of " << frametime));

    vdif_hdr->legacy          = (hdr->frameformat==fmt_vdif_legacy);
    vdif_hdr->data_frame_len8 = (unsigned int)(((hdr->payloadsize+(vdif_hdr->legacy?16:32))/8) & 0x00ffffff);
    vdif_hdr->ref_epoch       = (unsigned char)(epoch & 0x3f);
    vdif_hdr->epoch_seconds   = (unsigned int)((ts.tv_sec - tm_epoch) & 0x3fffffff);
    vdif_hdr->data_frame_num  = (uint32_t)(framenum.numerator()&0xffffff);
    return;
}

// This version of time stamp encoding takes into account that the subsecond
// part represents a time stamp somewhere in the *period*, where period!=1
// second.
// To properly encode the vdif frame number since start of period, we must
// be able to find back what the origin of the period was.
// We use the computed offset lookup table in the decoderstate part of the
// headersearch object.
// This offset lookuptable will tell, based on the modulo of our subsecond's
// numerator with the frame period what the integer offset to the zero point
// for this time stamp was.
// From there on encoding the vdif time stamp should be ez as 3.1415926
void encode_vdif2_timestamp(unsigned char* framedata,
                           const highrestime_type& ts,
                           const headersearch_type* const hdr) {
    // Before doing anything, check this and bail out if necessary -
    // we want to avoid dividing by zero
    if( (hdr->trackbitrate==0) || (hdr->trackbitrate == headersearch_type::UNKNOWN_TRACKBITRATE) )
        throw invalid_track_bitrate();
    if( hdr->ntrack==0 )
        throw invalid_number_of_tracks();

    struct tm             klad;
    highrestime_type      ts_zero( ts.tv_sec );
    struct vdif_header*   vdif_hdr = (struct vdif_header*)framedata;

    // First rework the time stamp t1 + x/y to t0 + n * p/q (where p/q is
    // the current frame time). We must make sure that the value in
    // the numerator is not the numerator of a reduced fraction; we 
    // really must have it in units of 'q'
    const samplerate_type& frametime( hdr->get_state().frametime );
    const subsecond_type&  ss( ts.tv_subsecond );
    const uint64_t         modulo = (ss.numerator() * (frametime.denominator() / ss.denominator())) % frametime.numerator();

    // Set zero point for the current period
    ts_zero.tv_sec += hdr->get_state().offset_lut[ modulo ];

    // Compute the frame number from the actual elapsed time since start of
    // period divided by the actual frame time.
    const highresdelta_type framenum = (ts - ts_zero) / frametime.as<highresdelta_type>();

    EZASSERT2(framenum.denominator()==1, headersearch_exception,
              EZINFO("The time stamp " << ts.tv_sec << " +" << ss << " is not representable as VDIF frame number with frametime=" <<
                     frametime << " and dt=" << (ts-ts_zero) << " => framenum=" << framenum << " [zeropointoffset=" << hdr->get_state().offset_lut[ modulo ] << "]"));

    ::gmtime_r(&ts.tv_sec, &klad);
    const int epoch    = (klad.tm_year + 1900 - 2000)*2 + (klad.tm_mon>=6);

    // Now set the zero point of that epoch, 00h00m00s on the 1st day of
    // month 0 (Jan) or 6 (July)
    klad.tm_hour  = 0;
    klad.tm_min   = 0; 
    klad.tm_sec   = 0;
    klad.tm_mon   = (klad.tm_mon/6)*6;
    klad.tm_mday  = 1;
    const time_t          tm_epoch = ::mktime(&klad);

#ifdef GDBDEBUG
    DEBUG(4, "encode_vdif2_ts: ts=" << ts << " (subsecond=" << ts.tv_subsecond << ") " << endl <<
             "                frametime=" << frametime << " => framenum=" << framenum << endl);
#endif

    EZASSERT2(framenum.numerator()<((0x1<<24) - 1), headersearch_exception,
              EZINFO("time stamp " << ts << " results in 24bit frame number overflow with frame time of " << frametime));

    vdif_hdr->legacy          = (hdr->frameformat==fmt_vdif_legacy);
    vdif_hdr->data_frame_len8 = (unsigned int)(((hdr->payloadsize+(vdif_hdr->legacy?16:32))/8) & 0x00ffffff);
    vdif_hdr->ref_epoch       = (unsigned char)(epoch & 0x3f);
    vdif_hdr->epoch_seconds   = (unsigned int)((ts_zero.tv_sec - tm_epoch) & 0x3fffffff);
    vdif_hdr->data_frame_num  = (uint32_t)(framenum.numerator()&0xffffff);
    return;
}




template<bool strip_parity> highrestime_type mk4_frame_timestamp(
        unsigned char const* framedata, const unsigned int track,
        const unsigned int ntrack, const samplerate_type& trackbitrate,
        decoderstate_type* decoder, const headersearch::strict_type strict) {
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

    return decode_mk4_timestamp(&timecode[0], trackbitrate, strict, decoder);
}

template<bool strip_parity> highrestime_type vlba_frame_timestamp(
        unsigned char const* framedata, const unsigned int track,
        const unsigned int ntrack, const samplerate_type& /*trackbitrate*/,
        decoderstate_type*, const headersearch::strict_type strict) {
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
    return decode_vlba_timestamp<vlba_tape_ts>((vlba_tape_ts const*)&timecode[0], strict);
}

highrestime_type mk5b_frame_timestamp(unsigned char const* framedata, 
                              const unsigned int /*track*/,
                              const unsigned int /*ntrack*/, 
                              const samplerate_type& trackbitrate,
                              decoderstate_type* state, 
                              const headersearch::strict_type strict) {
    struct m5b_state {
        time_t          second;
        samplerate_type frameno;
        bool            wrap;
    };
    // In Mk5B there is no per-track header. Only one header (4 32bit words) for all data
    // and it's at the start of the frame. The timecode IS a VLBA style
    // timecode which starts at word #2 in the header

    // Start by decoding the VLBA timestamp - this has at least the integer
    // second value
    highrestime_type    vlba   = decode_vlba_timestamp<mk5b_ts>((mk5b_ts const *)(framedata+8), strict);
    m5b_state*          m5b_s  = (m5b_state*)((void*)&state->user[0]); // gcc won't allow direct cast to pointer to struct
                                                                       // on account of strict aliasing rules
    m5b_header const*   m5b_h  = (m5b_header const*)framedata;
    samplerate_type     frameno(m5b_h->frameno);
    subsecond_type      prevnsec = 0;

    // Use the frame#-within-seconds to enhance accuracy
    // If we detect a wrap in the framenumber within the same integer
    // second: add max Mk5B framenumber
    // to the actual framenumber
    m5b_s->wrap = (vlba.tv_sec==m5b_s->second && // only if still in same integer second
                   (m5b_s->wrap ||               // seen previous wrap?
                    frameno < m5b_s->frameno));  // or do we see one now?
    if( m5b_s->wrap )
        frameno += 0x8000; // 15 bits is maximum framenumber so we must add 0x8000

    if ( trackbitrate != headersearch_type::UNKNOWN_TRACKBITRATE ) {
        // Differentiate between strict Mk5 and non-strict Mk5B 
        // time stamp decoding
        if( frameno>=state->framerate ) {
            if( (strict & headersearch::chk_strict) ||
                (strict & headersearch::chk_consistent) ) {
                if( strict & headersearch::chk_verbose )
                    std::cerr << "MARK5B FRAMENUMBER " << frameno << " OUT OF RANGE! Max " << state->framerate << std::endl;
                if( strict & headersearch::chk_nothrow )
                    return highrestime_type();                    
                EZASSERT2( frameno<state->framerate, headersearch_exception,
                           EZINFO("MARK5B FRAMENUMBER " << frameno << " OUT OF RANGE! Max " << state->framerate) );
            }
        } else {
            // replace the subsecond timestamp with one computed from the 
            // frametime
            prevnsec          = vlba.tv_subsecond;
            vlba.tv_subsecond = (state->frametime * frameno);

            // Two problems with the original assert:
            //   * strict==false ALWAYS made the assert fail
            //   * checking for ::fabs()<0 is nonsense and
            //     checking for double==0 is probably not good either
            // So reworked to properly deal with the 'strictness' setting
            // and allow the timestamps to be equal if they're closer 
            // than 1.0 x 10e-6 seconds
            //
            // HV: 23-May-2014 Digital back ends tend not to fill in the
            //                 VLBA-style sub second field in the Mark5B
            //                 header. If the "chk_allow_dbe" flag is set
            //                 we don't perform the test.
            //                 We detect DBE by having frameno!=0 and having
            //                 prevnsec == 0
            // HV: 11-Nov-2015 Now that we've 'rationalized' our time stamps
            //                 (they're kept as a rational number) we have
            //                 to revisit this code too. Due to limited precision
            //                 of the VLBA subsecond field (only .1 msec resolution)
            //                 then, at the higher frame rates, the time stamps
            //                 computed from the framenumber and the VLBA subsecond
            //                 field will differ a bit. We require that, when checking
            //                 for consistency, that both methods of computing the
            //                 subsecond value yield the same number, to the
            //                 VLBA precision, i.e. to the 1 part in 10^4.
            const bool  maybe_dbe = (prevnsec==0 && frameno!=0);

            if( strict & headersearch::chk_consistent ) {
                const unsigned int  vlba_precision( 10000 ); // 1 in 10^4 precision for VLBA time stamp
                const unsigned int  ss_from_vlba    = boost::rational_cast<unsigned int>(prevnsec * vlba_precision);
                const unsigned int  ss_from_frameno = (unsigned int)::floor(
                                                                boost::rational_cast<double>(vlba.tv_subsecond * vlba_precision));
                const bool          errcond         = (ss_from_vlba!=ss_from_frameno);

#ifdef GDBDEBUG
    DEBUG(4, "mk5b_frame_timestamp : ss_from_vlba=" << ss_from_vlba << " ss_from_frameno=" << ss_from_frameno << " => errcond=" << errcond << endl);
#endif
                // !dbe || (dbe && allow_dbe)
                if( errcond && (!maybe_dbe || (maybe_dbe && !(strict&headersearch::chk_allow_dbe))) ) {
                    ostringstream  msg;

                    msg << "Time stamp (" << prevnsec << ") and time from frame number for "
                        << "given data rate (frame#" << frameno << " * " << state->frametime << " = " <<  vlba.tv_subsecond << ") do not match";
                    if( strict & headersearch::chk_verbose )
                        std::cerr << msg.str() << std::endl;
                    // The fact that we end up here means that we know that
                    // the following ASSERT is going to #FAIL. But if the
                    // user has specified the nothrow flag, we shouldn't.
                    if( strict & headersearch::chk_nothrow )
                        return highrestime_type(); 
                    EZASSERT2(ss_from_vlba == ss_from_frameno, headersearch_exception, EZINFO(msg.str()));
                }
            }
        }
    }

#ifdef GDBDEBUG
    DEBUG(4, "mk5b_frame_timestamp : framerate, dur = " << state->framerate << ", " << state->frametime << std::endl <<
             "    VLBA -> " << vlba.tv_sec << "s + " << prevnsec << "s" << std::endl << 
             "    frameno hdr, used: " << m5b_h->frameno << ", " << frameno << std::endl <<
             "      => new subsec  : " << vlba.tv_subsecond << std::endl);
#endif

    // update state
    m5b_s->second  = vlba.tv_sec;
    m5b_s->frameno = m5b_h->frameno;
    return vlba;
}

highrestime_type  vdif_frame_timestamp(unsigned char const* framedata,
                                       const unsigned int /*track*/,
                                       const unsigned int /*ntrack*/,
                                       const samplerate_type& trackbitrate,
                                       decoderstate_type* decoder,
                                       const headersearch::strict_type /*strict*/) {
    struct tm                 tm;
    struct vdif_header const* hdr = (struct vdif_header const*)framedata;

    EZASSERT2(trackbitrate>0, headersearch_exception, EZINFO("Cannot do VDIF timedecoding when bitrate == 0"));

    // Get integer part of the time
    tm.tm_wday   = 0;
    tm.tm_isdst  = 0;
    tm.tm_yday   = 0;
    tm.tm_mday   = 1;
    tm.tm_hour   = 0;
    tm.tm_min    = 0;
    tm.tm_sec    = (int)hdr->epoch_seconds;
    tm.tm_year   = 100 + (hdr->ref_epoch/2);
    tm.tm_mon    = 6   * (hdr->ref_epoch%2);

    return highrestime_type( ::mktime(&tm), 
                             (trackbitrate==headersearch_type::UNKNOWN_TRACKBITRATE) ? 
                                 highrestime_type::UNKNOWN_SUBSECOND :
                                 hdr->data_frame_num * decoder->frametime );
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
    (IS_ST(fmt) ? PAYLOADSIZE(STRIP_ST(fmt), st_tracks) : PAYLOADSIZE(fmt, ntrk))
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

#define ENCODERFN(fmt, period) \
    ((fmt==fmt_mark5b)?&encode_mk5b_timestamp: \
     ((fmt==fmt_vlba)?&encode_vlba_timestamp: \
      ((fmt==fmt_mark4)?&encode_mk4_timestamp: \
       ((fmt==fmt_mark4_st)?&encode_mk4_timestamp_st: \
        ((fmt==fmt_vdif || fmt==fmt_vdif_legacy)? \
         (period==1 ? &encode_vdif_timestamp : &encode_vdif2_timestamp):((timeencoder_fn)0))))))

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
headersearch_type::headersearch_type(format_type fmt, unsigned int tracks, const samplerate_type& trkbitrate, unsigned int vdifpayloadsize):
    frameformat( fmt ),
    ntrack( tracks ),
    trackbitrate( trkbitrate ),
    syncwordsize( SYNCWORDSIZE_ST(fmt, tracks) ),
    syncwordoffset( SYNCWORDOFFSET_ST(fmt, tracks) ),
    headersize( ::headersize(fmt, tracks) ),
    framesize( ::framesize(fmt, tracks, vdifpayloadsize) ),
    payloadsize( PAYLOADSIZE_VDIF(fmt, tracks, vdifpayloadsize) ),
    payloadoffset( PAYLOADOFFSET_VDIF(fmt, tracks) ),
    state( ntrack, trackbitrate, payloadsize ),
    timedecoder( DECODERFN(fmt) ),
    timeencoder( ENCODERFN(fmt, state.framerate.denominator()) ),
    checker( CHECKFN(fmt) ),
    syncword( SYNCWORD_ST(fmt) )
{
    // Finish off with assertions ...
    const unsigned int mintrack = (frameformat==fmt_mark5b?1:8);

    if(MK4VLBA(frameformat) || frameformat==fmt_mark5b) {
        EZASSERT2( ((ntrack>=mintrack) && (ntrack<=64) && (ntrack & (ntrack-1))==0), headersearch_exception,
                   EZINFO("ntrack (" << ntrack << ") is NOT a power of 2 which is >=" << mintrack << " and <=64") );
    }
    if(IS_ST(frameformat)) {
        EZASSERT2( ntrack==32, headersearch_exception,
                   EZINFO("ntrack (" << ntrack << ") is NOT 32 while mode is straight through") );
    }
    if(IS_VDIF(frameformat)) {
        EZASSERT2( (payloadsize%8)==0, headersearch_exception,
                   EZINFO("The VDIF dataarraysize is not a multiple of 8") );
    }
    // check overflow in ntrack * trackbitrate. Since trackbitrate is a
    // rational<uint64_t> the only way this can overflow if ntrack *
    // trackbitrate.numerator() overflows. So we'll do that multiplication
    // and detect if it has wrapped ...
    const uint64_t  ntrack_by_bitrate = ntrack * trackbitrate.numerator();

    if( trkbitrate!=headersearch_type::UNKNOWN_TRACKBITRATE ) {
        EZASSERT2(ntrack_by_bitrate>=trackbitrate.numerator(), headersearch_exception,
                  EZINFO("It seems that the number of tracks x the trackbitrate would overflow the 64-bit number holding this product: " 
                         << ntrack << " x " << trackbitrate.numerator() << " = " << ntrack_by_bitrate));
    }
    // Should we check trackbitrate for sane values?
    if( trackbitrate.denominator()!=1 ) {
        // frame formats that do not count in integer second + frame number since start
        // cannot represent non-integer frames per 1 second. Systems that do count time stamps
        // in this way theoretically can. [This would assume that Mark5B
        // could do this ... but no, let's not]
        EZASSERT2( IS_VDIF(frameformat), headersearch_exception, 
                   EZINFO("Only VDIF format can handle non-integer samplerate per second") );
    }
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
    state( ntrack, trackbitrate, payloadsize ),
    timedecoder( 0 ),
    timeencoder( 0 ),
    checker( 0 ),
    syncword( 0 )
{ EZASSERT2( factor!=0 && ntrack>0 && trackbitrate>0 && payloadsize>0, headersearch_exception, 
             EZINFO("cannot " << ((factor<0)?("divide"):("multiply")) << " header by " << ::abs(factor) <<
                    " with ntrack=" << ntrack << " trackbitrate=" << trackbitrate << " payloadsize=" << payloadsize <<
                    endl) ); }


// scale the number of tracks either by the amount of chunks the input header is divided into
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
    state( ntrack, trackbitrate, payloadsize ),
    timedecoder( 0 ),
    timeencoder( 0 ),
    checker( 0 ),
    syncword( 0 )
{ EZASSERT2( ntrack>0 && trackbitrate>0 && payloadsize>0 && factor.imag()<=other.ntrack /*&& payloadsize!=(unsigned int)-1*/,
             headersearch_exception,
             EZINFO("cannot divide header by complex " << factor << " with " <<
                    "ntrack=" << ntrack << " trackbitrate=" << trackbitrate <<
                    "payloadsize=" << payloadsize << endl) ); }


// This is a static function! No 'this->' available here.
template<bool strip_parity> void headersearch_type::extract_bitstream(
        unsigned char* dst,
        const unsigned int track, const unsigned int ntrack, unsigned int nbit,
        unsigned char const* frame) {
    // We do not recompute all shifted bitpositions each time
    static const unsigned int  msb      = 7; // most significant bit number, for unsigned char that is
    static const unsigned char mask[]   = { 0x1,  0x2,  0x4,  0x8,  0x10,  0x20,  0x40,  0x80};
    static const signed   char unmask[] = {~0x1, ~0x2, ~0x4, ~0x8, ~0x10, ~0x20, ~0x40, 0x7F};
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
highrestime_type headersearch_type::decode_timestamp( unsigned char const* framedata, const headersearch::strict_type strict, const unsigned int track ) const {
    return timedecoder(framedata, track, this->ntrack, this->trackbitrate, &this->state, strict);
}

void headersearch_type::encode_timestamp( unsigned char* framedata, const highrestime_type& ts ) const {
    return timeencoder(framedata, ts, this);
}

bool headersearch_type::check( unsigned char const* framedata, const headersearch::strict_type checksyncword, unsigned int track ) const {
    return (this->*checker)(framedata, checksyncword, track);
}

bool headersearch_type::nop_check(unsigned char const*, const headersearch::strict_type, unsigned int) const {
    return true;
}



///////////////////////////////////////////////////////////////////
////                                                           ////
////    Function to go from format string (Walter Brisken)     ////
////    to actual headersearch object.                         ////
////    Walter's format has support for decimation, we don't.  ////
////    (We parse/swallow but inform the user it's ignored)    ////
////                                                           ////
///////////////////////////////////////////////////////////////////

/* Quoth Walter:
 * <format>-<rate>-<channels>-<bits>
 *
 * where
 *
 * <format> is the data format.  Examples:
 * Mark5B
 * VDIF (single threaded VDIF)
 * VDIFL (single threaded VDIF w/ legacy headers)
 * VLBA1_4 (VLBA format with fanout of 1:4)
 * MKIV1_2 (Mark4 format with fanout 1:2)
 *
 * (Note: technically it's VLBAn_m or MKIVn_m where
 *        the fan mode is n:m so _technically_
 *        4:1 fan-in would be covered but none of our
 *        codebases (mk5access/jive5ab) actually handle
 *        fan-in data)
 *
 * <rate> is data (excluding framing) rate in Mbps
 *
 * <channels> is the number of baseband channels.  must be 2^n
 *
 * <bits> is the number of bits per sample.
 *
 *
 * This format specifier breaks down a bit for multi-thread VDIF but that is
 * not handled by mark5access anyway.A
 */

// Prototype for a parse function. If the function returns 'true', it's
// expected to have filled in the parameters. If 'false', no assumptions
// are made about the contents of the variables.
typedef headersearch_type* (*parsefn_type)(char const * const s);


headersearch_type* pNone(char const * const s) {
    static const Regular_Expression rxNone( "^none$", REG_EXTENDED|REG_ICASE );

    if( rxNone.matches(s) )
        return new headersearch_type();
    return 0;
}

// Mark5B-<rate>[/<period>]-<channel>-<bits>[/<decimation>]
headersearch_type* pMark5B(char const * const s) {
    static const Regular_Expression rxMark5B( "^Mark5B-([0-9]+(/[0-9]+)?)-([0-9]+)-([0-9]+)(/[0-9]+)?$", REG_EXTENDED|REG_ICASE );

    unsigned int      nChan, bitspSample, nTrk;
    samplerate_type   rateMbps;
    const matchresult mr = rxMark5B.matches(s);

    if( !mr )
        return 0;

    // We don't have to check scanf because it passed the regex!
    // matchresult #0 is the whole string
    const string    rate_s( mr.group(1) );
    istringstream   iss( rate_s + ((rate_s.find('/')==string::npos) ? "/1" : "") );
    iss >> rateMbps;

    ::sscanf(mr.group(3).c_str(), "%u", &nChan);
    ::sscanf(mr.group(4).c_str(), "%u", &bitspSample);

    if( mr[5] ) {
        DEBUG(2, "pMark5B/ignoring supplied decimation " << mr.group(5) << endl);
    }

    // Compute actual number of tracks
    nTrk     = (nChan * bitspSample);
    if( nTrk==0 )
        return 0;
    // Convert total data rate into track bit rate
    rateMbps = (rateMbps * 1000000) / nTrk;
    return new headersearch_type(fmt_mark5b, nTrk, rateMbps, 0);
}

// NOTE: 
//   there are two kinds of VDIF specifiers [not counting legacy/real VDIF]:
//   VDIF-*-*-*       (and VDIFL-*-*-*)
//   VDIF_*-*-*-*     (and VDIFL_*-*-*-*)
// Spot the difference ... '_' versus '-' after the "VDIF" identifier.
//
// The '-' version is what is found e.g. in the VEX file but that string
// has one less field; it lacks the VDIF frame size. 
//
// As such that format is pretty useless. We 'parse' it; which reads that
// we check IF that format is detected, then we throw an error with
// a descriptive message that the user should be using a different format
headersearch_type* pVDIF_unsupported(char const * const s) {
    static const Regular_Expression rxVDIFu( "^VDIFL?-[0-9]+-[0-9]+-[0-9]+(/[0-9]+)?$", REG_EXTENDED|REG_ICASE );

    if( rxVDIFu.matches(s) ) {
        THROW_EZEXCEPT(headersearch_exception,
                       "\"VDIF(L)-*-*-*[/*]\" format not supported; it lacks the VDIF framesize. Try "
                       "VDIF(L)_<payload size>-*-*-*[/*] (note the '_' and the extra parameter)."); 
    }
    return 0;
}

// VDIF(L)_<payload>-<rate>[/<period>]-<channel>-<bits>[/<decimation>]
// <period> = we support xxx bits per yyy seconds where both xxx and yyy are
// integer but yyy is not necessarily 1!
headersearch_type* pVDIF(char const * const s) {
    static const Regular_Expression rxVDIF( "^(VDIFL?)_([0-9]+)-([0-9]+(/[0-9]+)?)-([0-9]+)-([0-9]+)(/[0-9]+)?$", REG_EXTENDED|REG_ICASE );
    matchresult     mr = rxVDIF.matches( s );
    unsigned int    nChan, bitspSample, nTrk, vdifPayload;
    samplerate_type rateMbps;

    if( !mr )
        return 0;

    // Regex matches so we can easily convert
    ::sscanf(mr.group(2).c_str(), "%u", &vdifPayload);

    // Deal with rateMbs being a rational
    const string    rate_s( mr.group(3) );
    istringstream   iss( rate_s + ((rate_s.find('/')==string::npos) ? "/1" : "") );
    iss >> rateMbps;
    ::sscanf(mr.group(5).c_str(), "%u", &nChan);
    ::sscanf(mr.group(6).c_str(), "%u", &bitspSample);

    if( mr[7] ) {
        DEBUG(2, "pVDIF/ignoring supplied decimation " << mr.group(7) << endl);
    }

    // Compute actual number of tracks
    nTrk     = (nChan * bitspSample);

    if( nTrk==0 )
        return 0;
    // Convert total data rate into track bit rate
    rateMbps = (rateMbps * 1000000) / nTrk;
    return new headersearch_type( (::tolower(mr[mr[1]])==::tolower("VDIF"))?fmt_vdif:fmt_vdif_legacy, nTrk, rateMbps, vdifPayload);
}

// VLBAn_m-<rate>-<channel>-<bits>[/<decimation>]
//   where n:m is fan-in (unsupported) or fan-out
headersearch_type* pVLBA(char const * const s) {
    static const Regular_Expression rxVLBA( "^VLBA([0-9])_([0-9])-([0-9]+)-([0-9]+)-([0-9]+)(/[0-9]+)?$", REG_EXTENDED|REG_ICASE );
    matchresult  mr = rxVLBA.matches( s );
    unsigned int rateMbps, nChan, bitspSample, trkIn, trkOut;

    if( !mr )
        return 0;

    // Valid string
    ::sscanf(mr.group(1).c_str(), "%u", &trkIn);
    ::sscanf(mr.group(2).c_str(), "%u", &trkOut);
    ::sscanf(mr.group(3).c_str(), "%u", &rateMbps);
    ::sscanf(mr.group(4).c_str(), "%u", &nChan);
    ::sscanf(mr.group(5).c_str(), "%u", &bitspSample);

    if( mr[6] ) {
        DEBUG(2, "pVLBA/ignoring supplied decimation " << mr.group(6) << endl);
    }

    // We do NOT support fan-in!
    EZASSERT2(trkIn<=trkOut, headersearch_exception, EZINFO("We do not support fan-in (" << trkIn << ":" << trkOut << ")"));
    if( trkIn==0 )
        return 0;
    // Compute actual number of tracks
    trkOut   = (nChan * bitspSample * trkOut/trkIn);
    if( trkOut==0 )
        return 0;
    // Convert total data rate into track bit rate
    rateMbps = (rateMbps * 1000000) / trkOut;
    return new headersearch_type(fmt_vlba, trkOut, rateMbps, 0);
}

// MarkIV
// MKIVn_m-<rate>-<channel>-<bits>[/<decimation>]
//   where n:m is fan-in (unsupported) or fan-out
headersearch_type* pMKIV(char const * const s) {
    static const Regular_Expression rxMKIV( "^MKIV([0-9])_([0-9])-([0-9]+)-([0-9]+)-([0-9]+)(/[0-9]+)?$", REG_EXTENDED|REG_ICASE );
    matchresult  mr = rxMKIV.matches( s );
    unsigned int rateMbps, nChan, bitspSample, trkIn, trkOut;

    if( !mr )
        return 0;

    // Valid string, matches regex!
    ::sscanf(mr.group(1).c_str(), "%u", &trkIn);
    ::sscanf(mr.group(2).c_str(), "%u", &trkOut);
    ::sscanf(mr.group(3).c_str(), "%u", &rateMbps);
    ::sscanf(mr.group(4).c_str(), "%u", &nChan);
    ::sscanf(mr.group(5).c_str(), "%u", &bitspSample);

    if( mr[6] ) {
        DEBUG(2, "pMKIV/ignoring supplied decimation " << mr.group(6) << endl);
    }

    // We do NOT support fan-in!
    EZASSERT2(trkIn<=trkOut, headersearch_exception, EZINFO("We do not support fan-in (" << trkIn << ":" << trkOut << ")"));
    if( trkIn==0 )
        return 0;
    // Compute actual number of tracks
    trkOut   = (nChan * bitspSample * trkOut/trkIn);
    if( trkOut==0 )
        return 0;
    // Convert total data rate into track bit rate
    rateMbps = (rateMbps * 1000000) / trkOut;
    return new headersearch_type(fmt_mark4, trkOut, rateMbps, 0);
}

// Returns NULL if no match was found
headersearch_type* text2headersearch( const string& s ) {
    static parsefn_type   formats[] = {&pNone, &pVDIF_unsupported, &pMark5B, &pVDIF, &pVLBA, &pMKIV};

    for(size_t n=0; n<sizeof(formats)/sizeof(formats[0]); n++)
        if( headersearch_type* rv=formats[n](s.c_str()) )
            return rv;
    return 0;
}

// CRC business
//typedef unsigned short CRCtype;

template <unsigned int CRCWidth, unsigned int Key>
struct crctable_type {
    // construct the crc table for a CRC of given Width and generating
    // polynomial key
    crctable_type() {
        const uint64_t polyorderbit( 1<<CRCWidth );
        EZASSERT( CRCWidth<=32 && CRCWidth>=8, headersearch_exception );
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
