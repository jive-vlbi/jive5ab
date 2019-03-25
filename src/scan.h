#ifndef JIVE5A_SCAN_H
#define JIVE5A_SCAN_H

#include <stdint.h> // for [u]int<N>_t  types
#include <string>

#include <ezexcept.h>

DECLARE_EZEXCEPT(scanexception)

// These were #define's in Mark5A
// for the max length of an extended scan name (including null-byte!)
const unsigned int Maxlength = 64;
const unsigned int VSNLength = 64;

typedef unsigned long long int user_dir_identifier_type;

struct UserDirectory;
class  xlrdevice;

class ROScanPointer {
 public:
    friend struct UserDirectory;
    friend class  xlrdevice;
        
    static const unsigned int invalid_scan_index;
    // creates an invalid ROScanPointer (scan_index == invalid)
    ROScanPointer();
        
    // constructors
    ROScanPointer(std::string sn, uint64_t ss, uint64_t sl,
                  unsigned int scan_index );

    ROScanPointer( unsigned int scan_index );

    std::string      name( void ) const;
    uint64_t         start( void ) const;
    uint64_t         length( void ) const;
    unsigned int     index( void ) const;

    // an '*' will be appended to the scan name while it is being recorded
    // if the user knows it is a scan being recorded, we might want to strip it
    static std::string strip_asterisk( std::string name );

 protected:
    std::string scanName;
    uint64_t scanStart;
    uint64_t scanLength;
    unsigned int scan_index;


};

std::ostream& operator<<( std::ostream& os, const ROScanPointer& sp );

class ScanPointer : public ROScanPointer {
 public:
    friend struct UserDirectory;
    friend class  xlrdevice;
        
    ScanPointer();

    // constructors
    ScanPointer( unsigned int scan_index );

 protected:
    static const user_dir_identifier_type invalid_user_dir_id;
    user_dir_identifier_type user_dir_id;

    // The "set" methods will return the new value.
    uint64_t      setStart( uint64_t s );
    uint64_t      setLength( uint64_t l );
    std::string   setName( const std::string& n );
        
};

std::ostream& operator<<( std::ostream& os, const ScanPointer& sp );

#endif
