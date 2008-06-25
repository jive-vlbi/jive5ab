// implementations
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
#include <userdir.h>
#include <evlbidebug.h>

using std::endl;

// the exception
DEFINE_EZEXCEPT(userdirexception);


// The RW version of the scanpointer
ScanPointer::ScanPointer():
    scanName( 0 ), scanStart( 0 ), scanLength( 0 )
{}

ScanPointer::ScanPointer(char* sn, unsigned long long* ss, unsigned long long* sl):
    scanName( sn ), scanStart( ss ), scanLength( sl )
{}

// return true if at least one of the pointers is null!
bool ScanPointer::empty( void ) const {
    return !(scanName && scanStart && scanLength);
}

std::string ScanPointer::name( void ) const {
    if( this->empty() )
        THROW_EZEXCEPT(userdirexception, "null-ScanPointer!");
    return std::string(scanName);
}
std::string ScanPointer::name( const std::string& n ) {
    if( this->empty() )
        THROW_EZEXCEPT(userdirexception, "null-ScanPointer!");
    if( n.size()>(Maxlength-1) )
        THROW_EZEXCEPT(userdirexception, "scanname longer than allowed ("
                << n.size() << " > " << (Maxlength-1) << ")");
    // Clear the current name
    ::memset(scanName, 0x0, Maxlength);
    // And copy the characters - note: since we zeroed the entry out
    // completely (Maxlength zeroes) and we've ascertained that
    // n.length() is at max 'Maxlength-1', there's at least one null-byte
    // left after the copy - this effectively null terminates the string.
    // Joepie.
    ::strcpy(scanName, n.c_str());

    return n;
}

unsigned long long ScanPointer::start( void ) const {
    if( this->empty() )
        THROW_EZEXCEPT(userdirexception, "null-ScanPointer!");
    return *scanStart;
}
unsigned long long ScanPointer::start( unsigned long long s ) {
    if( this->empty() )
        THROW_EZEXCEPT(userdirexception, "null-ScanPointer!");
    return ((*scanStart)=s);
}
unsigned long long ScanPointer::length( void ) const {
    if( this->empty() )
        THROW_EZEXCEPT(userdirexception, "null-ScanPointer!");
    return *scanLength;
}
unsigned long long ScanPointer::length( unsigned long long l ) {
    if( this->empty() )
        THROW_EZEXCEPT(userdirexception, "null-ScanPointer!");
    return ((*scanLength)=l);
}

std::ostream& operator<<( std::ostream& os, const ScanPointer& sp ) {
    if( sp.empty() )
        os << "<null>";
    else
        os << '"' << sp.name() << "\" S:" << sp.start() << " L:" << sp.length();
    return os;
}

// The RO version
ROScanPointer::ROScanPointer( const char* sn, const unsigned long long* ss,
                              const unsigned long long* sl ):
    scanName( sn ), scanStart( ss ), scanLength( sl )
{}

ROScanPointer::ROScanPointer( const ROScanPointer& o ):
    scanName( o.scanName ), scanStart( o.scanStart ), scanLength( o.scanLength )
{}

std::string ROScanPointer::name( void ) const {
    return std::string( scanName );
}

unsigned long long ROScanPointer::start( void ) const {
    return *scanStart;
}
unsigned long long ROScanPointer::length( void ) const {
    return *scanLength;
}
std::ostream& operator<<( std::ostream& os, const ROScanPointer& sp ) {
    os << '"' << sp.name() << "\" S:" << sp.start() << " L:" << sp.length();
    return os;
}



// Create a zero-filled ScanDirectory
ScanDir::ScanDir() {
    ::memset(this, 0x00, sizeof(ScanDir));
}

unsigned int ScanDir::nScans( void ) const {
    if( nRecordedScans<0 )
        THROW_EZEXCEPT(userdirexception, "Negative number of recorded scans?!");
    // Ok, nRecordedScans is non-negative so cast to unsigned int is safe
    return (unsigned int)nRecordedScans;
}

// Attempt to access scan 'scan'. Throws up
// if *anything* is fishy. Only returns if
// the requested scan can sensibly adressed
ROScanPointer ScanDir::operator[]( unsigned int scan ) const {
    // Check if there *are* recorded scans && the requested one
    // is within the recorded range
    if( nRecordedScans>0 /* integer comparison */ &&
        scan<(unsigned int)nRecordedScans /* unsigned, but is safe! */ )
        return ROScanPointer(&scanName[scan][0], &scanStart[scan], &scanLength[scan]);
    // Otherwise ...
    THROW_EZEXCEPT(userdirexception, "requested scan (#" << scan << ") out-of-range: "
                   << " nRecorded=" << nRecordedScans);  
}

ScanPointer ScanDir::getNextScan( void ) {
    ScanPointer     rv;

    // Assert sanity of current state.
    // That is: check if there's room for a new
    // recorded scan.
    // nRecorded < 0 => internal state bollox0red
    // nRecorded must also be < Maxscans, otherwise
    // there's no room for a new scan
    if( nRecordedScans<0 )
        THROW_EZEXCEPT(userdirexception, "nRecordedScans<0 makes no sense!");
    if( (unsigned int)nRecordedScans>=Maxscans )
        THROW_EZEXCEPT(userdirexception, "scanDirectory is full!");

    // Allocate the new Scan
    rv = ScanPointer( &scanName[nRecordedScans][0],
                      &scanStart[nRecordedScans],
                      &scanLength[nRecordedScans] );

    nRecordedScans++;
    return rv;
}

unsigned long long ScanDir::recordPointer( void ) const {
    return _recordPointer;
}

void ScanDir::recordPointer( unsigned long long newrecptr ) {
    _recordPointer = newrecptr;
}

long long ScanDir::playPointer( void ) const {
    return _playPointer;
}

void ScanDir::playPointer( long long newpp ) {
    _playPointer =  newpp;
}

double ScanDir::playRate( void ) const {
    return _playRate;
}

void ScanDir::playRate( double newpr ) {
    _playRate = newpr;
}

// If we detect incompatible settings, clean ourselves out
void ScanDir::sanitize( void ) {
    if( nRecordedScans<0 || (unsigned int)nRecordedScans>Maxscans ||
        nextScan<0 || (unsigned int)nextScan>=Maxscans ) {
        ::memset(this, 0x0, sizeof(ScanDir));
        DEBUG(0, "Detected fishiness in 'sanitize'. Cleaning out ScanDir!" << std::endl);
    }
}

// the actual UserDirectory
UserDirectory::UserDirectory():
    rawBytes( 0 ), dirStart( 0 ), dirLayout( CurrentLayout )
{
    this->init();
}

UserDirectory::UserDirectory( const xlrdevice& xlr ):
    rawBytes( 0 ), dirStart( 0 ), dirLayout( UnknownLayout )
{
    //read() clears the internals ...
    this->read( xlr );
}

UserDirectory::UserDirectory( const UserDirectory& o ):
    rawBytes( 0 ), dirStart( 0 ), dirLayout( o.dirLayout ) 
{
    this->init();
    ::memcpy(rawBytes, o.rawBytes, nBytes);
}

bool UserDirectory::operator==( const UserDirectory& o ) const {
    return (dirLayout==o.dirLayout && ::memcmp(rawBytes, o.rawBytes, nBytes)==0);
}

bool UserDirectory::operator!=( const UserDirectory& o ) const {
    return !(this->operator==(o));
}


const UserDirectory& UserDirectory::operator=( const UserDirectory& o ) {
    if( this!=&o ) {
        this->init();
        dirLayout = o.dirLayout;
        ::memcpy(rawBytes, o.rawBytes, nBytes);
    }
    return *this;
}

ScanDir& UserDirectory::scanDir( void ) {
    if( dirLayout==UnknownLayout )
        THROW_EZEXCEPT(userdirexception, "scanDir is inaccessible in current layout - "
                       << dirLayout);
    return *((ScanDir*)dirStart);
}

VSN8& UserDirectory::vsn8( void ) {
    if( dirLayout!=VSNVersionOne )
        THROW_EZEXCEPT(userdirexception, "VSN8 is inaccessible in current layout - "
                       << dirLayout);
    return *((VSN8*)(dirStart+sizeof(ScanDir)));
}
VSN16& UserDirectory::vsn16( void ) {
    if( dirLayout!=VSNVersionTwo )
        THROW_EZEXCEPT(userdirexception, "VSN16 is inaccessible in current layout - "
                       << dirLayout);
    return *((VSN16*)(dirStart+sizeof(ScanDir)));
}

UserDirectory::Layout UserDirectory::getLayout( void ) const {
    return dirLayout;
}

void UserDirectory::read( const xlrdevice& xlr ) {
    S_DEVSTATUS     devStatus;

    // If device is not idle -> throw up
    XLRCALL( ::XLRGetDeviceStatus(xlr.sshandle(), &devStatus) );
    if( devStatus.Recording || devStatus.Playing )
        THROW_EZEXCEPT(userdirexception, "System is not idle, reading UserDirectory");

    // re-init. We want to start from a clean slate
    this->init();

    // Great. Check the UserDir length and see what we can make of it.
    dirLayout = (enum Layout) (::XLRGetUserDirLength(xlr.sshandle()));
    switch( dirLayout ) {
        // These we recognize
        case OriginalLayout:
        case VSNVersionOne:
        case VSNVersionTwo:
            XLRCALL( ::XLRGetUserDir(xlr.sshandle(), dirLayout, 0, dirStart) );
            break;
        default:
            // unrecognized?
            // I'd hope that:
            //    "No disks in device" => UserDirLength==0 (==UnknownLayout)
            if( dirLayout!=UnknownLayout )
                DEBUG(0, "Found incompatible UserDirLength of "
                         << ::XLRGetUserDirLength(xlr.sshandle()) << endl);
            // force it to unknown
            dirLayout = UnknownLayout;
            break;
    }
#if 0
    // if still unknownlayout, the directory wasn't in the "userdir"
    // area. Try the "inline directory".
    if( dirLayout==UnknownLayout ) {
        S_DIR   currentDir;

        DEBUG(0, "UserDir::read/ trying inline DIR" << endl);

        XLRCALL( ::XLRGetDirectory(xlr.sshandle(), &currentDir) );
        dirLayout = (enum Layout)currentDir.AppendLength;
        DEBUG(0, "UserDir::read/ currentDir.AppendLength=" << dirLayout 
                << " (" << currentDir.AppendLength << ")" << endl);
        switch( dirLayout ) {
            // These we recognize
            case OriginalLayout:
                XLRCALL( ::XLRGetUserDir(xlr.sshandle(), dirLayout, 0, dirStart) );
                break;
            default:
                // unrecognized?
                dirLayout = UnknownLayout;
                break;
        }
    }
#endif
    // Sanitize - if not UnknownLayout
    if( dirLayout!=UnknownLayout )
        this->scanDir().sanitize();
}

void UserDirectory::write( const xlrdevice& xlr ) {
    S_DEVSTATUS     devStatus;

    // If device is not idle -> throw up
    XLRCALL( ::XLRGetDeviceStatus(xlr.sshandle(), &devStatus) );
    if( devStatus.Recording || devStatus.Playing )
        THROW_EZEXCEPT(userdirexception, "System is not idle, writing UserDirectory");

    // Attempt to write the userdirectory.
    switch( dirLayout ) {
        // These we recognize - this includes 'UnknownLayout' (ie 0(zero))
        case UnknownLayout:
        case OriginalLayout:
        case VSNVersionOne:
        case VSNVersionTwo:
            XLRCALL( ::XLRSetUserDir(xlr.sshandle(), (void*)dirStart, dirLayout) );
            break;
        default:
            // unrecognized?
            THROW_EZEXCEPT(userdirexception, "Attempt to write incompatible UserDirLength of "
                    << (unsigned int)dirLayout << endl);
            break;
    }
    return;
}


UserDirectory::~UserDirectory() {
    delete [] rawBytes;
    dirStart = 0;
    rawBytes = 0;
}

void UserDirectory::init( void ) {
    if( !rawBytes )
        rawBytes = new unsigned char[ nBytes ];
    ::memset( rawBytes, 0x0, nBytes );
    //dirStart  = (rawBytes + 0x7) / 8;
    dirStart = rawBytes;
}

#define CEES(a, o) \
    case UserDirectory::a: o << #a; break;

std::ostream& operator<<( std::ostream& os, UserDirectory::Layout layout ) {
    switch( layout ) {
        CEES(UnknownLayout, os);
        CEES(OriginalLayout, os);
        CEES(VSNVersionOne, os);
        CEES(VSNVersionTwo, os);
        default:
            os << "<Invalid layout?!>";
    }
    return os;
}
#undef CEES
