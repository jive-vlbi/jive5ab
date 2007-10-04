// implementation

#include <xlrdevice.h>
#include <evlbidebug.h>
#include <streamutil.h>

#include <stdio.h>

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
            << "       " << dptr->NumDrives << " drives/"
            << format("%.2lfGB", capacity) << " capacity/"
            << dptr->NumBuses << " buses/" 
            << dptr->NumExtPorts << " ext. ports" << endl;
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
    // (as per Conduant/StreamStor API manual for XLROpen())
    XLRCALL2( ::XLROpen(devnum, &sshandle), ::XLRClose(sshandle); XLRINFO(" devnum was " << devnum); );

    XLRCALL2( ::XLRGetDeviceInfo(sshandle, &devinfo), ::XLRClose(sshandle); XLRINFO(" sshandle was " << sshandle); );
}

xlrdevice::xlrdevice_type::~xlrdevice_type() {
    if( sshandle!=INVALID_SSHANDLE ) {
        DEBUG(1, "Closing XLRDevice #" << devnum << endl);
        ::XLRClose( sshandle );
    }
}

