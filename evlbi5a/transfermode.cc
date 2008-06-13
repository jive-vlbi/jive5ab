// implementation
#include <transfermode.h>
#include <sstream>

using namespace std;


tmexception::tmexception( const string& m ):
    __m( m )
{}

const char* tmexception::what( void ) const throw() {
    return __m.c_str();
}
tmexception::~tmexception() throw() 
{}


flagtype::flagtype(unsigned int f, const string& name):
    __f( f ), __nm( name )
{}


// This defines the enum => flag mapping
const transfer_submode::flagmap_type& get_flagmap( void ) {
    static transfer_submode::flagmap_type  __map = transfer_submode::flagmap_type();

    if( __map.size() )
        return __map;

    // Not filled in: do that now
    pair<transfer_submode::flagmap_type::iterator,bool> insres;

    // a value for the pause flag
    insres = __map.insert( make_pair(pause_flag, flagtype(0x1, "PAUSE")) );
    if( !insres.second )
        throw tmexception("Failed to insert pause_flag");

    // a value for the run flag
    insres = __map.insert( make_pair(run_flag, flagtype(0x2, "RUN")) );
    if( !insres.second )
        throw tmexception("Failed to insert run_flag");

    // a value for the wait flag
    insres = __map.insert( make_pair(wait_flag, flagtype(0x4, "WAIT")) );
    if( !insres.second )
        throw tmexception("Failed to insert wait_flag");

    // a value for the connected flag
    insres = __map.insert( make_pair(connected_flag, flagtype(0x8, "CONNECTED")) );
    if( !insres.second )
        throw tmexception("Failed to insert connected_flag");

    // done
    return __map;
}

// default: no flags set (what a surprise)
transfer_submode::transfer_submode() :
    flgs( 0x0 )
{}

// set a flag
transfer_submode& transfer_submode::operator|=(submode_flag f) {
    const flagmap_type&          fm( get_flagmap() );
    flagmap_type::const_iterator fmptr;

    if( (fmptr=fm.find(f))==fm.end() ) {
        ostringstream e;
        e << "Unknown submode flag " << f;
        throw tmexception(e.str());
    }
    flgs |= fmptr->second.__f;
    return *this;
}
// for balancing with "clr()"
transfer_submode& transfer_submode::set( submode_flag f ) {
    return (*this |= f);
}

// Clear a flag
transfer_submode& transfer_submode::clr( submode_flag f ) {
    const flagmap_type&          fm( get_flagmap() );
    flagmap_type::const_iterator fmptr;

    if( (fmptr=fm.find(f))==fm.end() ) {
        ostringstream e;
        e << "Unknown submode flag " << f;
        throw tmexception(e.str());
    }
    flgs &= (~fmptr->second.__f);
    return *this;
}
// clear all
void transfer_submode::clr_all( void ) {
    flgs = 0;
    return;
}

// check if a flag is set
bool transfer_submode::operator&( submode_flag f ) const {
    const flagmap_type&          fm( get_flagmap() );
    flagmap_type::const_iterator fmptr;

    if( (fmptr=fm.find(f))==fm.end() ) {
        ostringstream e;
        e << "Unknown submode flag " << f;
        throw tmexception(e.str());
    }
    return ((flgs & fmptr->second.__f)==fmptr->second.__f);
}

// Return a new object which is the bitwise or of
// this + the new flag
transfer_submode transfer_submode::operator|(submode_flag f) const {
    transfer_submode   t( *this );
    t|=f;
    return t;
}


#define KEES(o, a) \
    case a: os << #a; break;

// Show the major transfermode in human-readable format
ostream& operator<<(ostream& os, const transfer_type& tt) {
    switch( tt ) {
        KEES(os, no_transfer);
        KEES(os, disk2net);
        KEES(os, in2net);
        KEES(os, net2out);
        KEES(os, in2disk);
        default:
            os << "<invalid transfer_type #" << (int)tt;
            break;
    }
    return os;
}

// format the flags of the submode
ostream& operator<<(ostream& os, const transfer_submode& tsm ) {
    const transfer_submode::flagmap_type&           fm( get_flagmap() );
    transfer_submode::flagmap_type::const_iterator  fme;
    os << "<";
    for( fme=fm.begin(); fme!=fm.end(); ++fme) {
        if( tsm&fme->first )
            os << fme->second.__nm << ",";
    }
    os << ">";
    return os;
}
