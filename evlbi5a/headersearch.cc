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
#include <dosyscall.h>
#include <stringutil.h>

#include <string.h>

using std::ostream;
using std::string;
using std::cout;
using std::endl;


format_type text2format(const string& s) {
	const string lowercase( tolower(s) );
	if( lowercase=="mark4" )
		return fmt_mark4;
	else if( lowercase=="vlba" )
		return fmt_vlba;
	else if( lowercase=="mark5b" )
		return fmt_mark5b;
	throw invalid_format_string();
}

#if 0
unsigned int headersize(format_type fmt) {
    ASSERT_COND(fmt==fmt_mark5b);
    return headersize(fmt, 0);
}
#endif

// Now always return a value. Unknown/unhandled formats get 0
// as framesize. Well, you get what you ask for I guess
// [XXX] - default behaviour may need to change if fmt_unknown/fmt_none
//         separate
unsigned int headersize(format_type fmt, unsigned int ntrack) {
    // all know formats have 8 bytes of timecode
    unsigned int  trackheadersize = 0;

    // for mark5b there is no dependency on number of tracks
    if( fmt==fmt_mark5b ) {
        ntrack          = 1;
        trackheadersize = 16;
    }
    // both mark4/vlba have 4 bytes of syncword preceding the
    // 8 bytes of timecode (ie 12 bytes per track)
    if( fmt==fmt_mark4 || fmt==fmt_vlba )
        trackheadersize = 12;
    // mark4 has 8 pre-syncword bytes
    if( fmt==fmt_mark4 )
        trackheadersize += 8;
    // The full header for all tracks is just the number of tracks times the
    // size-per-track ...
    return (ntrack * trackheadersize);
}

#if 0
unsigned int framesize(format_type fmt) {
    ASSERT_COND(fmt==fmt_mark5b);
    return framesize(fmt, 0);
}
#endif

// Now always return a value. Unknown/unhandled formats get 0
// as framesize. Well, you get what you ask for I guess
// [XXX] - default behaviour may need to change if fmt_unknown/fmt_none
//         separate
unsigned int framesize(format_type fmt, unsigned int ntrack) {
    const unsigned int hsize = headersize(fmt, ntrack);

    switch( fmt ) {
        case fmt_mark5b:
            return hsize + 10000;
        case fmt_mark4:
            return hsize + (ntrack*2480);
        case fmt_vlba:
            return hsize + (ntrack*2500);
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
		FMTKEES(os, fmt_mark4,   "mark4");
		FMTKEES(os, fmt_vlba,    "vlba");
		FMTKEES(os, fmt_mark5b,  "mark5b");
        // [XXX] if fmt_none becomes its own type - do add it here!
		FMTKEES(os, fmt_unknown, "<unknown>");
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
			  << "framesize=" << h.framesize
			  << "]";
}

// syncwords for the various datastreams
// note: the actual definition of the mark4syncword is at the end of the file -
// it is a wee bit large - it accommodates up to 64 tracks of syncword.
extern unsigned char mark4_syncword[];
// mark5b syncword (0xABADDEED in little endian)
static unsigned char mark5b_syncword[] = {0xed, 0xde, 0xad, 0xab};



// ntrack only usefull if vlba||mark4
// [XXX] - if fmt_none becomes disctinct you may want/need to change this
//         default behaviour
#define MK4VLBA(fmt) \
    (fmt==fmt_mark4 || fmt==fmt_vlba)
#define SYNCWORDSIZE(fmt, n) \
    ((fmt==fmt_mark5b)?(sizeof(mark5b_syncword)):((MK4VLBA(fmt))?(n*4):0))
#define SYNCWORDOFFSET(fmt, n) \
    ((fmt==fmt_mark4)?(8*n):0)
#define SYNCWORD(fmt) \
    ((fmt==fmt_mark5b)?(&mark5b_syncword[0]):(MK4VLBA(fmt)?(&mark4_syncword[0]):0))

headersearch_type::headersearch_type():
	frameformat( fmt_unknown ), ntrack( 0 ),
	syncwordsize( 0 ), syncwordoffset( 0 ),
	headersize( 0 ), framesize( 0 ),
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
//	   Mk4 is datareplacement (headerbits are written over databits)
//	   VLBA is non-datareplacement
// * following the syncword are another 8 bytes of header. from
//     this we can compute the full headersize
headersearch_type::headersearch_type(format_type fmt, unsigned int tracks):
	frameformat( fmt ),
    ntrack( tracks ),
	syncwordsize( SYNCWORDSIZE(fmt, tracks) ),
	syncwordoffset( SYNCWORDOFFSET(fmt, tracks) ),
	headersize( ::headersize(fmt, tracks) ),
	framesize( ::framesize(fmt, tracks) ),
	syncword( SYNCWORD(fmt) )
{
    // Finish off with assertions ...
    if(MK4VLBA(frameformat) || frameformat==fmt_mark5b) {
	    ASSERT2_COND( ((ntrack>4) && (ntrack<=64) && (ntrack & (ntrack-1))==0),
                      SCINFO("ntrack (" << ntrack << ") is NOT a power of 2 which is >4 and <=64") );
    }
}


# if 0
// This construct only allows mark5b to be passed as argument
// * The syncwordsize + pattern are typical for mark5b
// * Mark5B diskframes have a fixed framesize, irrespective of number
//     of bitstreams recorded.
// * The syncword starts the frame, hence syncwordoffset==0
//     A total diskframe constist of 4 32bit words of header, followed
//     by 2500 32bit words of data, 32bits == 4 bytes
// * Following the syncword are 3 32bit words, making the
//     full headersize 4 times 32bit = 16 bytes
headersearch_type::headersearch_type(format_type fmt):
	frameformat( fmt ), ntrack( 0 ),
	syncwordsize( sizeof(mark5b_syncword) ), 
	syncwordoffset( 0 ),
	headersize( 16 ),
	framesize( (4 + 2500) * 4 ),
	syncword( &mark5b_syncword[0] )
{
	// Basic assertions on the arguments passed in
	ASSERT_COND( fmt==fmt_mark5b );
}

// This constructor only allows mark4/vlba formats
// * The syncwordsize + pattern is equal between VLBA and Mk4: 
//     4 x ntrack bytes of 0xFF
// * In Mk4 the syncword starts after the AUX data (8 bytes/track),
//     in VLBA at the start of the frame (the AUX data is at the end of the frame)
// * total framesize is slightly different:
//	   Mk4 is datareplacement (headerbits are written over databits)
//	   VLBA is non-datareplacement
// * following the syncword are another 8 bytes of header. from
//     this we can compute the full headersize
headersearch_type::headersearch_type(format_type fmt, unsigned int tracks):
	frameformat( fmt ), ntrack( tracks ),
	syncwordsize( ntrack * 4 ),
	syncwordoffset( ((frameformat==fmt_mark4)?(8 * ntrack):(0)) ),
	headersize( ntrack * ((frameformat==fmt_mark4)?(20):(12)) ),
	framesize( ntrack * ((frameformat==fmt_mark4)?(2500):(2520)) ),
	syncword( &mark4_syncword[0] )
{
	// Basic assertions on the arguments passed in
	//   (1) compatible format
	ASSERT_COND( (fmt==fmt_mark4 || fmt==fmt_vlba) );
	//   (2) number of tracks MUST be a power of two AND >4
	ASSERT2_COND( ((ntrack>4) && (ntrack<=64) && (ntrack & (ntrack-1))==0),
				  SCINFO("ntrack (" << ntrack << ") is NOT a power of 2 which is >4 and <=64") );
}
#endif


#if 0
void decode_mark4_timestamp(const void* /*hdr*/) {
    cout << "Mk4: " << ts->y << "Y" << ts->d0 << ts->d1 << ts->d2 << "d"
         << ts->h0 << ts->h1 << "h" << ts->m0 << ts->m1 << "m"
         << ts->s0 << ts->s1 << "." << ts->ss0 << ts->ss1 << ts->ss2 << "s"
         << endl;
}
#endif

#if 0
void decode_vlba_timestamp(const void* /*hdr*/) {
    cout << "VLBA: " << ts->j0 << ts->j1 << ts->j2 << " "
         << ts->s0 << ts->s1 << ts->s2 << ts->s3 << ts->s4 << "."
         << ts->ss0 << ts->ss1 << ts->ss2 << ts->ss3 << "s"
         << endl;
}
#endif



// CRC business
//typedef unsigned short CRCtype;

template <unsigned int CRCWidth, unsigned int Key>
struct crctable_type {
    // construct the crc table for a CRC of given Width and generating
    // polynomial key
    crctable_type() {
        const unsigned int polyorderbit( 1<<CRCWidth );
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
    }
    // Overload the functioncall operator. It takes a pointer
    // some databytes and the number of bytes to perform the CRC
    // computation over.
    unsigned int operator()(const unsigned char* data, unsigned int n) const {
        unsigned int  crc_register = 0;
        unsigned char top;
        while( n-- ) {
            top          = (crc_register>>(CRCWidth-8));
            crc_register = ((crc_register<<8)+*data++) ^ crc_table[top];
        }
        return crc_register;
    }
    static unsigned int crc_table[];
};
template <unsigned int CRCWidth, unsigned int Key>
unsigned int crctable_type<CRCWidth, Key>::crc_table[256];



// compute CRC12 (a la Mark4) on n bytes of data from idata
unsigned int crc12_mark4(const unsigned char* idata, unsigned int n) {
    // (CRC12) 100000001111 [generator polynomial]
    static const crctable_type<12, 0x80f> crc12t;

    // actually DO the crc computation and make sure the returnvalue
    // is a 12 bitter
    return (crc12t(idata, n)&0xfff); 
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
