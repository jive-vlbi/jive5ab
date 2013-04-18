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

#include <stdint.h> // for [u]int<N>_t  types

// ..
#include <ezexcept.h>
#include <xlrdefines.h>
#include <stringutil.h>
#include <userdir_layout.h>
#include <scan.h>

// structures as used by Mark5A.c
// There seem to be >1 versions of the UserDirectory on the
// Mark5. They are discernable only by size.
// 1) just the ScanDir
// 2) ScanDir + VSN + 8*diskinfo
// 3) ScanDir + VSN + 16*diskinfo
// 4) Enhanced Directory (Mark5C)
//
// Of the 2nd and 3rd there are again multiple flavors (1024 or 65536 scans, 
// SDK8 or SDK9, Bank B VSN stored or not)
// To make matters worse, some of those flavor have the same size, 
// we will make an educated guess which seems the most correct one.

// There will be one object (UserDir) which
// will encapsulate the various flavours and will
// do sanity checks for you.

DECLARE_EZEXCEPT(userdir_enosys)
DECLARE_EZEXCEPT(userdir_impossible_layout)

struct UserDirInterface {
    // abstract class, all access functions will throw:
#define THROW_USERDIR_ENOSYS                    \
    {                                           \
        throw userdir_enosys();                 \
    }                                           \

    // should always be implemented, returning the amount of bytes used
    virtual unsigned int size() const = 0;

    // if size is not enough to decide which layout to use
    // this function will return the "unlikeliness" of it being a correct interpretation
    virtual unsigned int insanityFactor() const {
        return 0;
    }
    
    // will try to detect problems and fix them
    virtual void sanitize() THROW_USERDIR_ENOSYS;

    // the amount of bytes to write to DirList file 
    // the bytes are taken from the start
    // (if it makes sense at all, otherwise will return 0)
    virtual unsigned int dirListSize() const {
        return 0;
    }

    // scan functions
    virtual ROScanPointer getScan( unsigned int /*index*/ ) const THROW_USERDIR_ENOSYS;
    virtual ScanPointer getNextScan( void ) THROW_USERDIR_ENOSYS;
    virtual void setScan( const ScanPointer& /*scan*/ ) THROW_USERDIR_ENOSYS;
    virtual unsigned int nScans( void ) const THROW_USERDIR_ENOSYS;

    // erase functions
    virtual void clear_scans( void ) THROW_USERDIR_ENOSYS;
    virtual void remove_last_scan( void ) THROW_USERDIR_ENOSYS;
    virtual void recover( uint64_t /*recovered_record_pointer*/ ) THROW_USERDIR_ENOSYS;

    // disk info cache functions
    virtual std::string getVSN() const THROW_USERDIR_ENOSYS;
    virtual void setVSN( std::string& /*vsn*/ ) THROW_USERDIR_ENOSYS;
    virtual void getDriveInfo( unsigned int /*disk*/, S_DRIVEINFO& /*out*/ ) const THROW_USERDIR_ENOSYS;
    virtual void setDriveInfo( unsigned int /*disk*/, S_DRIVEINFO& /*in*/ ) THROW_USERDIR_ENOSYS;
    virtual unsigned int numberOfDisks() const THROW_USERDIR_ENOSYS;
    
#undef THROW_USERDIR_ENOSYS
    
    virtual ~UserDirInterface() {}

    // implementation of this abstract class have to implement the following 
    // 2 constructors

    // an emtpy interface of appropriate length at 'start'
    //UserDirInterface( unsigned char * start );
    
    // interpret the 'length' bytes at 'start' as the layout corresponding to the type
    //UserDirInterface( unsigned char * start, unsigned int length );
    
};

struct OriginalLayout : public UserDirInterface {
    OriginalLayout( unsigned char* start );
    OriginalLayout( unsigned char* start, unsigned int length );

    virtual unsigned int size() const;
    virtual unsigned int dirListSize() const;

    virtual unsigned int insanityFactor() const;
    
    virtual void sanitize();

    // scan functions
    virtual ROScanPointer getScan( unsigned int index ) const;
    virtual ScanPointer getNextScan( void );
    virtual void setScan( const ScanPointer& scan );
    virtual unsigned int nScans( void ) const;

    // erase functions
    virtual void clear_scans( void );
    virtual void remove_last_scan( void );
    virtual void recover( uint64_t recovered_record_pointer );

    // disk info cache functions are not implemented, 
    // they'll still throw
 private:
    typedef ScanDir<1024> ScanDirLayout;

    OriginalLayout();
    
    ScanDirLayout* scanDir;
};

template <unsigned int Maxscans, unsigned int nDisks, typename DriveInfo, bool BankB>
struct Mark5ABLayout : public UserDirInterface {
    Mark5ABLayout( unsigned char* start ) {
        scanDirPointer = (ScanDirLayout*)start;
        diskInfoCachePointer = (DiskInfoCacheLayout*)(start + sizeof(ScanDirLayout));

        scanDirPointer->clear();
        diskInfoCachePointer->clear();
    }

    Mark5ABLayout( unsigned char* start, unsigned int length ) {
        if ( length != (sizeof(ScanDirLayout) + sizeof(DiskInfoCacheLayout)) ) {
            throw userdir_impossible_layout();
        }
        scanDirPointer = (ScanDirLayout*)start;
        diskInfoCachePointer = (DiskInfoCacheLayout*)(start + sizeof(ScanDirLayout));
    }

    virtual unsigned int size() const {
        return sizeof(ScanDirLayout) + sizeof(DiskInfoCacheLayout);
    }

    virtual unsigned int dirListSize() const {
        return sizeof(ScanDirLayout);
    }

    virtual unsigned int insanityFactor() const {
        return ((ScanDirLayout*)scanDirPointer)->insanityFactor() +
            diskInfoCachePointer->insanityFactor();
    }

    virtual void sanitize( void ) {
        ((ScanDirLayout*)scanDirPointer)->sanitize();
    }

    virtual ROScanPointer getScan( unsigned int index ) const {
        return ((ScanDirLayout*)scanDirPointer)->getScan( index );
    }
    virtual ScanPointer getNextScan( void ) {
        return ((ScanDirLayout*)scanDirPointer)->getNextScan();
    }
    virtual void setScan( const ScanPointer& scan ) {
        ((ScanDirLayout*)scanDirPointer)->setScan( scan );
    }
    virtual unsigned int nScans( void ) const {
        return ((ScanDirLayout*)scanDirPointer)->nScans();
    }

    virtual void clear_scans( void ) {
        ((ScanDirLayout*)scanDirPointer)->clear_scans();
    }
    virtual void remove_last_scan( void ) {
        ((ScanDirLayout*)scanDirPointer)->remove_last_scan();
    }
    virtual void recover( uint64_t recovered_record_pointer ) {
        ((ScanDirLayout*)scanDirPointer)->recover( recovered_record_pointer );
    }

    virtual std::string getVSN( void ) const {
        return diskInfoCachePointer->getVSN();
    }
    virtual void setVSN( std::string& vsn ) {
        diskInfoCachePointer->setVSN( vsn );
    }
    virtual void getDriveInfo( unsigned int disk, S_DRIVEINFO& out ) const {
        diskInfoCachePointer->getDriveInfo( disk, out );
    }
    virtual void setDriveInfo( unsigned int disk, S_DRIVEINFO& in ) {
        diskInfoCachePointer->setDriveInfo( disk, in );
    }
    virtual unsigned int numberOfDisks() const {
        return diskInfoCachePointer->numberOfDisks();
    }

 private:
    typedef ScanDir<Maxscans> ScanDirLayout;
    typedef DiskInfoCache<nDisks, DriveInfo, BankB> DiskInfoCacheLayout;

    Mark5ABLayout();

    ScanDirLayout* scanDirPointer;
    DiskInfoCacheLayout* diskInfoCachePointer;
};

struct EnhancedLayout : public UserDirInterface {
    EnhancedLayout( unsigned char* start );
    EnhancedLayout( unsigned char* start, unsigned int length );

    virtual unsigned int size() const;

    virtual unsigned int insanityFactor() const;
    
    virtual void sanitize();

    // scan functions
    virtual ROScanPointer getScan( unsigned int index ) const;
    virtual ScanPointer getNextScan( void );
    virtual void setScan( const ScanPointer& scan );
    virtual unsigned int nScans( void ) const;

    // erase functions
    virtual void clear_scans( void );
    virtual void remove_last_scan( void );
    virtual void recover( uint64_t recoveredRecordPointer );

    // disk info cache functions
    virtual std::string getVSN() const;
    virtual void setVSN( std::string& vsn );

    // 1 scan will be used signal the end of the directory (similar to c-string)
    static const unsigned int MaxScans = (XLR_MAX_UDIR_LENGTH - sizeof(EnhancedDirectoryHeader)) / sizeof(EnhancedDirectoryEntry) - 1;

 private:
    EnhancedDirectoryHeader& header;
    EnhancedDirectoryEntry* scans; // array
    unsigned int number_of_scans;

    EnhancedLayout();
    
};

// The encapsulating structure. Use this as your primary
// entrance to the StreamStor's userdirectory
struct UserDirectory {
    // The maximum number of bytes the user directory may
    // contain
    static const unsigned int  nBytes = XLR_MAX_UDIR_LENGTH+8;

    // default c'tor. It is empty (no scans etc)
    // but has "CurrentLayout" layout.
    UserDirectory();

    bool operator==( const UserDirectory& o ) const;
    bool operator!=( const UserDirectory& o ) const;

    // read/write to streamstor device.
    // if the device is recording/playbacking, 
    // throwance of exceptions will be your part.
    void        read( const xlrdevice& xlr );
    void        write( const xlrdevice& xlr );

    // the string representation of the current interface
    std::string currentInterfaceName() const;

    // the functions is the block below are only available if
    // disk info cache is available
    std::string  getVSN( void ) const;
    void         setVSN( std::string& vsn );
    void         getDriveInfo( unsigned int drive, S_DRIVEINFO& out ) const;
    void         setDriveInfo( unsigned int drive, S_DRIVEINFO& in );
    unsigned int numberOfDisks();

    ROScanPointer getScan( unsigned int index ) const;
    ScanPointer getNextScan( void );
    void setScan( const ScanPointer& scan );
    unsigned int nScans( void ) const;

    void clear_scans( void );
    void remove_last_scan( void );
    void recover( uint64_t recovered_record_pointer );

    // will force an EMPTY layout
    void forceLayout( std::string layoutName );

    ~UserDirectory();

private:
    static user_dir_identifier_type next_user_dir_id;
    user_dir_identifier_type id;
    
    // issues this->read(xlr).
    UserDirectory( const xlrdevice& xlr );

    // Keep a characterbuffer of XLR_MAX_UDIR_LENGTH+8
    // (In order to be able to to an xlrread/write, for
    // inline directory, the address has to be eight-byte-aligned).
    unsigned char*  rawBytes;
    // pointer to 8-byte-aligned address within rawBytes
    unsigned char*  dirStart;

    // clears 'rawBytes' to 0x0 and lets 'dirStart' point
    // at the first 8-byte-aligned address in 'rawBytes'
    void            init( void );

    UserDirInterface* interface;
    std::string       interfaceName;
    void setInterface( unsigned int dirSize );

    void try_write_dirlist( void ) const;

    // Copy and assignment. Don't allow them.
    UserDirectory( const UserDirectory& o );
    const UserDirectory& operator=( const UserDirectory& o );


};

#endif
