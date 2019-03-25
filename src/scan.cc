#include <scan.h>

#include <iostream>
#include <limits>

DEFINE_EZEXCEPT(scanexception)

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
        THROW_EZEXCEPT(scanexception, "scanname longer than allowed ("
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

