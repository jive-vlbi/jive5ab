// Helper class for simple meta-data header protocol
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
#include <threadfns/kvmap.h>
#include <stringutil.h>
#include <vector>


using namespace std;

const char kvmap_type::nul         = '\0';
const string kvmap_type::separator = ": ";


DEFINE_EZEXCEPT(kvmap_exception)


kvmap_type::kvmap_type()
{}


// Overloaded functions for std::string and const char* 
kvmap_type::iterator kvmap_type::set(const std::string& key, const char* value) {
    return this->set(key, std::string(value));
}

kvmap_type::iterator kvmap_type::set(const std::string& key, const string& value) {
    return this->BasicSelf::insert( std::make_pair(key, value) ).first;
}

string kvmap_type::toBinary( void ) const {
    ostringstream binform;

    for(BasicSelf::const_iterator kv=this->BasicSelf::begin(); kv!=this->BasicSelf::end(); kv++)
        binform << kv->first << kvmap_type::separator << kv->second << kvmap_type::nul;
    binform << kvmap_type::nul;
    return binform.str();
}

void kvmap_type::fromBinary( const string& bin ) {
    // ok, empty ourselves
    this->BasicSelf::clear();

    // Split the input string at our separator character to form 'lines'
    vector<string>  identifiers = ::split(bin, '\0', false);

    // iterate over the individual lines and break them up at our separator
    for(size_t i=0; i<identifiers.size(); i++) { 
        const size_t  separator_index = identifiers[i].find(kvmap_type::separator);

        // skip empty ones
        if( identifiers[i].empty() )
            continue;

        if( separator_index==string::npos ) {
            THROW_EZEXCEPT(kvmap_exception, "Failed to find separator in line: '" << identifiers[i] << "'" << endl);
        }
        (*this)[ identifiers[i].substr(0, separator_index) ] = 
                 identifiers[i].substr(separator_index + kvmap_type::separator.size());
    }

}

