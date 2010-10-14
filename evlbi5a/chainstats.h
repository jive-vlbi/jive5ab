// keep track of throughput of chainsteps on a per step basis
// Copyright (C) 2007-2010 Harro Verkouter
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
#ifndef JIVE5AB_CHAINSTATS_H
#define JIVE5AB_CHAINSTATS_H

#include <chain.h>
#include <map>
#include <string>
#include <ezexcept.h>

DECLARE_EZEXCEPT(chainstatistics);

// Keep one of these per step in the chain
struct statentry_type {
    std::string   stepname;
    long long int count;

    statentry_type();
    statentry_type(const std::string& nm, long long int c);
};


struct chainstats_type {
    typedef std::map<chain::stepid, statentry_type> statsmap_type;
    typedef statsmap_type::const_iterator const_iterator; 

    // initializes an entry for step <id>.
    // set the name of a step and an optional inital countervalue
    // (defaults to 0)
    void init(chain::stepid id, const std::string& name, long long int n=0);

    // add <amount> to the counter for step <id>
    void add(chain::stepid id, long long int amount);

    // empty the thing/fresh start
    void clear( void );

    // allow iteration over the entries (read-only)
    const_iterator begin( void ) const;
    const_iterator end( void ) const;

    private:
        statsmap_type  statistics;
};



#endif
