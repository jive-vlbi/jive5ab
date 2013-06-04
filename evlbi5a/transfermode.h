// bookeeping of transfermodes + submodes
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

#ifndef EVLBI5A_TRANSFERMODE_H
#define EVLBI5A_TRANSFERMODE_H

#include <iostream>
#include <map>
#include <string>
#include <exception>


struct tmexception:
    public std::exception
{
    tmexception( const std::string& m);
    virtual const char* what( void )const throw();
    virtual ~tmexception() throw();

    const std::string __m;
};


// What are we doing?
// [Note: record is an alias for in2disk and play is an alias for disk2out...]
// in2fork: send data over the internets + record to disk at the same time
// Note: no_transfer could've been called 'idle' but in order to not
// pollute with such a generic enumerationvalue ...
enum transfer_type {
    no_transfer,
    disk2net, disk2out, disk2file,
    in2net, in2disk, in2fork, in2file,
    net2out, net2disk, net2fork, net2file, net2check, net2sfxc, net2sfxcfork,
    fill2net, fill2file, fill2out,
    /* spill2net = fill -> split -> net; spin2net = in -> split -> net; spid2net = disk -> split -> net, etc */
    /* splet2* = network -> split -> * */
    spill2net, spid2net, spin2net, spin2file, splet2net, splet2file,
    spill2file, spid2file, spif2file, spif2net,
    file2check, file2mem, file2disk, file2net,
    in2mem, in2memfork, mem2net, mem2file, mem2sfxc, mem2time,
    net2mem,
    condition
};

// states a major transfer mode could be in. Which one(s) apply is
// entirely defined by the major mode itself...
// People should *never* assume that the enum corresponds to a specific value
enum submode_flag {
    pause_flag = 56,
    run_flag = 109,
    wait_flag = 42,
    connected_flag = 271
};


// Parse a string to an enumerated transfermode
// the match is case insensitive
// Returns 'no_transfer' if the string is unrecognized
transfer_type string2transfermode( const std::string& s );

// Check the implementations of "fromfile()" et.al. for
// an illustrative example.

// Operators that make supporting >1 transfer per function
// slightly more readable
bool fromfile(transfer_type tt);
bool tofile(transfer_type tt);

bool fromnet(transfer_type tt);
bool tonet( transfer_type tt);

// in2* and *2out
bool fromio(transfer_type tt);
bool toio(transfer_type tt);

// disk2* and *2disk [Streamstor, not regular file]
bool fromdisk(transfer_type tt);
bool todisk(transfer_type tt);

bool fromfill(transfer_type tt);

bool toout(transfer_type tt);


// bind an unsigned int (taken the be the actual flag value)
// and a string, the human-readable form/name of the flag
// together
struct flagtype {
    flagtype(unsigned int f, const std::string& name);

    const unsigned int __f;
    const std::string  __nm;
};

struct transfer_submode {
    // by mapping enum => flag we can ensure that 
    // no unknown flags get set. If the flag is not in
    // the map, it cannot be set/cleared
    typedef std::map<submode_flag, flagtype> flagmap_type;

    // default: no flags set (what a surprise)
    transfer_submode();

    // set a flag
    transfer_submode& operator|=(submode_flag f);

    // for balancing with "clr()" [it is functionally equal to "*this|=f"]
    transfer_submode& set( submode_flag f );

    // Clear a flag
    transfer_submode& clr( submode_flag f );

    // ...
    transfer_submode& clr_all( void );

    // check if a flag is set
    bool operator&( submode_flag f ) const;

    // Return a new object which is the bitwise or of
    // this + the new flag
    transfer_submode operator|(submode_flag f) const;

    private:
        unsigned int   flgs;

};
// get ro access to the defined flags
const transfer_submode::flagmap_type& get_flagmap( void );

// Show the major transfermode in human-readable format
std::ostream& operator<<(std::ostream& os, const transfer_type& tt);

// format the flags that are set within submode with textrepresentations
// as "<[flag,]*>"
// eg:
// if the run_flag and wait_flag are set it will show:
// <RUN, WAIT,>
std::ostream& operator<<(std::ostream& os, const transfer_submode& tsm );

#endif
