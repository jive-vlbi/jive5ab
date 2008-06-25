// stores recorded scan-info on the StreamStor
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
#ifndef JIVE5A_USERDIR_H
#define JIVE5A_USERDIR_H

#include <iostream>
#include <string>

// ..
#include <ezexcept.h>
#include <xlrdevice.h>


DECLARE_EZEXCEPT(userdirexception);

// structures as used by Mark5A.c
// There seem to be >1 versions of the UserDirectory on the
// Mark5. They are discernable only by size.
// 1) just the ScanDir
// 2) ScanDir + VSN + 8*diskinfo
// 3) ScanDir + VSN + 16*diskinfo
//
// There will be one object (UserDir) which
// will encapsulate the various flavours and will
// do sanity checks for you.

// These were #define's in Mark5A
// for the max. # of scans that may be in the SS UserDir
// and the max length of an extended scan name (including null-byte!)
const unsigned int Maxscans  = 1024;
const unsigned int Maxlength = 64;
const unsigned int VSNLength = 64;


// Use this struct as the interface between the s/w
// and how that info is stored on disk
// The default c'tor creates an entry with null-pointers.
// Attempts to dereference values will result in throwance of
// exceptions.
// scanName, if non-null, MUST point at a characterarray
// of minimum size 'Maxlength' (see above).
// Attempts to assign a string of length > (Maxlength-1) will also
// result in throwance of an exception. The system will not silently
// truncate!
struct ScanPointer {
    // constructors
    ScanPointer();
    ScanPointer(char* sn, unsigned long long* ss, unsigned long long* sl);

    // returns true if this is a null-scanpointer
    bool empty( void ) const;

    // r/w access. These functions do check for null-pointer
    // dereference.
    // The "set" methods will return the new value.
    std::string        name( void ) const;
    std::string        name( const std::string& n );

    unsigned long long start( void ) const;
    unsigned long long start( unsigned long long s );

    unsigned long long length( void ) const;
    unsigned long long length( unsigned long long l );

    private:
        char*               scanName;
        unsigned long long* scanStart;
        unsigned long long* scanLength;
};

std::ostream& operator<<( std::ostream& os, const ScanPointer& sp );


struct ROScanPointer {
    // The ROScanPointer can *only* be fully constructed.
    // No default c'tor
    ROScanPointer(const char* sn, const unsigned long long* ss,
                  const unsigned long long *sl );
    // Copy is Ok. Assignment is automatically impossible
    // since we have const datamembers.
    ROScanPointer( const ROScanPointer& o );

    std::string         name( void ) const;
    unsigned long long  start( void ) const;
    unsigned long long  length( void ) const;

    private:
        const char* const               scanName;
        const unsigned long long* const scanStart;
        const unsigned long long* const scanLength;

        // prohibit this'un
        ROScanPointer();

};
std::ostream& operator<<( std::ostream& os, const ROScanPointer& sp );


// The original scandirectory
struct ScanDir {

    // Creates an empty scandirectory, everything nicely
    // zeroed
    ScanDir();

    // The number of recorded scans
    unsigned int  nScans( void ) const;

    // Get access to scans. Detects if you're trying to
    // access outside the recorded scans.
    ROScanPointer operator[]( unsigned int scan ) const;

    // If you want to record a new scan, use this method
    // to get the next writable entry and do *not* forget
    // the values you got. The system will internally
    // bump the nRecordedScans as soon as you call this
    // one. The "ScanPointer" you got will be the only
    // writable entry to the scan.
    ScanPointer         getNextScan( void );

    unsigned long long  recordPointer( void ) const;
    void                recordPointer( unsigned long long newrecptr );

    long long           playPointer( void ) const;
    void                playPointer( long long newpp );

    double              playRate( void ) const;
    void                playRate( double newpr );

    // hmmm ...
    // this is to sanitize. If it detects possible fishyness
    // (nRecordedScans<0, nextScan<0, nRecordedScans>maxscans,
    // nextScan>=maxscans ) it resets itself to "empty".
    // Needed for fixing after reading this from StreamStor - 
    // you never can be *really* sure that you read a ScanDir
    // - maybe you read some data. This is how it's done in Mark5A.
    // And we should try to mimick the behaviour
    void sanitize( void );

    private:
        // Actual # of recorded scans
        int                nRecordedScans;
        // "pointer" to next_scan for "next_scan"?
        int                nextScan;
        // Arrays of scannames, startpos + lengths
        char               scanName[Maxscans][Maxlength];
        unsigned long long scanStart[Maxscans]; /* Start byte position */ 
        unsigned long long scanLength[Maxscans]; /* Length in bytes */ 
        // Current record and playback pointers
        unsigned long long _recordPointer;
        long long          _playPointer;
        double             _playRate;
};


// We need (at least) two versions of VSN
template <unsigned int nDisks>
struct VSN {
    typedef VSN<nDisks>  self_type;
    // create zero-filled instance
    VSN() {
        ::memset(this, 0x00, sizeof(self_type));
    }

    std::string  getVSN( void ) {
        return std::string(actualVSN);
    }
    // const and non-const access to the driveinfos
    const S_DRIVEINFO& operator[]( unsigned int disk ) const {
        if( disk>=nDisks )
            THROW_EZEXCEPT(userdirexception, "requested disk#" << disk << 
                    " out of range (max is " << nDisks << ")");
        return driveInfo[disk];
    }
    private:
        char         actualVSN[VSNLength];
        S_DRIVEINFO  driveInfo[nDisks];
};

typedef VSN<8>  VSN8;
typedef VSN<16> VSN16;


// The encapsulating structure. Use this as your primary
// entrance to the StreamStor's userdirectory
struct UserDirectory {
    // This enumerates the currently known layouts
    enum Layout {
        UnknownLayout  = 0,
        OriginalLayout = sizeof(ScanDir),
        VSNVersionOne  = sizeof(ScanDir)+sizeof(VSN8),
        VSNVersionTwo  = sizeof(ScanDir)+sizeof(VSN16),
        CurrentLayout  = VSNVersionTwo
    };

    // The maximum number of bytes the user directory may
    // contain
    static const unsigned int  nBytes = XLR_MAX_UDIR_LENGTH+8;

    // default c'tor. It is empty (no scans etc)
    // but has "CurrentLayout" layout.
    UserDirectory();

    // issues this->read(xlr).
    UserDirectory( const xlrdevice& xlr );

    // Copy and assignment.
    // These take care of makeing sure pointers point
    // into own buffers etc ...
    UserDirectory( const UserDirectory& o );
    const UserDirectory& operator=( const UserDirectory& o );

    bool operator==( const UserDirectory& o ) const;
    bool operator!=( const UserDirectory& o ) const;

    // Get access to the constituent members.
    // Note that accessing VSN[8|16] whilst the layout-on-disk
    // seems to indicate that those are NOT present, will
    // result in exceptional behaviour ...
    // The 'getLayout()' will tell you which layout was detected,
    // if any

    // Always available (apart from "UnknownLayout")
    ScanDir&    scanDir( void );
    // Only available if Layout==VSNVersionOne
    VSN8&       vsn8( void );
    // Only available if Layout==VSNVersionTwo
    VSN16&      vsn16( void );

    Layout      getLayout( void ) const;

    // read/write to streamstor device.
    // if the device is recording/playbacking, 
    // throwance of exceptions will be your part.
    void    read( const xlrdevice& xlr );
    void    write( const xlrdevice& xlr );

    ~UserDirectory();

    private:
        // Keep a characterbuffer of XLR_MAX_UDIR_LENGTH+8
        // (In order to be able to to an xlrread/write, for
        // inline directory, the address has to be eight-byte-aligned).
        unsigned char*  rawBytes;
        // pointer to 8-byte-aligned address within rawBytes
        unsigned char*  dirStart;

        // The current detected layout
        Layout          dirLayout;

        // clears 'rawBytes' to 0x0 and lets 'dirStart' point
        // at the first 8-byte-aligned address in 'rawBytes'
        void            init( void );
};
// Show layout in human readable format
std::ostream& operator<<( std::ostream& os, UserDirectory::Layout l );

#endif
