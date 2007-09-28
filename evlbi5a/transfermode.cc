// implementation
#include <transfermode.h>

using namespace std;

// default: no flags set (what a surprise)
transfer_submode::transfer_submode() :
    flgs( 0x0 )
{}

// set a flag
transfer_submode& transfer_submode::operator|=(submode_flag f) {
    flgs |= (unsigned int)f;
    return *this;
}
// for balancing with "clr()"
transfer_submode& transfer_submode::set( submode_flag f ) {
    return (*this |= f);
}

// Clear a flag
transfer_submode& transfer_submode::clr( submode_flag f ) {
    flgs &= ~((unsigned int)f);
    return *this;
}
// clear all
void transfer_submode::clr_all( void ) {
    flgs = 0;
    return;
}

// check if a flag is set
bool transfer_submode::operator&( submode_flag f ) const {
    return ((flgs & ((unsigned int)f))==((unsigned int)f));
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
        default:
            os << "<invalid transfer_type #" << (int)tt;
            break;
    }
    return os;
}

// format the flags of the submode
ostream& operator<<(ostream& os, const transfer_submode& tsm ) {
    os << "<";
    if( tsm&pause_flag )
        os << "PAUSE,";
    if( tsm&run_flag )
        os << "RUN,";
    if( tsm&wait_flag )
        os << "WAIT,";
    if( tsm&connected_flag )
        os << "CONNECTED,";
    os << ">";
    return os;
}
