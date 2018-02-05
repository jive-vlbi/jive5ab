// implementation
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
#include <xlrdevice.h>
#include <evlbidebug.h>
#include <streamutil.h>
#include <threadutil.h>
#include <dosyscall.h>
#include <carrayutil.h>
#include <mutex_locker.h>
#include <hex.h>

#include <strings.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <map>
#include <stdlib.h>

using namespace std;

xlrreg::teng_registermap xlrdbRegisters( void );
xlrreg::teng_registermap xlrdevice::xlrdbregs = xlrdbRegisters();

DEFINE_EZEXCEPT(xlrreg_exception)




#ifdef NOSSAPI
#define NOTSUPPORTED "Compiled without support for StreamStor"
XLR_RETURN_CODE XLRClose(SSHANDLE) { return XLR_FAIL; }
UINT            XLRDeviceFind( void )   { return 0; }
XLR_RETURN_CODE XLRGetDBInfo(SSHANDLE,PS_DBINFO) { return XLR_FAIL; }
XLR_RETURN_CODE XLRGetErrorMessage(char* e,XLR_ERROR_CODE) { 
#if defined(__APPLE__) || defined(__OpenBSD__)
    ::strlcpy(e, NOTSUPPORTED, sizeof(NOTSUPPORTED)); return XLR_SUCCESS; }
#else
    ::strcpy(e, NOTSUPPORTED); return XLR_SUCCESS; }
#endif
DWORDLONG       XLRGetFIFOLength(SSHANDLE)  { return 0; }
// StreamStor error codes start at 2 so we must be sure to return something
// that counts as an error
XLR_ERROR_CODE  XLRGetLastError( void ) { return 2; }
DWORDLONG       XLRGetLength(SSHANDLE)  { return 0; }
DWORDLONG       XLRGetPlayLength(SSHANDLE) { return 0; }
UINT            XLRGetUserDirLength(SSHANDLE) { return 0; }
XLR_RETURN_CODE XLRReadFifo(SSHANDLE,READTYPE*,ULONG,BOOLEAN) { return XLR_FAIL; }
XLR_RETURN_CODE XLRSkip(SSHANDLE,UINT,BOOLEAN) { return XLR_FAIL; }
XLR_RETURN_CODE XLRMountBank(SSHANDLE, UINT32)    { return XLR_FAIL; }
XLR_RETURN_CODE XLRDismountBank(SSHANDLE, UINT32) { return XLR_FAIL; }

// Do call an XLR-API method and check returncode.
// If it's not XLR_SUCCESS an xlrexception is thrown
// Precondition: xlr_access_lock is held by the calling thread
#define LOCKED_XLRCALL(a)  do { XLR_LOCATION;                           \
        XLR_STUFF(#a);                                                  \
        xlr_Svar_0a << " - compiled w/o SSAPI support";                 \
        throw xlrexception(xlr_Svar_0a.str());                          \
    } while( 0 );
#else
#define LOCKED_XLRCALL(a)                                \
    do {                                                 \
        XLR_RETURN_CODE xrv0lcl1;                        \
        xrv0lcl1 = a;                                    \
        if( xrv0lcl1!=XLR_SUCCESS ) {                    \
            XLR_LOCATION;                                \
            XLR_STUFF(#a);                               \
            throw xlrexception( xlr_Svar_0a.str() );     \
        }                                                \
    } while( 0 );
#endif

// the mutex to serialize access
static pthread_mutex_t xlr_access_lock = PTHREAD_MUTEX_INITIALIZER;

void do_xlr_lock( void ) {
    int rv;
    if( (rv=::pthread_mutex_lock(&xlr_access_lock))!=0 ) {
        // we cannot do much but report that the lock failed -
        // other than letting the app crash which might seem
        // a bit over-the-top. at least give observant user 
        // (yeah, right, as if there are any) chance to
        // try to shut down nicely
        std::cerr << "do_xlr_lock() failed - " << evlbi5a::strerror(rv) << std::endl;
    }
    return;
}
void do_xlr_unlock( void ) {
    int rv;
    if( (rv=::pthread_mutex_unlock(&xlr_access_lock))!=0 ) {
        // we cannot do much but report that the lock failed -
        // other than letting the app crash which might seem
        // a bit over-the-top. at least give observant user 
        // (yeah, right, as if there are any) chance to
        // try to shut down nicely
        std::cerr << "do_xlr_unlock() failed - " << evlbi5a::strerror(rv) << std::endl;
    }
    return;
}


// The error capturer
lastxlrerror_type::lastxlrerror_type() :
    xlr_errno( ::XLRGetLastError() )
{
    char    s[XLR_ERROR_LENGTH];
    ::XLRGetErrorMessage(s, xlr_errno);
    xlr_errormessage = string(s);
}

// format it as "<message> (<errno>)" when this thing
// is inserted into a stream
ostream& operator<<( ostream& os, const lastxlrerror_type& xlre ) {
    return os << " - " << xlre.xlr_errormessage << " (" << xlre.xlr_errno << ")";
}

// disk states as appended to the vsn
const std::string      disk_state_strings[] = {"Recorded", "Played", "Erased", "Unknown", "Error"};
const set<std::string> disk_states::all_set(&disk_state_strings[0], &disk_state_strings[array_size(disk_state_strings)]);

pair<string, string> disk_states::split_vsn_state(string label) {
    size_t record_separator_pos = label.find('\036');
    if (record_separator_pos == string::npos) {
        return pair<string, string>(label, "Unknown");
    }
    else {
        return pair<string, string>(label.substr(0, record_separator_pos), label.substr(record_separator_pos + 1));
    }
}

// units are in 15ns
const ULONG xlrdevice::drive_stats_default_values[] = {1125000/15, 2250000/15, 4500000/15, 9000000/15,
                                                       18000000/15, 36000000/15, 72000000/15};
const size_t xlrdevice::drive_stats_length          = array_size(drive_stats_default_values);

// the exception
xlrexception::xlrexception( const string& s ):
    msg( s )
{}

const char* xlrexception::what() const throw() {
    return msg.c_str();
}
xlrexception::~xlrexception() throw()
{}


// software version stuff
swversion_type::swversion_type(unsigned int ma, unsigned int mi):
    major_v( ma ), minor_v( mi )
{}

swversion_type::swversion_type(char const*const mas, char const*const mis):
    major_v( (unsigned int)::strtol(mas, 0, 0) ), minor_v( (unsigned int)::strtol(mis, 0, 0) )
{}

bool operator<(const swversion_type& l, const swversion_type& r) {
    if( l.major_v==r.major_v )
        return l.minor_v < r.minor_v;
    return l.major_v<r.major_v;
}
bool operator==(const swversion_type& l, const swversion_type& r) {
    return (l<r)==false && (r<l)==false;
}
bool operator>(const swversion_type& l, const swversion_type& r) {
    return r<l;
}
bool operator<=(const swversion_type& l, const swversion_type& r) {
    return (l<r) || (l==r);
}
bool operator>=(const swversion_type& l, const swversion_type& r) {
    return (r<l) || (r==l);
}
ostream& operator<<(ostream& os, const swversion_type& sw) {
    return os << sw.major_v << "." << sw.minor_v;
}

// The xlr register stuff
// Note: 'uint32_t' used to be UINT32 but SDKs < SDK9 don't define that type.
//       We use the standard 32-bit unsigned data type and hope they're compatible.
//       The compilert should complain if they aren't
xlrreg_pointer::xlrreg_pointer():
    devHandle( ::noDevice ), wordnr( (uint32_t)-1 ), startbit( (uint32_t)-1 ),
    valuemask( 0 ), fieldmask( 0 )
{}

xlrreg_pointer::xlrreg_pointer(const xlrreg::regtype reg, SSHANDLE dev):
    devHandle( dev ), wordnr( reg.word ), startbit( reg.startbit ),
    valuemask( bitmasks<uint32_t>()[reg.nbit] ), fieldmask( valuemask<<startbit )
{
    EZASSERT(reg.nbit != 0, xlrreg_exception);
    EZASSERT(devHandle != ::noDevice, xlrreg_exception );
}


#if HAVE_10GIGE 
const xlrreg_pointer& xlrreg_pointer::operator=( const bool& b ) {
    UINT32    value( (b)?((UINT32)0x1):((UINT32)0x0) );
    // forward to normal operator=()
    return this->operator=(value);
}
#else
const xlrreg_pointer& xlrreg_pointer::operator=( const bool& ) {
    THROW_EZEXCEPT(xlrreg_exception, "Compiled under SDK without 10GigE daughterboard support")
}
#endif

uint32_t xlrreg_pointer::operator*( void ) const {
#if HAVE_10GIGE
    UINT32     w;

    EZASSERT(devHandle!=::noDevice, xlrreg_exception);
    XLRCALL( ::XLRReadDBReg32(devHandle, wordnr, &w) );
    return ((w&((UINT32)fieldmask))>>startbit);
#else
    THROW_EZEXCEPT(xlrreg_exception, "Compiled under SDK without 10GigE daughterboard support")
#endif
}

ostream& operator<<(ostream& os, const xlrreg_pointer& rp ) {
    os << "XLR DB value @bit" << rp.startbit << " [vmask=" << hex_t(rp.valuemask)
        << " fmask=" << hex_t(rp.fieldmask) << "]";
    return os;
}



// The interface object

xlrdevice::xlrdevice():
   mydevice( new xlrdevice_type() )
{}

xlrdevice::xlrdevice( UINT d ):
    mydevice( new xlrdevice_type(d) )
{
    update_mount_status();
}

xlrdevice::operator bool() const {
    return mydevice->devnum!=xlrdevice::noDevice;
}

UINT xlrdevice::devnum( void ) const {
    return mydevice->devnum;
}

SSHANDLE xlrdevice::sshandle( void ) const {
    return mydevice->sshandle;
}

const S_DBINFO& xlrdevice::dbInfo( void ) const {
    return mydevice->dbinfo;
}

void xlrdevice::copyDevInfo( S_DEVINFO& into ) const {
    mutex_locker locker( mydevice->user_dir_lock );
    into = mydevice->devinfo;
}

const S_XLRSWREV& xlrdevice::swRev( void ) const {
    return mydevice->swrev;
}

void xlrdevice::setBankMode( S_BANKMODE newmode ) {
    mydevice->setBankMode( newmode );
    update_mount_status();
}

S_BANKMODE xlrdevice::bankMode( void ) const {
    return mydevice->bankMode;
}


bool xlrdevice::isAmazon( void ) const {
    return (mydevice->devnum!=xlrdevice::noDevice &&
            ::strncasecmp(mydevice->devinfo.BoardType, "AMAZON", 6)==0);
}

unsigned int xlrdevice::boardGeneration( void ) const {
    if( mydevice->devnum==xlrdevice::noDevice )
        return 0;

    if( ::strcmp(mydevice->devinfo.BoardType, "AMAZON-EXP")==0 )
        return 5;
    else if( ::strcmp(mydevice->devinfo.BoardType, "AMAZON")==0 ||
             ::strcmp(mydevice->devinfo.BoardType, "AMAZON-P")==0 ||
             ::strcmp(mydevice->devinfo.BoardType, "AMAZON-VP")==0 )
        return 4;
    else
        return 3;
    // Make sure compilert is happy
    return 0;
}

unsigned int xlrdevice::maxForkDataRate( void ) const {
    if( mydevice->devnum==xlrdevice::noDevice )
        return 0;

    if ( !isAmazon() ) {
        // V100, VF2
        return  512 * 1000 * 1000;
    }
    else {
        // Amazon
        return 1024 * 1000 * 1000;
    }
}

xlrreg_pointer xlrdevice::operator[](xlrreg::teng_register reg) {
    xlrreg::teng_registermap::const_iterator curreg;

    // Assert that the register actually is defined
    EZASSERT2((curreg=xlrdevice::xlrdbregs.find(reg))!=xlrdevice::xlrdbregs.end(), xlrreg_exception,
              EZINFO("No defintion found for daughter board register"));

    // Excellent!
    return xlrreg_pointer(curreg->second, mydevice->sshandle);
}

ROScanPointer xlrdevice::getScan( unsigned int index ) {
    mutex_locker locker( mydevice->user_dir_lock );
    return mydevice->user_dir.getScan( index );
}

ScanPointer xlrdevice::startScan( std::string name ) {
    mutex_locker locker( mydevice->user_dir_lock );
    if ( mydevice->recording_scan ) {
        throw xlrexception("already recording a scan, cannot start another one");
    }
    ScanPointer scan = mydevice->user_dir.getNextScan();

    try {
        // check for duplicate scan names, add a suffix if needed
        map<char, unsigned int> duplicate_count;
        bool duplicate_detected = false;
        string::size_type requested_size = name.size();
        for (unsigned int index = 0; index < mydevice->user_dir.nScans() - 1; index++) {
            string scan_name = mydevice->user_dir.getScan(index).name();
            if ( scan_name == name ) {
                duplicate_detected = true;
            }
            else if ( (scan_name.size() == requested_size + 1) &&
                 (scan_name.substr(0, requested_size) == name) ) {
                char extension = scan_name[requested_size];
                // it's only an extension if it is in [a-z] or [A-Z]
                if ( ('a' <= extension && extension <= 'z') ||
                     ('A' <= extension && extension <= 'Z') ) {
                    duplicate_count[extension]++;
                }
            }
        }
        if ( duplicate_detected ) {
            unsigned int minimum_count = UINT_MAX;
            char extension = '\0';
            for ( char extension_candidate = 'a'; 
                  extension_candidate != ('Z' + 1);
                  extension_candidate = (extension_candidate == 'z' ? 'A' : extension_candidate+1) ) {
                if ( duplicate_count[extension_candidate] < minimum_count ) {
                    minimum_count = duplicate_count[extension_candidate];
                    extension = extension_candidate;
                }
            }
            DEBUG(2, "Found duplicate scan name, extending requested scan name with '" << extension << "'" << endl);
            name += extension;
        }

        // add a * to the name to indicate a scan in process of recording
        scan.setName( name + "*" );
        scan.setStart( ::XLRGetLength(sshandle()) );
        scan.setLength( 0 );
        
        mydevice->user_dir.setScan( scan );
    }
    catch ( ... ) {
        mydevice->user_dir.remove_last_scan();
        throw;
    }

    // HV: Build in protection agains record pointer being reset.
    // After adding but before actually crying victory, do a small
    // consistency check: nowhere in the scan directory, the start byte
    // of a scan may be lower than the previous scan.
    bool     recordPointerReset = false;
    uint64_t lastRecPtr = 0;

    for(unsigned int i=0; i<mydevice->user_dir.nScans() && recordPointerReset==false; i++) {
        const uint64_t    curRecPtr = mydevice->user_dir.getScan(i).start();
        
        recordPointerReset = (curRecPtr < lastRecPtr);
        lastRecPtr         = curRecPtr;
    }

    if( recordPointerReset ) {
        // Ok, erase the last scan - we're going to throw up
        mydevice->user_dir.remove_last_scan();

        EZASSERT2(recordPointerReset==false, userdirexception,
                  EZINFO("Record pointer was reset; recording not allowed to prevent overwriting data. "
                         "This disk pack is corrupted. Fix it or insert a new disk pack."));
    } 
    
    mydevice->user_dir.write( *this );
    mydevice->recording_scan = true;
    return scan;
}

void xlrdevice::finishScan( ScanPointer& scan ) {
    mutex_locker locker( mydevice->user_dir_lock );
    mydevice->recording_scan = false;
    S_DIR diskDir;
    
    ::memset(&diskDir, 0, sizeof(S_DIR));
    XLRCALL( ::XLRGetDirectory(sshandle(), &diskDir) );

    // It turns out that Streamstor AMAZON *can* [and sometimes WILL]
    // produce recordings that are modulo 4 in size, in stead of modulo 8.
    // So, to be on the safe side, we truncate ALL recordings to a size
    // of modulo 8
    if( (diskDir.Length%8) || (diskDir.AppendLength%8) ) {
        // Note: the playpointer class automatically truncates to modulo 8!
        const playpointer  newend( diskDir.Length );
        const uint64_t     diff = diskDir.Length - newend.Addr;

        XLRCALL( ::XLRTruncate(sshandle(), newend.AddrHi, newend.AddrLo) );
        DEBUG(-1, "*** WARNING: Non-multiple-of-eight recording detected." << endl <<
                  "             Length:" << diskDir.Length << " AppendLength:" << diskDir.AppendLength << endl <<
                  "             Truncating to:" << newend << " => Lost:" << diff << " bytes" << endl);
        // Do NOT call XLRGetDirectory again; it will clobber the
        // ".AppendLength" to be identical to ".Length"!
        // In stead, we just modify the struct fields
        diskDir.Length        = newend.Addr;
        diskDir.AppendLength -= diff;
    }

    // Note: appendlength is the amount of bytes 
    // appended to the existing recording using
    // XLRAppend().
    scan.setLength( diskDir.AppendLength );

    // strip the last '*' if present
    scan.setName( ROScanPointer::strip_asterisk(scan.name()) );
    
    mydevice->user_dir.setScan( scan );

    // and update on disk
    mydevice->user_dir.write( *this );

}

std::string xlrdevice::userDirLayoutName() const {
    mutex_locker locker( mydevice->user_dir_lock );
    return mydevice->user_dir.currentInterfaceName();
}

void xlrdevice::stopRecordingFailure() {
    // any interaction with the streamstor is bound to fail, 
    // just get us in a workable, but unpredictable, state
    mutex_locker locker( mydevice->user_dir_lock );
    mydevice->recording_scan = false;
}

unsigned int xlrdevice::nScans( void ) {
    mutex_locker locker( mydevice->user_dir_lock );
    return mydevice->user_dir.nScans();
}

bool xlrdevice::isScanRecording( void ) {
    mutex_locker locker( mydevice->user_dir_lock );
    return mydevice->recording_scan;
}

void xlrdevice::write_label( std::string XLRCODE(vsn) ) {
    XLRCALL( ::XLRSetLabel(sshandle(), const_cast<char*>(vsn.c_str()), vsn.size()) );   
}

void xlrdevice::write_vsn( std::string vsn ) {
    ASSERT_COND( vsn.size() < VSNLength ); 

    {
        mutex_locker locker( mydevice->user_dir_lock );

        // if the user directory layout has the VSN, update it
        try {
            mydevice->user_dir.setVSN( vsn );
        }
        catch (userdir_enosys&) {}
    
        // same for the drive info cache
        try {
            unsigned int number_of_disks = mydevice->user_dir.numberOfDisks();
            S_DEVINFO     dev_info;

            XLRCALL( ::XLRGetDeviceInfo( sshandle(), &dev_info ) );

            vector<unsigned int> master_slave;
            master_slave.push_back(XLR_MASTER_DRIVE);
            master_slave.push_back(XLR_SLAVE_DRIVE);
        
            S_DRIVEINFO drive_info;
            for (unsigned int bus = 0; bus < dev_info.NumBuses; bus++) {
                for (vector<unsigned int>::const_iterator ms = master_slave.begin();
                     ms != master_slave.end();
                     ms++) {
                    unsigned int disk_index = bus * 2 + (*ms==XLR_MASTER_DRIVE? 0 : 1);
                    if ( disk_index < number_of_disks ) {
                        try {
                            XLRCALL( ::XLRGetDriveInfo( sshandle(), bus, *ms, &drive_info ) );
                        }
                        catch ( ... ) {
                            memset( &drive_info, 0, sizeof(drive_info) );
                        }
                        mydevice->user_dir.setDriveInfo( disk_index, drive_info );
                    }
                }
            }
        }
        catch (userdir_enosys&) {}
    
        mydevice->user_dir.write( *this );

        write_label( vsn );
    }

    // VSN has changed, make sure the new status is stored
    update_mount_status(); 
}

void xlrdevice::write_state( std::string new_state ) {
    // new state has to be appended to VSN, so retreive that
    char label[XLR_LABEL_LENGTH + 1];
    label[XLR_LABEL_LENGTH] = '\0';

    XLRCALL( ::XLRGetLabel( sshandle(), label) );
    pair<string, string> vsn_state = disk_states::split_vsn_state(string(label));
    // and write the vsn with new state appended
    write_label(vsn_state.first + '\036' + new_state);
}

std::string xlrdevice::read_label( void ) {
    return mydevice->read_label();
}

std::string xlrdevice::get_vsn( void ) {
    mutex_locker locker( mydevice->user_dir_lock );
    return mydevice->user_dir.valid() ? mydevice->user_dir.getVSN() : std::string();
}
std::string xlrdevice::get_companion( void ) {
    mutex_locker locker( mydevice->user_dir_lock );
    return mydevice->user_dir.valid() ? mydevice->user_dir.getCompanionVSN() : std::string();
}

void xlrdevice::recover( UINT XLRCODE( mode ) ) {
    XLRCALL( ::XLRRecoverData(sshandle(), mode) );
    DWORDLONG length = ::XLRGetLength( sshandle() );
    if ( length > 0 ) { // something was left after recovering, let's trust it
        mutex_locker locker( mydevice->user_dir_lock );
        mydevice->user_dir.recover( (uint64_t)length );
        mydevice->user_dir.write( *this );
    }
}

vector<S_DRIVEINFO> xlrdevice::getStoredDriveInfo( void )  {
    mutex_locker locker( mydevice->user_dir_lock );

    unsigned int numberOfDisks = mydevice->user_dir.numberOfDisks();
    vector<S_DRIVEINFO> cache( numberOfDisks );
    for ( unsigned int diskIndex = 0; diskIndex < numberOfDisks; diskIndex++ ) {
        mydevice->user_dir.getDriveInfo( diskIndex, cache[diskIndex] );
    }
    return cache;
}

void xlrdevice::start_condition() {
    {
        mutex_locker locker( mydevice->user_dir_lock );
        mydevice->user_dir.clear_scans();
        mydevice->user_dir.write( *this );
    }
    XLRCALL( ::XLRErase(sshandle(), SS_OVERWRITE_RW_PATTERN) );
}

struct mode_switcher {
    mode_switcher( SSHANDLE ss, S_BANKMODE XLRCODE(newBM), S_BANKMODE oldBM ):
        ssHandle( ss ), oldBankMode( oldBM ) {
            XLRCALL( ::XLRSetBankMode(ssHandle, newBM) );
    }

    ~mode_switcher() throw(xlrexception) {
            XLRCALL( ::XLRSetBankMode(ssHandle, oldBankMode) );
    }

    private:
        SSHANDLE   ssHandle;
        S_BANKMODE oldBankMode;

        // In C++11 we could just really delete these member functions. Now
        // we just put them in the private section and leave them
        // declared but unimplemented.
        mode_switcher();
        mode_switcher(const mode_switcher&);
        const mode_switcher& operator=(const mode_switcher&);
};

void xlrdevice::erase( std::string layoutName, const SS_OWMODE XLRCODE(owm) ) {
    // save the VSN so it can be restored
    string  label( this->read_label() );

    // HV/BE: 8/Nov/2016 Attempt to deal correctly with non-bank mode
    //                   0. initialize two VSNs with whatever is in the
    //                      current user directory
    //                   1. if in non-bank mode, switch temp. to bank mode,
    //                      read both labels. Overwrite captured values
    //                      if those are not identical. Set 'must write
    //                      vsn'.
    //                   2. test if target userdir can be constructed w/o
    //                      errors
    //                   3.   << erase >>
    //                   4. in post phase write both VSNs into userdir and 
    //                      flush to disk
    const S_BANKMODE curbm = this->bankMode();
    bool             mustWriteVSN(curbm==SS_BANKMODE_DISABLED );
    std::string      ownVSN( this->get_vsn() ), companionVSN( this->get_companion() );

    // HV: 9/Nov/2016: Edits to do our best to support non-bankmode
    //                 basically we try to retain the constituent VSNs as
    //                 best as we can.
    // If ownVSN (read back from UserDirectory) is empty, fall back to using
    // current label [there are user directory formats that do not register
    // the own VSN - 'OriginalLayout' to name but one]
    //
    // If someone managed to create a non-bankmode disk pack with
    // userdirectory 'OriginalLayout' then the original VSNs are lost can
    // not be regenerated (other than using eyeball technology and reading
    // the barcodelabel sticker on the module itself), breaking up the
    // non-bankmode pack [run jive5ab in bankmode] and put back the original
    // VSNs in both disk packs' label fields by using 'protect=off; vsn=<orignal vsn>'
    if( ownVSN.empty() )
        ownVSN = label;

    if( curbm==SS_BANKMODE_DISABLED ) {
        // oh bugger.
        char            tmp[XLR_LABEL_LENGTH + 1];
        string          vsnA, vsnB, labelA, labelB; 
        const string    curLayout( mydevice->user_dir.valid() ? mydevice->user_dir.currentInterfaceName() : "" );
        XLR_RETURN_CODE xlrResult;
        
        // Note: the comparison will be done on VSN basis but we record the
        // full label(s) in the UserDirectory

        // switch to non-bank mode (unswitches when going out of context)
        //  note that mode_switcher assumes that the streamstor lock is free
        mode_switcher tmpBankMode(sshandle(), SS_BANKMODE_NORMAL/*new*/, SS_BANKMODE_DISABLED/*old*/);
        // here we grab the mutex manually. C++ guaranteers that d'tors are
        // called in reverse order when going out of scope so ~ssLock()
        // released mutex before ~tmpBankMode() grabs it back again
        mutex_locker  ssLock( xlr_access_lock );

        // (attempt to) switch to bank A
        // Note that failure here is not a fail - that is: do we allow erase
        // in non-bank mode whilst not both banks populated/ready?
        xlrResult = XLR_FAIL;
        XLRCODE( xlrResult = ::XLRSelectBank(sshandle(), BANK_A) );
        if( xlrResult==XLR_SUCCESS ) {
            tmp[XLR_LABEL_LENGTH] = '\0';
            XLRCODE( ::XLRGetLabel(sshandle(), tmp) );
            labelA = string(tmp);
            // Label is <vsn>/<size>/<rate>\036<disk state>
            vsnA   = labelA.substr(0, labelA.find('/') );
        }

        xlrResult = XLR_FAIL;
        XLRCODE( xlrResult = ::XLRSelectBank(sshandle(), BANK_B) );
        if( xlrResult==XLR_SUCCESS ) {
            tmp[XLR_LABEL_LENGTH] = '\0';
            XLRCODE( ::XLRGetLabel(sshandle(), tmp) );
            labelB = string(tmp);
            vsnB   = labelB.substr(0, labelB.find('/') );
        }
        // Test that there _are_ disks to be erased
        EZASSERT2(!(vsnA.empty() && vsnB.empty()), xlrexception, EZINFO("No disk packs loaded to erase"));

        // We only overwrite the labels in case those are different from
        // each other. We leave the decision on wether they *must* be
        // written up to another part of the code
        if( vsnA!=vsnB ) {
            ownVSN       = labelA;
            companionVSN = labelB;
        }
        mustWriteVSN = ((vsnA!=vsnB) || curLayout!=layoutName);
    }

    // JonQ ["who else?" ...] found that an error during forceLayout would
    // leave the userdirectory in an unrecognized state - a
    // userdirectory of size 0, 
    // or, rather, after the XLRErase() there *is*
    // no user directory (i.e. size==0) until someone writes one and if we
    // don't because the forceLayout() or write_vsn() failed to complete
    // succesfully then that state remains unchanged.
    // So before write_vsn() we had better verify that the forced layout can
    // cope with the current disk pack (number of disks, capacity/disk -
    // there are layouts who cannot represent larger disks or have to few
    // disks (e.g. forcing an 8Disk layout on a non-bank system (which has
    // 16 disks))
    Zero<S_DEVINFO>             curDevInfo;
    const Zero<S_DRIVEINFO>     noDriveInfo;
    std::vector<S_DRIVEINFO>    curDriveInfo;
   
    XLRCALL( ::XLRGetDeviceInfo(sshandle(), &curDevInfo) );
    for(unsigned int bus = 0; bus<curDevInfo.NumBuses; bus++) {
        for(unsigned int ms = 0; ms<2; ms++) {
            S_DRIVEINFO       di;
            XLR_RETURN_CODE   dsk = XLR_FAIL;
            
            XLRCODE( dsk = ::XLRGetDriveInfo(sshandle(), bus, ((ms==0) ? XLR_MASTER_DRIVE : XLR_SLAVE_DRIVE), &di) );
            // We don't trust the SDK to leave our drive info struct
            // untouched if it returns fail so we explicitly zero it out in
            // such and event
            if( dsk==XLR_FAIL )
                di = noDriveInfo;
            curDriveInfo.push_back( di );
        }
    }
    // Now that we have collected that - attempt to force the layout first
    // on a temporary object [otherwise we clobber our current state, which
    // we would like to stay unmodified in case of an exception ...]
    UserDirectory   tmpUD;

    tmpUD.forceLayout(layoutName, curDriveInfo);

    // Attempt to write the own VSN/companion VSN
    // and depending on wether or not that *has* to be done failure is fatal or not
    try {
        tmpUD.setVSN( ownVSN );
        tmpUD.setCompanionVSN( companionVSN );
    }
    catch( ... ) {
        if( mustWriteVSN )
            throw;
    }

    // Now we can erase & force our own userdir layout onto the erased disk pack
    XLRCALL( ::XLRErase(sshandle(), owm) );
    {
        mutex_locker locker( mydevice->user_dir_lock );
        mydevice->user_dir.forceLayout( layoutName, curDriveInfo );
        // Update own + companion VSN - sometimes it is permissable for this
        // to fail (see above)
        try {
            mydevice->user_dir.setVSN( ownVSN );
            mydevice->user_dir.setCompanionVSN( companionVSN );
        }
        catch( ... ) {
            if( mustWriteVSN )
                throw;
        }
    }
    write_vsn( label ); // will also write the layout to disk    
}

void xlrdevice::erase_last_scan() {
    mutex_locker locker( mydevice->user_dir_lock );
    unsigned int number_of_scans = mydevice->user_dir.nScans();
    ASSERT2_COND( number_of_scans > 0, SCINFO("no scans to erase") );
    ROScanPointer scan = mydevice->user_dir.getScan( number_of_scans - 1 );
    XLRCODE( playpointer scan_start = scan.start() );
    XLRCALL( ::XLRTruncate(sshandle(), scan_start.AddrHi, scan_start.AddrLo) );
    mydevice->user_dir.remove_last_scan();
    mydevice->user_dir.write( *this );
}

#ifndef NOSSAPI
static const DRIVETYPE  masterslave[2] = {XLR_MASTER_DRIVE, XLR_SLAVE_DRIVE};
#endif

xlrdevice::mount_status_type xlrdevice::update_mount_status() {
    bool             faulty = false;
    string           vsn;
    mount_point_type mount_point = NoBank;

    mutex_locker user_dir_locker( mydevice->user_dir_lock );

    {
        // we need retreive one consistent state of the StreamStor,
        // so lock it
        mutex_locker xlr_locker( xlr_access_lock );
        
        // first check that we are not playing/recording
        S_DEVSTATUS dev_status;
        LOCKED_XLRCALL( ::XLRGetDeviceStatus(sshandle(), &dev_status) );
        if ( dev_status.Playing || dev_status.Recording ) {
            return mydevice->mount_status;
        }

        LOCKED_XLRCALL( ::XLRGetDeviceInfo(sshandle(), &mydevice->devinfo) );

        if ( mydevice->devinfo.TotalCapacity != 0 ) {
            // assume something mounted
            if (bankMode() == SS_BANKMODE_DISABLED) {
                mount_point = NonBankMode;
        
                char label[XLR_LABEL_LENGTH + 1];
                label[XLR_LABEL_LENGTH] = '\0';
                try {
                    LOCKED_XLRCALL( ::XLRGetLabel(sshandle(), label) );
                }
                catch ( xlrexception& e ) {
                    // try again with SKIPCHECKDIR on
                    LOCKED_XLRCALL( ::XLRSetOption(sshandle(), SS_OPT_SKIPCHECKDIR) );
                    LOCKED_XLRCALL( ::XLRGetLabel(sshandle(), label) );
                }
        
                vsn = label;
            }
            else {
                S_BANKSTATUS bank_status;
                LOCKED_XLRCALL( ::XLRGetBankStatus(sshandle(), 0, &bank_status) );
                if ( bank_status.Selected && (bank_status.State == STATE_READY) ) {
                    vsn = bank_status.Label;
                    mount_point = BankA;
                }
                else {
                    LOCKED_XLRCALL( ::XLRGetBankStatus(sshandle(), 1, &bank_status) );
                    if ( bank_status.Selected && (bank_status.State == STATE_READY) ) {
                        vsn = bank_status.Label;
                        mount_point = BankB;
                    }
                }
                EZASSERT2 ( bank_status.Selected, xlrexception, EZINFO("No bank selected, but device reports a non-empty capacity") );
                faulty = ( bank_status.MediaStatus == MEDIASTATUS_FAULTED );
            }
            vsn = vsn.substr( 0, vsn.find('/') );
        }
    } // end of xlr lock
   
    mount_status_type new_state( mount_point, vsn );
    if ( new_state != mydevice->mount_status ) {
        if ( mount_point == NoBank ) {
            DEBUG( 0, "Bank deselect detected" << endl );
        }
        else if ( mount_point == NonBankMode ) {
            DEBUG( 0, "New mounting in non-bank mode detected, " << vsn << endl );
        }
        else {
            DEBUG( 0, "New bank mounting detected, " << vsn << " in bank " << (mount_point == BankA ? "A" : "B") << endl );
        }
        if ( faulty ) {
            // to be able to do anything with this disk we probably need the SKIPCHECKDIR option
            DEBUG( -1, "Detected faulty disk, turning skip check dir on" << endl);
            XLRCALL( ::XLRSetOption(sshandle(), SS_OPT_SKIPCHECKDIR) );
        } else {
            // If no reason to assume faulty, clear this option!
            XLRCALL( ::XLRClearOption(sshandle(), SS_OPT_SKIPCHECKDIR) );
        }
        mydevice->user_dir.read( *this, mount_point!=NoBank );
        mydevice->mount_status = new_state;
        mydevice->recording_scan = false;
        if ( mount_point != NoBank ) {

            locked_set_drive_stats( vector<ULONG>() ); // empty vector will use current settings
            // Special request from Paul Burgess of JBO - can we display
            // disk info, like Mark5A/DIMino does? [Of course we can Paul!]
            for(unsigned int nr = 0; nr<16; nr++) {
                S_DRIVEINFO        di;
                XLR_RETURN_CODE    dsk = XLR_FAIL;
                const unsigned int bus = nr/2, slave = (nr % 2);
                XLRCODE( const DRIVETYPE    ms = masterslave[slave] );

                XLRCODE( dsk = ::XLRGetDriveInfo(sshandle(), bus, ms, &di) );
                if( dsk==XLR_SUCCESS ) {
                    DEBUG(1, format("DISK: Bus %02d/%s\t%s/%s/%s %lu",
                                    bus, (slave?"slave":"master"), ::strip(di.Model).c_str(), 
                                    ::strip(di.Serial).c_str(), ::strip(di.Revision).c_str(), di.Capacity*512)
                             << endl);
                }
            }
        }
    }
    return mydevice->mount_status;
}

void xlrdevice::locked_set_drive_stats( vector<ULONG> settings ) {

    if ( !settings.empty() ) {
        ASSERT_COND( settings.size() == drive_stats_length );
        mydevice->drive_stats_settings = settings;
    }

    XLRCALL( ::XLRSetOption(sshandle(), SS_OPT_DRVSTATS) );
    
    XLRCODE(
    S_DRIVESTATS set_ranges[XLR_MAXBINS];
    for (unsigned int i = 0; i < XLR_MAXBINS; i++) {
        if ( i < drive_stats_length ) {
            set_ranges[i].range = mydevice->drive_stats_settings[i];
        }
        else {
            set_ranges[i].range = -1;
        }
        set_ranges[i].count = 0;
    }
    XLRCALL( ::XLRSetDriveStats(sshandle(), set_ranges) );
            );
}

void xlrdevice::set_drive_stats( vector<ULONG> settings ) {
    mutex_locker locker( mydevice->user_dir_lock );
    locked_set_drive_stats( settings );
}

vector< ULONG > xlrdevice::get_drive_stats( ) {
    mutex_locker locker( mydevice->user_dir_lock );
    return mydevice->drive_stats_settings;
}

xlrdevice::~xlrdevice() {
}


// insert into a stream
ostream& operator<<( ostream& os, const xlrdevice& d ) {
    const xlrdevice::xlrdevice_type& dt( *d.mydevice );

    if( dt.devnum==xlrdevice::noDevice ) {
        os << "<not initialized>";
    } else {
        double            capacity;
        S_DEVINFO const*  dptr = &dt.devinfo;

        // compute capacity in MB
        capacity = (((double)dptr->TotalCapacity) * 4096.0 )/( 1024.0 * 1024.0 * 1024.0);

        os << "XLR#" << dt.devnum << ": " << dptr->BoardType << " Serial: " << dptr->SerialNum << endl
            << "       "
            << dptr->NumDrives << " drive" << ((dptr->NumDrives!=1)?("s"):("")) << "/"
            << format("%.2lfGB", capacity) << " capacity/"
            << dptr->NumBuses << " bus" << ((dptr->NumBuses!=1)?("es"):("")) << "/" 
            << dptr->NumExtPorts << " ext. port" << ((dptr->NumExtPorts!=1)?("s"):(""))
            << endl;
    }
    return os;
}

// The actual implementation
xlrdevice::xlrdevice_type::xlrdevice_type() :
    devnum( xlrdevice::noDevice ), sshandle( INVALID_SSHANDLE ), bankMode( (S_BANKMODE)-1 ),
    drive_stats_settings(drive_stats_default_values, drive_stats_default_values + drive_stats_length)
{
    PTHREAD_CALL( ::pthread_mutex_init(&user_dir_lock, NULL) );
}

xlrdevice::xlrdevice_type::xlrdevice_type( UINT d ) :
    devnum( d ), drive_stats_settings(drive_stats_default_values, drive_stats_default_values + drive_stats_length)
{
    PTHREAD_CALL( ::pthread_mutex_init(&user_dir_lock, NULL) );
    DEBUG(1, "Opening XLRDevice #" << devnum << endl);
    // Attempt to open device and call XLRClose() if it fails
    // (as per Conduant/StreamStor API manual for XLROpen()).
    XLRCALL2( ::XLROpen(devnum, &sshandle),
              ::XLRClose(sshandle); XLRINFO(" devnum was " << devnum); );

    // Get device info
    XLRCALL2( ::XLRGetDeviceInfo(sshandle, &devinfo),
              ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle); );

    // Get Daughterboard info. Daughterboards are optional so we call this
    // function but do *not* check its returnvalue
    do_xlr_lock();
    ::XLRGetDBInfo(sshandle, &dbinfo);
    do_xlr_unlock();

    // Get the software revisions
    XLRCALL2( ::XLRGetVersion(sshandle, &swrev),
              ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle); );

    // And get current device status.
    // If the 'SystemReady' flag is NOT set, we're up a certain creek where ony
    // typically finds oneself w/o paddle ...
    // If the fifo is full at time of opening, that's *also* not good! 
    // In those cases, we try a reset and check again?
    // Only try it a certain maximum nr of times
    bool               reset = false;
    unsigned int       nreset = 0;
    const unsigned int max_nreset = 4;
    do {
        // do get the current status
        XLRCALL2( ::XLRGetDeviceStatus(sshandle, &devstatus),
                  ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle); );
        if( (reset = (reset||(!devstatus.SystemReady))) )
            DEBUG(1, "Device #" << devnum << " is not Ready!");
        if( (reset = (reset||(devstatus.FifoFull))) )
            DEBUG(1, "Device #" << devnum << " fifo is Full!");

        if( reset ) {
            DEBUG(1, " Resetting device #" << devnum << ", see if it helps ...");
            XLRCALL2( ::XLRReset(sshandle),
                      ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle) );
        }
    } while( reset && (++nreset<=max_nreset) );

    // If reset is *still* true, we were not able to recover from
    // a condition that triggered the code to try to reset. Give up.
    if( reset ) {
        ostringstream  oss;
        oss << "Failed to clear reset-condition(s) after "
            << nreset << " tries. Giving up.";
        throw xlrexception(oss.str());
    }

    // If we end up here we know the device is online
    // Force normal bankmode [should be the default but we'd
    // better enforce this]
    this->setBankMode( SS_BANKMODE_NORMAL );
}

void xlrdevice::xlrdevice_type::setBankMode( S_BANKMODE newmode ) {
    XLRCALL2( ::XLRSetBankMode(sshandle, newmode),
              ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle) );
    bankMode = newmode;
}
string xlrdevice::xlrdevice_type::read_label( void ) {
    char label[XLR_LABEL_LENGTH + 1];
    label[XLR_LABEL_LENGTH] = '\0';

    XLRCALL( ::XLRGetLabel( sshandle, label) );
    return string(label);
}

xlrdevice::xlrdevice_type::~xlrdevice_type() {
    if( sshandle!=INVALID_SSHANDLE ) {
        DEBUG(1, "Closing XLRDevice #" << devnum << endl);
        do_xlr_lock();
        ::XLRClose( sshandle );
        do_xlr_unlock();
    }
}


xlrreg::teng_registermap xlrdbRegisters( void ) {
    xlrreg::teng_registermap   rv;

// Only fill in the mapping if we actually *have* the register definitions
#if HAVE_10GIGE
    // bit #4 in word SS_10GIGE_REG_MAC_FLTR_CTRL is the byte-length-check
    // enable bit
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_BYTE_LENGTH_CHECK_ENABLE,
                                 xlrreg::regtype(1, 4, SS_10GIGE_REG_MAC_FLTR_CTRL))).second, xlrreg_exception);
    // bit 3 in this word is "reset monitor counters", not used at this time
    // bit 2 is the psn mode 2
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_PSN_MODE2,
                                 xlrreg::regtype(1, 2, SS_10GIGE_REG_MAC_FLTR_CTRL))).second, xlrreg_exception);
    // bit 1 is psn mode 1
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_PSN_MODE1,
                                 xlrreg::regtype(1, 1, SS_10GIGE_REG_MAC_FLTR_CTRL))).second, xlrreg_exception);
    // Read the 2 PSN mode bits in one go
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_PSN_MODES,
                                 xlrreg::regtype(2, 1, SS_10GIGE_REG_MAC_FLTR_CTRL))).second, xlrreg_exception);

    // bit 0 is disable mac filter control
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_DISABLE_MAC_FILTER,
                                 xlrreg::regtype(1, 0, SS_10GIGE_REG_MAC_FLTR_CTRL))).second, xlrreg_exception);

    // word 0x4 is the 32-bit DPOFST
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_DPOFST,
                                 xlrreg::regtype(SS_10GIGE_REG_DATA_PAYLD_OFFSET))).second, xlrreg_exception);
    // word 0x5 is the 32-bit DFOFST
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_DFOFST,
                                 xlrreg::regtype(SS_10GIGE_REG_DATA_FRAME_OFFSET))).second, xlrreg_exception);
    // word 0x6 is the 32-bit PSNOFST
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_PSNOFST,
                                 xlrreg::regtype(SS_10GIGE_REG_PSN_OFFSET))).second, xlrreg_exception);
    // word 0x7 is the BYTE_LENGTH - the length of the packets
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_BYTE_LENGTH,
                                 xlrreg::regtype(SS_10GIGE_REG_BYTE_LENGTH))).second, xlrreg_exception);

    // bit 4 in word 0xD = promiscuous mode
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_PROMISCUOUS,
                                 xlrreg::regtype(1, 4, SS_10GIGE_REG_ETHR_FILTER_CTRL))).second, xlrreg_exception);
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_CRC_CHECK_DISABLE,
                                 xlrreg::regtype(1, 2, SS_10GIGE_REG_ETHR_FILTER_CTRL))).second, xlrreg_exception);
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_DISABLE_ETH_FILTER,
                                 xlrreg::regtype(1, 0, SS_10GIGE_REG_ETHR_FILTER_CTRL))).second, xlrreg_exception);
    // Packet length filtering registers
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_PACKET_LENGTH_CHECK_ENABLE,
                                 xlrreg::regtype(1, 31, SS_10GIGE_REG_ETHR_PKT_LENGTH))).second, xlrreg_exception);
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_PACKET_LENGTH,
                                 xlrreg::regtype(16, 0, SS_10GIGE_REG_ETHR_PKT_LENGTH))).second, xlrreg_exception);

    // The fill pattern
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_FILL_PATTERN,
                                 xlrreg::regtype(SS_10GIGE_REG_FILL_PATTERN))).second, xlrreg_exception);

    // MAC filter 0xF
    //   1 32-bit word for the 4 least significant bytes
    //   1 16-bit 'word' for the 2 most significant bytes (half a 32-bit  word)
    //   1 bit    in the MSB word
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_MAC_F_LO,
                                 xlrreg::regtype(SS_10GIGE_REG_SRC_ADDR_F_LSB))).second, xlrreg_exception);
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_MAC_F_HI,
                                 xlrreg::regtype(16, 0, SS_10GIGE_REG_SRC_ADDR_F_MSB))).second, xlrreg_exception);
    EZASSERT(rv.insert(make_pair(xlrreg::TENG_MAC_F_EN,
                                 xlrreg::regtype(1, 31, SS_10GIGE_REG_SRC_ADDR_F_MSB))).second, xlrreg_exception);
#endif   // HAVE_10GIGE

    return rv;
}

