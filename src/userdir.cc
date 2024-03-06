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
#include <scan_label.h>
#include <carrayutil.h>

#include <boost/mpl/for_each.hpp>

#include <fstream>

DEFINE_EZEXCEPT(userdir_enosys)
DEFINE_EZEXCEPT(userdir_impossible_layout)

using std::endl;
    
// quite a mess, Mark5A/dimino/DRS has 18 different ways to interpret
// the bytes in the user directory, this file defines a boost::mpl::map,
// called Layout_Map, that maps a string (name) to a layout type
#include <userdir.mpl>


OriginalLayout::OriginalLayout( unsigned char* start ) {
    scanDir = (ScanDirLayout*)start;
    scanDir->clear();
}
OriginalLayout::OriginalLayout( unsigned char* start, unsigned int length ) {
    if ( length != sizeof(ScanDirLayout) ) {
        throw userdir_impossible_layout();
    }
    scanDir = (ScanDirLayout*)start;
}
unsigned int OriginalLayout::size() const {
    return sizeof(ScanDirLayout);
}
unsigned int OriginalLayout::dirListSize( void ) const {
    return sizeof(ScanDirLayout);    
}
unsigned int OriginalLayout::insanityFactor( void ) const {
    return scanDir->insanityFactor();
}
void OriginalLayout::sanitize( void ) {
    scanDir->sanitize();
}
ROScanPointer OriginalLayout::getScan( unsigned int index ) const {
    return scanDir->getScan( index );
}
ScanPointer OriginalLayout::getNextScan( void ) {
    return scanDir->getNextScan();
}
void OriginalLayout::setScan( const ScanPointer& scan ) {
    scanDir->setScan( scan );
}
unsigned int OriginalLayout::nScans( void ) const {
    return scanDir->nScans();
}
void OriginalLayout::clear_scans( void ) {
    scanDir->clear_scans();
}
void OriginalLayout::remove_last_scan( void ) {
    scanDir->remove_last_scan();
}
void OriginalLayout::recover( uint64_t recovered_record_pointer ) {
    scanDir->recover( recovered_record_pointer );
}
std::string OriginalLayout::getVSN( void ) const {
        return std::string();
}
std::string OriginalLayout::getCompanionVSN( void ) const {
        return std::string();
}

EnhancedLayout::EnhancedLayout( unsigned char* start ) :
    header(*(EnhancedDirectoryHeader*)start) 
{
    scans = (EnhancedDirectoryEntry*)(start + sizeof(EnhancedDirectoryHeader));
    number_of_scans = 0;
    header.clear();
}

EnhancedLayout::EnhancedLayout( unsigned char* start, unsigned int length ) :
    header(*(EnhancedDirectoryHeader*)start) 
{
    if ( (length < sizeof(EnhancedDirectoryHeader)) ||
         ((length - sizeof(EnhancedDirectoryHeader)) % sizeof(EnhancedDirectoryEntry) != 0) ) {
        throw userdir_impossible_layout();
    }
    scans = (EnhancedDirectoryEntry*)(start + sizeof(EnhancedDirectoryHeader));
    number_of_scans = (length - sizeof(EnhancedDirectoryHeader)) / sizeof(EnhancedDirectoryEntry);
    if ( number_of_scans > MaxScans ) {
        throw userdir_impossible_layout();
    }
}

unsigned int EnhancedLayout::size() const {
    return sizeof(EnhancedDirectoryHeader) + 
        number_of_scans * sizeof(EnhancedDirectoryEntry);
}

unsigned int EnhancedLayout::insanityFactor() const {
    unsigned int res = 0;

    for ( unsigned int i = 0; i < number_of_scans; i++ ) {
        INSANITYCHECK2(res, scans[i].scan_number != (i+1), "(scan #" << i << " scannumber invalid)");
        INSANITYCHECK2(res, (scans[i].data_type < 1) || (scans[i].data_type > 10), "(scan #" << i << " data type invalid)");
        INSANITYCHECK2(res, scans[i].start_byte > scans[i].stop_byte, "(scan #" << i << " bogus start/stop byte numbers)");
    }

    return res;
}

void EnhancedLayout::sanitize() {
     for ( unsigned int i = 0; i < number_of_scans; i++ ) {
         if ( (scans[i].data_type < 1) || (scans[i].data_type > 10) ) {
             scans[i].data_type = 1;
         }
         scans[i].stop_byte = std::max( scans[i].start_byte, scans[i].stop_byte);
     }
}

unsigned int EnhancedLayout::dirListSize() const {
    return sizeof(EnhancedDirectoryHeader) + 
        number_of_scans * sizeof(EnhancedDirectoryEntry);
}

ROScanPointer EnhancedLayout::getScan( unsigned int index ) const {
    EZASSERT2( index < number_of_scans, userdirexception, 
               EZINFO("requested scan (#" << index << ") out-of-range: "
                      << " nRecorded=" << number_of_scans) );
    EnhancedDirectoryEntry& scan(scans[index]);
    EZASSERT2( scan.start_byte <= scan.stop_byte, userdirexception, 
               EZINFO("stop byte (" << scan.stop_byte << 
                      ") < start byte (" << scan.start_byte << ")") );
    std::string name = from_c_str(&scan.experiment[0], sizeof(scan.experiment)) + "_" +
        from_c_str(&scan.station_code[0], sizeof(scan.station_code)) + "_" +
        from_c_str(&scan.scan_name[0], sizeof(scan.scan_name));
    return ROScanPointer(name,
                         scan.start_byte, scan.stop_byte - scan.start_byte, 
                         index);
}

ScanPointer EnhancedLayout::getNextScan() {
    EZASSERT2( number_of_scans < MaxScans, userdirexception,
               EZINFO("user directory is full, nRecorded=" << number_of_scans) );
    return ScanPointer( number_of_scans++ );
}

void EnhancedLayout::setScan( const ScanPointer& scan ) {
    EZASSERT2( scan.index() + 1 == number_of_scans, userdirexception,
               EZINFO("scan to be saved not the last scan") );
    EnhancedDirectoryEntry& target(scans[scan.index()]);
    scan_label::Split_Result split = scan_label::split(scan_label::extended, scan.name());

    EZASSERT2( split.experiment.size() <= sizeof(target.experiment), userdirexception,
               EZINFO("experiment name too long, maximum size=" << sizeof(target.experiment)) );
    EZASSERT2( split.station.size() <= sizeof(target.station_code), userdirexception,
               EZINFO("station name too long, maximum size=" << sizeof(target.station_code)) );
    EZASSERT2( split.scan.size() <= sizeof(target.scan_name), userdirexception,
               EZINFO("scan name too long, maximum size=" << sizeof(target.scan_name)) );

    // fill in the target scan
    target.clear();
    target.data_type = 1; // unknown
    target.scan_number = scan.index() + 1; // 1 based
    // strncpy() does not guarantee to null-terminate
    ::strncpy(target.experiment, split.experiment.c_str(), sizeof(target.experiment)-1);
    target.experiment[ sizeof(target.experiment)-1 ] = '\0';
    ::strncpy(target.station_code, split.station.c_str(), sizeof(target.station_code)-1);
    target.station_code[ sizeof(target.station_code)-1 ] = '\0';
    ::strncpy(target.scan_name, split.scan.c_str(), sizeof(target.scan_name)-1);
    target.scan_name[ sizeof(target.scan_name)-1 ] = '\0';
    target.start_byte = scan.start();
    target.stop_byte = target.start_byte + scan.length();
}

unsigned int EnhancedLayout::nScans() const {
    return number_of_scans;
}

void EnhancedLayout::clear_scans() {
    number_of_scans = 0;
}

void EnhancedLayout::remove_last_scan() {
    EZASSERT2( number_of_scans > 0, userdirexception, 
               EZINFO("no scan to remove") );
    number_of_scans--;
}

void EnhancedLayout::recover( uint64_t recoveredRecordPointer ) {
    if ( recoveredRecordPointer == 0 ) {
        number_of_scans = 0;
        return;
    }
    
    if ( number_of_scans > 0 ) {
        int last_scan = (int)number_of_scans - 1;
        while ( (last_scan >= 0) && 
                (scans[last_scan].start_byte >= recoveredRecordPointer) ) {
            last_scan--;
            number_of_scans--;
        }
        if ( last_scan >= 0 ) {
            scans[last_scan].stop_byte = recoveredRecordPointer;
        }
    }
    else {
        scans[0].clear();
        scans[0].data_type = 1;
        scans[0].stop_byte = recoveredRecordPointer;
        ::strncpy(&scans[0].scan_name[0], "recovered scan", sizeof(scans[0].scan_name));
        number_of_scans = 1;
    }
}

std::string EnhancedLayout::getVSN() const {
    return from_c_str( &header.vsn[0], sizeof(header.vsn) );
}

void EnhancedLayout::setVSN(std::string& vsn) {
    ::memset( &header.vsn[0], 0x0, sizeof(header.vsn) );
    ::memcpy( &header.vsn[0], vsn.data(), std::min(vsn.size(), sizeof(header.vsn)) );
}

std::string EnhancedLayout::getCompanionVSN() const {
    return from_c_str( &header.companion_vsn[0], sizeof(header.companion_vsn) );
}
void EnhancedLayout::setCompanionVSN(std::string& vsn) {
    ::memset( &header.vsn[0], 0x0, sizeof(header.vsn) );
    ::memcpy( &header.vsn[0], vsn.data(), std::min(vsn.size(), sizeof(header.vsn)) );
}

user_dir_identifier_type UserDirectory::next_user_dir_id = 0;

// the actual UserDirectory
UserDirectory::UserDirectory():
    rawBytes( 0 ), dirStart( 0 ), interface(NULL)
{
    this->init();
}

UserDirectory::UserDirectory( const xlrdevice& xlr ):
    rawBytes( 0 ), dirStart( 0 ), interface(NULL)
{
    //read() clears the internals ...
    this->read( xlr, false );
}

bool UserDirectory::operator==( const UserDirectory& o ) const {
    return (::memcmp(rawBytes, o.rawBytes, nBytes)==0);
}

bool UserDirectory::operator!=( const UserDirectory& o ) const {
    return !(this->operator==(o));
}

bool UserDirectory::valid( void ) const {
    return (interface != 0);
}

void UserDirectory::read( const xlrdevice& xlr, const bool expect_new ) {
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
    unsigned int dirSize = ::XLRGetUserDirLength(xlr.sshandle());
    do_xlr_unlock();
    
    delete interface;
    interface = NULL;
    
    if ( dirSize > 0 ) {
        XLRCALL( ::XLRGetUserDir(xlr.sshandle(), dirSize, 0, dirStart) );
        setInterface( dirSize );
    } else if( expect_new ) {
        DEBUG(-1, "UserDirectory::read() - found dirSize==0 whilst expecting something" << endl);
    }
    
    // Sanitize - if anything to sanitize
    if( interface != NULL ) {
        try {
            interface->sanitize();
        }
        catch ( userdir_enosys& e ) {
        }
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

    if ( interface == NULL ) {
        THROW_EZEXCEPT(userdirexception, "Attempt to write user directory with unknown layout");
    }
    
    unsigned int dirSize = interface->size();
    if ( dirSize > XLR_MAX_UDIR_LENGTH ) {
        THROW_EZEXCEPT(userdirexception, "Attempt to write a user directory larger then the StreamStor allows (requested: " << dirSize << ", maximum allowed: " << XLR_MAX_UDIR_LENGTH <<")");
    }
    
    XLRCALL( ::XLRSetUserDir(xlr.sshandle(), (void*)dirStart, dirSize) );
    try_write_dirlist();
    return;
}

#define CHECK_USER_DIRECTORY                                            \
    if ( interface == NULL ) {                                          \
        THROW_EZEXCEPT( userdirexception, "user directory not recognized" ); \
    }                                                                   \

std::string UserDirectory::currentInterfaceName() const {
    CHECK_USER_DIRECTORY;
    return interfaceName;
}

std::string UserDirectory::getVSN( void ) const {
    CHECK_USER_DIRECTORY;
    return interface->getVSN();
}

void UserDirectory::setVSN( std::string& vsn ) {
    CHECK_USER_DIRECTORY;
    interface->setVSN( vsn );
}
std::string UserDirectory::getCompanionVSN( void ) const {
    CHECK_USER_DIRECTORY;
    return interface->getCompanionVSN();
}

void UserDirectory::setCompanionVSN( std::string& vsn ) {
    CHECK_USER_DIRECTORY;
    interface->setCompanionVSN( vsn );
}

void UserDirectory::getDriveInfo( unsigned int drive, S_DRIVEINFO& out ) const {
    CHECK_USER_DIRECTORY;
    return interface->getDriveInfo( drive, out );
}

void UserDirectory::setDriveInfo( unsigned int drive, S_DRIVEINFO const& in ) {
    CHECK_USER_DIRECTORY;
    interface->setDriveInfo( drive, in );
}

unsigned int UserDirectory::numberOfDisks() {
    CHECK_USER_DIRECTORY;
    return interface->numberOfDisks();
}

ROScanPointer UserDirectory::getScan( unsigned int index ) const {
    CHECK_USER_DIRECTORY;
    return interface->getScan( index );
}

ScanPointer UserDirectory::getNextScan( ) {
    CHECK_USER_DIRECTORY;
    ScanPointer s = interface->getNextScan();
    s.user_dir_id = id;
    return s;
}

void UserDirectory::setScan( const ScanPointer& scan ) {
    CHECK_USER_DIRECTORY;
    if ( scan.user_dir_id != id ) {
        THROW_EZEXCEPT( userdirexception, "trying to write a scan started for a different disk" );
    }
    return interface->setScan( scan );
}

unsigned int UserDirectory::nScans( void ) const {
    CHECK_USER_DIRECTORY;
    return interface->nScans();
}

void UserDirectory::clear_scans( void ) {
    CHECK_USER_DIRECTORY;
    return interface->clear_scans();
}

void UserDirectory::remove_last_scan( void ) {
    CHECK_USER_DIRECTORY;
    return interface->remove_last_scan();
}

void UserDirectory::recover( uint64_t recovered_record_pointer ) {
    CHECK_USER_DIRECTORY;
    return interface->recover( recovered_record_pointer );
}

#undef CHECK_USER_DIRECTORY

UserDirectory::~UserDirectory() {
    delete interface;
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

struct Best_Layout_Operator {
    struct Output {
        Output() : interface(NULL), name("") {}
        UserDirInterface* interface;
        std::string       name;
        unsigned int      insanity;
    };

    Best_Layout_Operator( unsigned char* start, unsigned int size, Output& o ) : 
        dirStart(start), dirSize(size), output(&o) {}
        
    template <typename T> 
    void operator()(T) {
        try {
            UserDirInterface* tmp = new typename T::second( dirStart, dirSize );
            unsigned int s;
            try {
                DEBUG(4, "Attempting Layout " << boost::mpl::c_str<typename T::first>::value << endl);
                s = tmp->insanityFactor();
                DEBUG(4, "Counted " << s << " inconsistencies" << endl);
            }
            catch ( ... ) {
                delete tmp;
                throw;
            }
            if ( (output->interface == NULL) || (output->insanity > s) ) {
                delete output->interface;
                output->interface = tmp;
                output->name = boost::mpl::c_str<typename T::first>::value;
                output->insanity = s;
            }
            else {
                delete tmp;
            }
        }
        catch (userdir_impossible_layout& e) {
        }
    }

 private:
    Best_Layout_Operator();
    unsigned char* dirStart;
    unsigned int   dirSize;
    Output*        output;
};

void UserDirectory::setInterface( unsigned int dirSize ) {
    
    Best_Layout_Operator::Output output;
    Best_Layout_Operator best_layout_operator( dirStart, dirSize, output );
    boost::mpl::for_each< Layout_Map >( best_layout_operator );

    delete interface;
    interface = output.interface;

    if( interface == NULL ) {
        DEBUG(0, "Could not find a proper layout for the user directory, size "
              << dirSize << endl);
    }
    else {
        interfaceName = output.name;
        DEBUG(3, "Layout set to " << interfaceName << ", detected " << output.insanity << " inconsistencies" << endl);
    }
}

struct Force_Layout_Operator {
    struct Output {
        Output() : interface(NULL) {}
        UserDirInterface* interface;
    };

    Force_Layout_Operator( unsigned char* start, std::string name, Output* o, std::vector<S_DRIVEINFO> const& di ) : 
        dirStart(start), layoutName(name), output(o), driveInfo(di) {}
        
    template <typename T> 
    void operator()(T) {
        if ( output->interface != NULL ) {
            // already found the interface, no need to continue searching
            return;
        }
        if ( layoutName == boost::mpl::c_str<typename T::first>::value ) {
            // We're allowed to throw up and break everything from here.
            // The reason is that we've found the matching layout (the one
            // that the user wishes to force) and if there's something wrong
            // then we cannot honour the request to force.
            // Trowage is done such that the cause of rejection can be
            // reported back to the caller, basically informing him/her
            // he/she's an idiot :D
            UserDirInterface*  tmpInterface = 0;

            try {
                tmpInterface = new typename T::second( dirStart );
                // Assert that the interface supports writing disk info of
                // at least the number of disks that the current system has
                EZASSERT2(tmpInterface->numberOfDisks()>=driveInfo.size(), userdirexception,
                          EZINFO("requested layout cannot store drive metadata of " << driveInfo.size() << " disks"));
                // We should try to set/get driveInfo to see if the target
                // can support storing the current disk pack's meta data 
                // specifically, SDK8 user dir has a size limit of 1TB per
                // disk that it can handle.
                // Find the driveInfo with the largest capacity and attempt
                // to set/get that.
                std::vector<S_DRIVEINFO>::const_iterator p = driveInfo.begin();

                for(std::vector<S_DRIVEINFO>::const_iterator cur = p; cur!=driveInfo.end(); cur++)
                    if( cur->Capacity>p->Capacity )
                        p = cur;
                // p points at driveInfo with max capacity or at
                // dirInfo.end() in case there are no drives ...
                if( p!=driveInfo.end() ) {
                    Zero<S_DRIVEINFO>  tmpDI;

                    // Now attempt to read/write the driveInfo
                    tmpInterface->setDriveInfo(0, *p);    // write entry having max capacity
                    tmpInterface->setDriveInfo(0, tmpDI); // write back zeroes:
                                                          // the tmpInterface will be the real one so we must deliver it pristine
                    tmpInterface->getDriveInfo(0, tmpDI); // attempt to read into current S_DEVINFO
                }
            }
            catch (userdir_enosys& ) {
                // thrown if this userdir layout does not support storing
                // drive info at all, i.e. it will never fail on trying to write
                // any drive info, thus this can be taken as a success value
            }
            catch( std::exception& e ) {
                delete tmpInterface;
                DEBUG(0, "ForceLayout: layout " << layoutName << " cannot be forced because of " << e.what() << endl);
                // And pass on the exception to the higher layer such that
                // it can be reported back to the user why the requested
                // layout cannot be selected
                throw;
            }
            catch( ... ) {
                std::ostringstream msg;

                delete tmpInterface;
                msg << "ForceLayout: layout " << layoutName << " rejected because of unknown exception";
                DEBUG(0,  msg.str() << endl);
                THROW_EZEXCEPT(userdirexception, msg.str());
            }
            output->interface = tmpInterface;
        }
    }

 private:
    Force_Layout_Operator();
    unsigned char*           dirStart;
    std::string              layoutName;
    Output*                  output;
    std::vector<S_DRIVEINFO> driveInfo;
};

void UserDirectory::forceLayout( std::string layoutName, std::vector<S_DRIVEINFO> const& di ) {
    Force_Layout_Operator::Output output;
    Force_Layout_Operator         force_layout_operator( dirStart, layoutName, &output, di );
    boost::mpl::for_each<Layout_Map>( force_layout_operator );

    EZASSERT2( output.interface != NULL, userdirexception,
               EZINFO("Failed to find layout called '" << layoutName << "'") );
    delete interface;
    interface = output.interface;
    interfaceName = layoutName;
}

void UserDirectory::try_write_dirlist( void ) const {
    EZASSERT2( interface, userdirexception, EZINFO("Cannot write userdir with no layout to /var/dir/*") );

    unsigned int dirListBytes = interface->dirListSize();
    EZASSERT2( dirListBytes, userdirexception, EZINFO("Cannot write userdir with no zero dirListSize() to /var/dir/*") );

    // We expect this to succeed
    try {
        std::ofstream file;
        file.exceptions( std::ofstream::failbit | std::ofstream::badbit );
        file.open( "/var/dir/Mark5A", std::ios_base::out | std::ios_base::trunc | std::ios_base::binary );
        file.write( (const char*)dirStart, dirListBytes );
        file.close();
    }
    catch (std::exception& e) {
        DEBUG(4, "Failed to write DirList to /var/dir/Mark5A, exception: " << e.what() << std::endl);
    }
    catch ( ... ) {
        DEBUG(4, "Failed to write DirList to /var/dir/Mark5A, unknown exception" << std::endl);
    }

    // try to write it to /var/dir/<VSN> too
    std::string label;
    try {
        label = this->getVSN();
    }
    catch ( ... ) {
        // failed to get VSN from the layout, forget trying to write the
        // DirList to the VSN specific file
    }
    if ( !label.empty() ) {
        // only take the actual VSN portion, cut of the rate/capacity and/or
        // the disk state
        label = label.substr(0, label.find_first_of("/\036"));
        try {
            std::ofstream file;
            file.exceptions( std::ofstream::failbit | std::ofstream::badbit );
            file.open( ("/var/dir/" + label).c_str(), std::ios_base::out | std::ios_base::trunc | std::ios_base::binary );
            file.write( (const char*)dirStart, dirListBytes );
            file.close();
        }
        catch (std::exception& e) {
            DEBUG( 4, "Failed to write DirList to /var/dir/" << label << ", exception: " << e.what() << std::endl);
        }
        catch ( ... ) {
            DEBUG( 4, "Failed to write DirList to /var/dir/" << label << ", unknown exception" << std::endl);
        }
    }
}
