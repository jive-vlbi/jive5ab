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

#include <stdio.h>
#include <strings.h>
#include <pthread.h>

using namespace std;


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

const S_DBINFO& xlrdevice::dbInfo( void ) const {
    return mydevice->dbinfo;
}

const S_DEVINFO& xlrdevice::devInfo( void ) const {
    return mydevice->devinfo;
}

const S_XLRSWREV& xlrdevice::swRev( void ) const {
    return mydevice->swrev;
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
}

xlrdevice::xlrdevice_type::~xlrdevice_type() {
    if( sshandle!=INVALID_SSHANDLE ) {
        DEBUG(1, "Closing XLRDevice #" << devnum << endl);
        do_xlr_lock();
        ::XLRClose( sshandle );
        do_xlr_unlock();
    }
}

