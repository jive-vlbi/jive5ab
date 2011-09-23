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

#include <stdint.h> // for [u]int<N>_t  types

DECLARE_EZEXCEPT(chainstatistics)

// Keep one of these per step in the chain
struct statentry_type {
    std::string      stepname;
    volatile int64_t count;

    statentry_type();
    statentry_type(const std::string& nm, int64_t c);
};


struct chainstats_type {
    typedef std::map<chain::stepid, statentry_type> statsmap_type;
    typedef statsmap_type::const_iterator const_iterator; 

    // initializes an entry for step <id>.
    // set the name of a step and an optional inital countervalue
    // (defaults to 0)
    void init(chain::stepid id, const std::string& name, int64_t n=0);

    // This'un ALWAYS returns a reference to an existing int64_t
    // value (the return value ALWAYS refers to valid *storage*).
    //
    // It MAY NOT be the counter you were hoping for; notably if the
    // indicated step has not been initialized.
    // (See "void init(chain::stepid, std::string&, int64_t)".).
    //
    // The dummy counter is shared between everyone who requests the counter
    // for a non-existing step.
    volatile int64_t& counter(chain::stepid id);

    // add <amount> to the counter for step <id>
    void add(chain::stepid id, int64_t amount);

    // empty the thing/fresh start
    void clear( void );

    // allow iteration over the entries (read-only)
    const_iterator begin( void ) const;
    const_iterator end( void ) const;

    private:
        statsmap_type  statistics;
};



#endif
