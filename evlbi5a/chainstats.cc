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
#include <chainstats.h>

using namespace std;

DEFINE_EZEXCEPT(chainstatistics)


statentry_type::statentry_type():
    stepname( "<none>" ), count( 0 )
{}
statentry_type::statentry_type(const string& nm, int64_t c):
    stepname( nm ), count( c )
{}

void chainstats_type::init(chain::stepid id, const string& name, int64_t n) {
    statsmap_type::iterator  statptr = statistics.find(id);

    if( statptr!=statistics.end() && statptr->second.stepname!=name ) {
        EZASSERT2(statptr->second.stepname==name, chainstatistics,
                  EZINFO("An entry for step #" << id << " is already present as " << statistics[id].stepname << " (attempt to set to " << name << ")"));
    }
    if( statptr==statistics.end() )
        statistics[id] = statentry_type(name, n);
}

void chainstats_type::add(chain::stepid id, int64_t amount) {
    EZASSERT2(statistics.find(id)!=statistics.end(), chainstatistics,
              EZINFO("No entry for step #" << id << " present?!"));
    statistics[id].count += amount;
}

volatile int64_t& chainstats_type::counter(chain::stepid id) {
    static int64_t          dummy;
    statsmap_type::iterator entry = statistics.find(id);

    if( entry!=statistics.end() )
        return entry->second.count;
    return dummy;
}

void chainstats_type::clear( void ) {
    statistics.clear();
}

chainstats_type::const_iterator chainstats_type::begin( void ) const {
    return statistics.begin();
}
chainstats_type::const_iterator chainstats_type::end( void ) const {
    return statistics.end();
}
