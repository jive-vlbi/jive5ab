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
#include <transfermode.h>
#include <sstream>

using namespace std;

// local prototype (def'n is below)
transfer_submode::flagmap_type init_flagmap( void );

// A static variable
static transfer_submode::flagmap_type  __map = init_flagmap();


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
    return __map;
}


transfer_submode::flagmap_type init_flagmap( void ) {
    transfer_submode::flagmap_type                      tmp__map;
    pair<transfer_submode::flagmap_type::iterator,bool> insres;

    // a value for the pause flag
    insres = tmp__map.insert( make_pair(pause_flag, flagtype(0x1, "PAUSE")) );
    if( !insres.second )
        throw tmexception("Failed to insert pause_flag");

    // a value for the run flag
    insres = tmp__map.insert( make_pair(run_flag, flagtype(0x2, "RUN")) );
    if( !insres.second )
        throw tmexception("Failed to insert run_flag");

    // a value for the wait flag
    insres = tmp__map.insert( make_pair(wait_flag, flagtype(0x4, "WAIT")) );
    if( !insres.second )
        throw tmexception("Failed to insert wait_flag");

    // a value for the connected flag
    insres = tmp__map.insert( make_pair(connected_flag, flagtype(0x8, "CONNECTED")) );
    if( !insres.second )
        throw tmexception("Failed to insert connected_flag");

    // done
    return tmp__map;
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
transfer_submode& transfer_submode::clr_all( void ) {
    flgs = 0;
    return *this;
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
        KEES(os, fill2net);
        KEES(os, fill2file);
        KEES(os, spill2net);
        KEES(os, spid2net);
        KEES(os, spill2file);
        KEES(os, spid2file);
        KEES(os, disk2out);
        KEES(os, disk2file);
        KEES(os, net2out);
        KEES(os, net2disk);
		KEES(os, net2file);
		KEES(os, net2check);
        KEES(os, net2sfxc);
        KEES(os, in2net);
        KEES(os, in2disk);
        KEES(os, in2fork);
        KEES(os, in2file);
        KEES(os, file2check);
        KEES(os, file2mem);
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
