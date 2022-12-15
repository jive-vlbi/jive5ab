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
#include <carrayutil.h>
#include <sstream>
#include <algorithm>
#include <string>

using namespace std;

// local prototype (def'n is below)
transfer_submode::flagmap_type init_flagmap( void );

// A static variable
static transfer_submode::flagmap_type  __map = init_flagmap();

bool fromfile(transfer_type tt) {
    static transfer_type transfers[] = {file2check, file2mem, spif2file, spif2net, file2disk, file2net, vbs2net};
    return find_element(tt, transfers);
}

bool tofile(transfer_type tt) {
    static transfer_type transfers[] = { disk2file, in2file, net2file, fill2file, spill2file, spif2file, spbs2file,
                                         splet2file, spin2file, spid2file, mem2file, net2vbs, fill2vbs, vbsrecord, mem2vbs };
    return find_element(tt, transfers);
}

bool fromnet(transfer_type tt) {
    static transfer_type transfers[] = { net2out, net2disk, net2fork, net2file, net2check, net2sfxc, net2sfxcfork,
                                         splet2net, splet2file, net2mem, net2vbs, vbsrecord };
    return find_element(tt, transfers);
}

bool tonet(transfer_type tt) {
    static transfer_type transfers[] = { disk2net, in2net, fill2net, spill2net, spid2net, spin2net, splet2net,
                                         spif2net, spbs2net, mem2net, file2net, vbs2net, stream2sfxc };
    return find_element(tt, transfers);
}

bool fromio(transfer_type tt) {
    static transfer_type transfers[] = { in2net, in2disk, in2fork, in2file, spin2net, spin2file, in2mem, in2memfork, tvr};
    return find_element(tt, transfers);
}

bool toio(transfer_type tt) {
    static transfer_type transfers[] = { disk2out, net2out, net2fork, fill2out };
    return find_element(tt, transfers);
}

bool fromdisk(transfer_type tt) {
    static transfer_type transfers[] = { disk2net, disk2out, disk2file, spid2net, spid2file, condition, bankswitch, stream2sfxc, mounting, disk2etransfer }; 
    return find_element(tt, transfers);
}

bool todisk(transfer_type tt) {
    static transfer_type transfers[] = { in2disk, net2disk, net2fork, in2memfork, file2disk, condition, bankswitch, mounting, fill2disk };
    return find_element(tt, transfers);
}

bool fromfill(transfer_type tt) {
    static transfer_type transfers[] = { fill2net, fill2file, spill2net, spill2file, fill2out, fill2vbs, fill2disk };
    return find_element(tt, transfers);
}

bool toqueue(transfer_type tt) {
    static transfer_type transfers[] = { file2mem, in2mem, in2memfork, net2mem };
    return find_element(tt, transfers);
}

bool isfork(transfer_type tt) {
    static transfer_type transfers[] = { net2fork, net2sfxcfork, in2memfork, in2fork };
    return find_element(tt, transfers);
}


bool fromvbs(transfer_type tt) {
    static transfer_type transfers[] = { spbs2file, spbs2net, vbs2net };
    return find_element(tt, transfers);
}

bool tovbs(transfer_type tt) {
    static transfer_type transfers[] = { fill2vbs, vbsrecord, net2vbs, mem2vbs };
    return find_element(tt, transfers);
}

bool diskunavail(transfer_type tt) {
    return (tt==condition || tt==bankswitch || tt==mounting);
}

bool streamstorbusy(transfer_type tt) {
    return (diskunavail(tt) || toio(tt) || fromio(tt) || todisk(tt) || fromdisk(tt));
}


#define TT(x)   {#x, x}
struct s2tt_type {
    std::string   s;
    transfer_type tt;
};
struct s2ttfinder {
    s2ttfinder(const std::string& s):
        s2find( s )
    {}

    bool operator()(const s2tt_type& s2tt) {
        return s2tt.s == s2find;
    }

    const std::string s2find;
};

transfer_type string2transfermode(const string& s ) {
    static s2tt_type  s2tt[] = {
        TT(disk2net),
        TT(disk2out),
        TT(disk2file),
        TT(disk2etransfer),
        TT(in2net),
        TT(in2disk),
        TT(in2fork),
        TT(in2file),
        TT(net2out),
        TT(net2disk),
        TT(net2fork),
        TT(net2file),
        TT(net2check),
        TT(net2sfxc),
        TT(net2sfxcfork),
        TT(fill2net),
        TT(fill2file),
        TT(fill2out),
        TT(fill2vbs),
        TT(fill2disk),
        TT(spill2net),
        TT(spid2net),
        TT(spin2net),
        TT(spin2file),
        TT(splet2net),
        TT(splet2file),
        TT(spill2file),
        TT(spid2file),
        TT(spif2file),
        TT(spif2net),
        TT(spbs2net),
        TT(spbs2file),
        TT(file2check),
        TT(file2mem),
        TT(file2disk),
        TT(in2mem),
        TT(in2memfork),
        TT(mem2net),
        TT(mem2file),
        TT(mem2sfxc),
        TT(file2net),
        TT(net2mem),
        TT(mem2time),
        TT(vbs2net),
        TT(net2vbs),
        TT(vbsrecord),
        TT(mem2vbs),
        TT(tvr),
        TT(compute_trackmask),
        TT(condition),
        TT(bankswitch),
        TT(mounting),
        TT(stream2sfxc)
    };
    s2tt_type* p =  std::find_if(s2tt, s2tt+array_size(s2tt), s2ttfinder(s));

    if( p!=(s2tt+array_size(s2tt)) )
        return p->tt;
    return no_transfer;
}


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

    // a value for the broken flag
    insres = tmp__map.insert( make_pair(broken_flag, flagtype(0x10, "BROKEN")) );
    if( !insres.second )
        throw tmexception("Failed to insert broken_flag");

    // done
    return tmp__map;
}

// default: no flags set (what a surprise)
transfer_submode::transfer_submode() :
    flgs( 0x0 )
{}

transfer_submode::transfer_submode(const transfer_submode& other):
    flgs( other.flgs )
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
        KEES(os, disk2etransfer);
        KEES(os, fill2net);
        KEES(os, fill2file);
        KEES(os, fill2out);
        KEES(os, fill2vbs);
        KEES(os, fill2disk);
        KEES(os, spill2net);
        KEES(os, splet2net);
        KEES(os, splet2file);
        KEES(os, spid2net);
        KEES(os, spin2net);
        KEES(os, spin2file);
        KEES(os, spill2file);
        KEES(os, spif2file);
        KEES(os, spif2net);
        KEES(os, spid2file);
        KEES(os, spbs2net);
        KEES(os, spbs2file);
        KEES(os, disk2out);
        KEES(os, disk2file);
        KEES(os, net2out);
        KEES(os, net2disk);
        KEES(os, net2fork);
        KEES(os, net2file);
        KEES(os, net2check);
        KEES(os, net2sfxc);
        KEES(os, net2sfxcfork);
        KEES(os, in2net);
        KEES(os, in2disk);
        KEES(os, in2fork);
        KEES(os, in2file);
        KEES(os, file2check);
        KEES(os, file2mem);
        KEES(os, file2net);
        KEES(os, file2disk);
        KEES(os, in2mem);
        KEES(os, in2memfork);
        KEES(os, mem2net);
        KEES(os, mem2file);
        KEES(os, mem2sfxc);
        KEES(os, net2mem);
        KEES(os, mem2time);
        KEES(os, vbs2net);
        KEES(os, net2vbs);
        KEES(os, vbsrecord);
        KEES(os, mem2vbs);
        KEES(os, tvr);
        KEES(os, compute_trackmask);
        KEES(os, condition);
        KEES(os, bankswitch);
        KEES(os, mounting);
        KEES(os, stream2sfxc);
        default:
            os << "<invalid transfer_type #" << (int)tt << ">";
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
