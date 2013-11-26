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
#ifndef JIVE5A_KVMAP_H
#define JIVE5A_KVMAP_H

#include <map>
#include <string>
#include <sstream>
#include <ezexcept.h>


DECLARE_EZEXCEPT(kvmap_exception)


// The meta-data header protocol is a bit like HTML or e-mail headers; a
// list of:
//  keyword: value
// pairs, separated by "\0" (in HTML, e-mail, this is typically "\r\n")
// The key, value section is ended by a double terminator, so basically an
// empty key,value section.
//
// Both keyword and value a string valued
// The key/value separator we use is ": " (colon followed by a space)


// The kvmap_type *is* a map of string => string but we decorate it with a
// few helper methods:
//      * setting values of arbitrary type; we do the translation to string
//      * convert to binary form
//      * extract from a binary form

struct kvmap_type :
    public std::map<std::string, std::string>
{
    typedef std::map<std::string, std::string>  BasicSelf;
    typedef BasicSelf::iterator                 iterator;
    typedef BasicSelf::const_iterator           const_iterator;

    static const char                                    nul;
    static const std::string                             separator;

    kvmap_type();

    iterator set(const std::string& key, const char* value);
    iterator set(const std::string& key, const std::string& value);

    // Set key 'key' to value 'value' (string representation of ~)
    // Note: some specializations are found below!
    template <typename T>
    iterator set(const std::string& key, const T& value) {
        std::ostringstream  valstr;

        valstr << value;
        return this->set(key, valstr.str());
    }


    // Conversion to/from binary form.
    // Note that "fromBinary()" will first erase all keys
    // and then load fresh from "bin".
    std::string  toBinary( void ) const;
    void         fromBinary( const std::string& bin );
};


#endif
