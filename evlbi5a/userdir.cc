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
#include <xlrdevice.h>

#include <fstream>
#include <string.h>

using std::endl;

// the exception
DEFINE_EZEXCEPT(userdirexception)

const unsigned int ROScanPointer::invalid_scan_index = std::numeric_limits<unsigned int>::max();

// The RO version
ROScanPointer::ROScanPointer( ) : scan_index( invalid_scan_index ) {
}

ROScanPointer::ROScanPointer( unsigned int i ) : scan_index( i ) {
}

ROScanPointer::ROScanPointer( std::string sn, uint64_t ss, uint64_t sl,
                              unsigned int i ):
scanName( sn ), scanStart( ss ), scanLength( sl ), scan_index( i )
{}

std::string ROScanPointer::name( void ) const {
    return scanName;
}

uint64_t ROScanPointer::start( void ) const {
    return scanStart;
}

uint64_t ROScanPointer::length( void ) const {
    return scanLength;
}

unsigned int ROScanPointer::index( void ) const {
    return scan_index;
}

std::string ROScanPointer::strip_asterisk( std::string n ) {
    if ( !n.empty() && (*(n.end() - 1) == '*') ) {
        return n.substr( 0, n.size() - 1 );
    }
    else {
        return n;
    }
}

std::ostream& operator<<( std::ostream& os, const ROScanPointer& sp ) {
    os << '"' << sp.name() << "\" S:" << sp.start() << " L:" << sp.length();
    return os;
}

const user_dir_identifier_type ScanPointer::invalid_user_dir_id = std::numeric_limits<user_dir_identifier_type>::max();

// The RW version of the scanpointer
ScanPointer::ScanPointer( ) : 
    ROScanPointer( invalid_scan_index ), user_dir_id( invalid_user_dir_id )
{}

ScanPointer::ScanPointer( unsigned int i ) :
    ROScanPointer( i ), user_dir_id( invalid_user_dir_id )
{}

std::string ScanPointer::setName( const std::string& n ) {
    if( n.size()>(Maxlength-1) )
        THROW_EZEXCEPT(userdirexception, "scanname longer than allowed ("
                << n.size() << " > " << (Maxlength-1) << ")");
    scanName = n;
    return n;
}

uint64_t ScanPointer::setStart( uint64_t s ) {
    return scanStart=s;
}

uint64_t ScanPointer::setLength( uint64_t l ) {
    return scanLength=l;
}

std::ostream& operator<<( std::ostream& os, const ScanPointer& sp ) {
    os << '"' << sp.name() << "\" S:" << sp.start() << " L:" << sp.length();
    return os;
}



// Create a zero-filled ScanDirectory
ScanDir::ScanDir() {
    memset(this, 0x00, sizeof(ScanDir));
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
        return ROScanPointer(&scanName[scan][0], scanStart[scan], scanLength[scan], scan);
    // Otherwise ...
    THROW_EZEXCEPT(userdirexception, "requested scan (#" << scan << ") out-of-range: "
                   << " nRecorded=" << nRecordedScans);  
}

ScanPointer ScanDir::getNextScan( void ) {
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
    return ScanPointer( nRecordedScans++ );
}

uint64_t ScanDir::recordPointer( void ) const {
    return _recordPointer;
}

void ScanDir::recordPointer( uint64_t newrecptr ) {
    _recordPointer = newrecptr;
}

uint64_t ScanDir::playPointer( void ) const {
    return _playPointer;
}

void ScanDir::playPointer( uint64_t newpp ) {
    _playPointer =  newpp;
}

double ScanDir::playRate( void ) const {
    return _playRate;
}

void ScanDir::playRate( double newpr ) {
    _playRate = newpr;
}

void ScanDir::setScan( const ScanPointer& scan ) {
    // some sanity checks
    if( (int)scan.index() >= nRecordedScans )
        THROW_EZEXCEPT(userdirexception, "scan index larger than number of recorded scans!");
    if ( scan.name().size() >= Maxlength ) 
        THROW_EZEXCEPT(userdirexception, "scan name too long!");

    strncpy(&scanName[scan.index()][0], scan.name().c_str(), Maxlength);
    scanStart[scan.index()] = scan.start();
    scanLength[scan.index()] = scan.length();
}

void ScanDir::clear_scans( void ) {
    nRecordedScans = 0;
}

void ScanDir::remove_last_scan( void ) {
    if ( nRecordedScans <= 0 ) {
        THROW_EZEXCEPT(userdirexception, "no scan to remove in scanDir");
    }
    nRecordedScans--;
}

// If we detect incompatible settings, clean ourselves out
void ScanDir::sanitize( void ) {
    if( nRecordedScans<0 || (unsigned int)nRecordedScans>Maxscans ||
        nextScan<0 || (unsigned int)nextScan>=Maxscans ) {
        ::memset(this, 0x0, sizeof(ScanDir));
        DEBUG(0, "Detected fishiness in 'sanitize'. Cleaning out ScanDir!" << std::endl);
    }
}

void ScanDir::recover( uint64_t recovered_record_pointer ) {
    _recordPointer = recovered_record_pointer;
    int last_scan = (int)nRecordedScans - 1;
    if ( (last_scan >= 0) &&
         (scanStart[last_scan] + scanLength[last_scan] < recovered_record_pointer) ) {
        scanLength[last_scan] = recovered_record_pointer - scanStart[last_scan];
    }
    else {
        scanStart[0] = 0;
        scanLength[0] = recovered_record_pointer;
        strncpy(&scanName[0][0], "recovered scan", Maxlength);
        nRecordedScans++;
    }
}


user_dir_identifier_type UserDirectory::next_user_dir_id = 0;

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

ScanDir& UserDirectory::scanDir( void ) const {
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
    // Serialize access to the StreamStor.
    do_xlr_lock();
    dirLayout = (enum Layout) (::XLRGetUserDirLength(xlr.sshandle()));
    do_xlr_unlock();
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
                         << (unsigned int)dirLayout << endl);
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
    if( dirLayout!=UnknownLayout ) {
        this->scanDir().sanitize();
        try_write_dirlist();
    }
}

void UserDirectory::write( const xlrdevice& XLRCODE(xlr) ) {
    S_DEVSTATUS     devStatus;

    ::memset(&devStatus, 0, sizeof(S_DEVSTATUS));
    // If device is not idle -> throw up
    XLRCALL( ::XLRGetDeviceStatus(xlr.sshandle(), &devStatus) );
    if( devStatus.Recording || devStatus.Playing ) {
        THROW_EZEXCEPT(userdirexception, "System is not idle, writing UserDirectory");
    }

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
    try_write_dirlist();
    return;
}

ROScanPointer UserDirectory::getScan( unsigned int index ) const {
    return scanDir()[index];
}

ScanPointer UserDirectory::getNextScan( ) {
    ScanPointer scan = scanDir().getNextScan();
    scan.user_dir_id = id;
    return scan;
}

void UserDirectory::setScan( const ScanPointer& scan ) {
    if ( scan.user_dir_id != id ) {
        THROW_EZEXCEPT(userdirexception, "scan's ID does not match user directory's ID, user directory changed while operating on scan");
    }
    scanDir().setScan( scan );
}

unsigned int UserDirectory::nScans( void ) const {
    return scanDir().nScans();
}

void UserDirectory::clear_scans( void ) {
    ScanDir& sd = scanDir();
    sd.clear_scans();
    sd.recordPointer(0);
    sd.playPointer(0);
}

void UserDirectory::remove_last_scan( void ) {
    ScanDir& sd = scanDir();
    sd.remove_last_scan();
    unsigned int scans = sd.nScans();
    if ( scans > 0 ) {
        ROScanPointer scan = sd[scans - 1];
        uint64_t end = scan.start() + scan.length();
        if ( sd.recordPointer() > end )  {
            sd.recordPointer( end );
        }
        if ( sd.playPointer() > end ) {
            sd.playPointer( end );
        }
    }
    else {
        sd.recordPointer( 0 );
        sd.playPointer( 0 );
    }
}

UserDirectory::~UserDirectory() {
    delete [] rawBytes;
    dirStart = 0;
    rawBytes = 0;
}

void UserDirectory::init( void ) {
    if( !rawBytes ) {
        rawBytes = new unsigned char[ nBytes ];
    }
    ::memset( rawBytes, 0x0, nBytes );
    id = next_user_dir_id++;
    //dirStart  = (rawBytes + 0x7) / 8;
    dirStart = rawBytes;
}

void UserDirectory::try_write_dirlist( void ) const {
    try {
        std::ofstream file;
        file.exceptions ( std::ofstream::failbit | std::ofstream::badbit );
        file.open( "/var/dir/Mark5A", std::ios_base::out | std::ios_base::trunc | std::ios_base::binary );
        file.write( (const char*)&(scanDir()), sizeof(ScanDir) );
        file.close();
    }
    catch (std::exception& e) {
        DEBUG( -1, "Failed to write DirList, exception: " << e.what() << std::endl);
    }
    catch ( ... ) {
        DEBUG( -1, "Failed to write DirList, unknown exception" << std::endl);
    }
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
