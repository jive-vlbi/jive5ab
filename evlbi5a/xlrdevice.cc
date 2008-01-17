// implementation

#include <xlrdevice.h>
#include <evlbidebug.h>
#include <streamutil.h>

#include <stdio.h>
#include <strings.h>

using namespace std;

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



// the exception
xlrexception::xlrexception( const string& s ):
    msg( s )
{}

const char* xlrexception::what() const throw() {
    return msg.c_str();
}
xlrexception::~xlrexception() throw()
{}


// The interface object

xlrdevice::xlrdevice():
   mydevice( new xlrdevice_type() )
{}

xlrdevice::xlrdevice( UINT d ):
    mydevice( new xlrdevice_type(d) )
{}

UINT xlrdevice::devnum( void ) const {
    return mydevice->devnum;
}

SSHANDLE xlrdevice::sshandle( void ) const {
    return mydevice->sshandle;
}

bool xlrdevice::isAmazon( void ) const {
    return (mydevice->devnum!=xlrdevice::noDevice &&
            ::strncasecmp(mydevice->devinfo.BoardType, "AMAZON", 6)==0);
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
    devnum( xlrdevice::noDevice ), sshandle( INVALID_SSHANDLE )
{}

xlrdevice::xlrdevice_type::xlrdevice_type( UINT d ):
    devnum( d )
{
    DEBUG(1, "Opening XLRDevice #" << devnum << endl);
    // Attempt to open device and call XLRClose() if it fails
    // (as per Conduant/StreamStor API manual for XLROpen()).
    XLRCALL2( ::XLROpen(devnum, &sshandle),
              ::XLRClose(sshandle); XLRINFO(" devnum was " << devnum); );

    // Get device info
    XLRCALL2( ::XLRGetDeviceInfo(sshandle, &devinfo),
              ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle); );
#if 0
    // Get Daughterboard info
    XLRCALL2( ::XLRGetDBInfo(sshandle, &dbinfo),
              ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle) );
#endif
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
}

xlrdevice::xlrdevice_type::~xlrdevice_type() {
    if( sshandle!=INVALID_SSHANDLE ) {
        DEBUG(1, "Closing XLRDevice #" << devnum << endl);
        ::XLRClose( sshandle );
    }
}

