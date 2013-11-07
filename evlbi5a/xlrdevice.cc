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
#include <dosyscall.h>
#include <carrayutil.h>
#include <mutex_locker.h>
#include <hex.h>

#include <strings.h>
#include <string.h>
#include <pthread.h>

using namespace std;


xlrreg::teng_registermap xlrdbRegisters( void );
xlrreg::teng_registermap xlrdevice::xlrdbregs = xlrdbRegisters();


DEFINE_EZEXCEPT(xlrreg_exception)


#ifdef NOSSAPI
XLR_RETURN_CODE XLRClose(SSHANDLE) { return XLR_FAIL; }
UINT            XLRDeviceFind( void )   { return 0; }
XLR_RETURN_CODE XLRGetDBInfo(SSHANDLE,PS_DBINFO) { return XLR_FAIL; }
XLR_RETURN_CODE XLRGetErrorMessage(char* e,XLR_ERROR_CODE) { 
    ::strcpy(e, "Compiled without support for StreamStor"); return XLR_SUCCESS; }
DWORDLONG       XLRGetFIFOLength(SSHANDLE)  { return 0; }
// StreamStor error codes start at 2 so we must be sure to return something
// that counts as an error
XLR_ERROR_CODE  XLRGetLastError( void ) { return 2; }
DWORDLONG       XLRGetLength(SSHANDLE)  { return 0; }
DWORDLONG       XLRGetPlayLength(SSHANDLE) { return 0; }
UINT            XLRGetUserDirLength(SSHANDLE) { return 0; }
XLR_RETURN_CODE XLRReadFifo(SSHANDLE,READTYPE*,ULONG,BOOLEAN) { return XLR_FAIL; }
XLR_RETURN_CODE XLRSkip(SSHANDLE,UINT,BOOLEAN) { return XLR_FAIL; }
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
        std::cerr << "do_xlr_lock() failed - " << ::strerror(rv) << std::endl;
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
        std::cerr << "do_xlr_unlock() failed - " << ::strerror(rv) << std::endl;
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



// The xlr register stuff

xlrreg_pointer::xlrreg_pointer():
    devHandle( ::noDevice ), wordnr( (UINT32)-1 ), startbit( (UINT32)-1 ),
    valuemask( 0 ), fieldmask( 0 )
{}

xlrreg_pointer::xlrreg_pointer(const xlrreg::regtype reg, SSHANDLE dev):
    devHandle( dev ), wordnr( reg.word ), startbit( reg.startbit ),
    valuemask( bitmasks<UINT32>()[reg.nbit] ), fieldmask( valuemask<<startbit )
{
    EZASSERT(reg.nbit != 0, xlrreg_exception);
    EZASSERT(devHandle != ::noDevice, xlrreg_exception );
}


const xlrreg_pointer& xlrreg_pointer::operator=( const bool& b ) {
    UINT32    value( (b)?((UINT32)0x1):((UINT32)0x0) );
    // forward to normal operator=()
    return this->operator=(value);
}

UINT32 xlrreg_pointer::operator*( void ) const {
    UINT32     w;

    EZASSERT(devHandle!=::noDevice, xlrreg_exception);
    XLRCALL( ::XLRReadDBReg32(devHandle, wordnr, &w) );
    return ((w&((UINT32)fieldmask))>>startbit);
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

const S_DEVINFO& xlrdevice::devInfo( void ) const {
    return mydevice->devinfo;
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

xlrreg_pointer xlrdevice::operator[](xlrreg::teng_register reg) {
    xlrreg::teng_registermap::const_iterator curreg;

    // Assert that the register actually is defined
    EZASSERT((curreg=xlrdevice::xlrdbregs.find(reg))!=xlrdevice::xlrdbregs.end(), xlrreg_exception);

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

    // add a * to the name to indicate a scan in process of recording
    scan.setName( name + "*" );
    scan.setStart( ::XLRGetLength(sshandle()) );
    scan.setLength( 0 );
    mydevice->user_dir.setScan( scan );
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

void xlrdevice::write_state( std::string new_state ) {
    // new state has to be appended to VSN, so retreive that
    char label[XLR_LABEL_LENGTH + 1];
    label[XLR_LABEL_LENGTH] = '\0';

    XLRCALL( ::XLRGetLabel( sshandle(), label) );
    pair<string, string> vsn_state = disk_states::split_vsn_state(string(label));
    // and write the vsn with new state appended
    write_label(vsn_state.first + '\036' + new_state);
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

void xlrdevice::erase( const SS_OWMODE XLRCODE(owm) ) {
    XLRCALL( ::XLRErase(sshandle(), owm) );
    mutex_locker locker( mydevice->user_dir_lock );
    mydevice->user_dir.clear_scans();
    mydevice->user_dir.write( *this );
}

void xlrdevice::erase( std::string layoutName, const SS_OWMODE XLRCODE(owm) ) {
    XLRCALL( ::XLRErase(sshandle(), owm) );
    {
        mutex_locker locker( mydevice->user_dir_lock );
        mydevice->user_dir.forceLayout( layoutName );
    }
    
    // restore the VSN
    char label[XLR_LABEL_LENGTH + 1];
    label[XLR_LABEL_LENGTH] = '\0';
    XLRCALL( ::XLRGetLabel( sshandle(), label) );
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


void xlrdevice::update_mount_status() {
    string vsn;
    mount_point_type mount_point = NoBank;

    // first check that we are not playing/recording
    S_DEVSTATUS dev_status;
    XLRCALL( ::XLRGetDeviceStatus(sshandle(), &dev_status) );
    if ( dev_status.Playing || dev_status.Recording ) {
        return;
    }

    XLRCALL( ::XLRGetDeviceInfo(sshandle(), &mydevice->devinfo) );

    bool faulty = false;
    if ( mydevice->devinfo.TotalCapacity != 0 ) {
        // assume something mounted
        if (bankMode() == SS_BANKMODE_DISABLED) {
            mount_point = NonBankMode;
        
            char label[XLR_LABEL_LENGTH + 1];
            label[XLR_LABEL_LENGTH] = '\0';
            try {
                XLRCALL( ::XLRGetLabel(sshandle(), label) );
            }
            catch ( xlrexception& e ) {
                // try again with SKIPCHECKDIR on
                XLRCALL( ::XLRSetOption(sshandle(), SS_OPT_SKIPCHECKDIR) );
                XLRCALL( ::XLRGetLabel(sshandle(), label) );
            }
        
            vsn = label;
        }
        else {
            S_BANKSTATUS bank_status;
            XLRCALL( ::XLRGetBankStatus(sshandle(), 0, &bank_status) );
            if ( bank_status.Selected ) {
                vsn = bank_status.Label;
                mount_point = BankA;
            }
            else {
                XLRCALL( ::XLRGetBankStatus(sshandle(), 1, &bank_status) );
                if ( bank_status.Selected ) {
                    vsn = bank_status.Label;
                    mount_point = BankB;
                }
            }
            ASSERT_COND ( bank_status.Selected );
            faulty = ( bank_status.MediaStatus == MEDIASTATUS_FAULTED );
        }
        vsn = vsn.substr( 0, vsn.find('/') );

    }
    
    mount_status_type new_state( mount_point, vsn );
    if ( new_state != mydevice->mount_status ) {
        if ( faulty ) {
            // to be able to do anything with this disk we probably need the SKIPCHECKDIR option
            DEBUG( -1, "Detected faulty disk, turning skip check dir on" << endl);
            XLRCALL( ::XLRSetOption(sshandle(), SS_OPT_SKIPCHECKDIR) );
        }
        mutex_locker locker( mydevice->user_dir_lock );
        mydevice->user_dir.read( *this );
        mydevice->mount_status = new_state;
        mydevice->recording_scan = false;
        if ( mount_point != NoBank ) {
            locked_set_drive_stats( vector<ULONG>() ); // empty vector will use current settings
        }
    }
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

    return rv;
}
