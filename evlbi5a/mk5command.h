// group all mark5 commands together in one location
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
#ifndef JIVE5A_MK5COMMAND_H
#define JIVE5A_MK5COMMAND_H

#include <runtime.h>
#include <map>
#include <string>
#include <vector>
//#include <mk5command/mk5.h>

// map commands to functions.
// The functions must have the following signature:
//
//     (std::string)(*)(bool, const std::vector<std::string>&, runtime&)
//
// [that is: a function taking a bool, a reference to a const std::vector of
//  std::strings and a reference to a environment, returning std::string]
//
// the bool indicating query or not, the vector<string>
// the arguments to the function and the 'runtime' environment upon
// which the command may execute. Obviously it's non-const... otherwise
// you'd have a rough time changing it eh!
//
// Note: fn's that do not need to access the environment (rarely...)
// may list it as an unnamed argument...
// Return a reply-string. Be sure to fully format the reply
// (including semi-colon and all)
//
// NOTE: the first entry in the vector<string> is the command-keyword
// itself, w/o '?' or '=' (cf. main() where argv[0] is the
// programname itself)
typedef std::string (*mk5cmd)(bool, const std::vector<std::string>&, runtime& rte);

// this is our "dictionary"
typedef std::map<std::string, mk5cmd>  mk5commandmap_type;

// This method will (obviously) not create a gazillion of instances
// but fills the map the first time you ask for it.
// Throws cmdexception() if it fails to insert (or
// something else goes wrong).
// We have different commandsets, depending on which hardware we're running
// on!
const mk5commandmap_type& make_mk5a_commandmap( bool buffering );
const mk5commandmap_type& make_dim_commandmap( bool buffering );
// buffering is not used for dom and generic command map
// the parameter is just here to keep the function signatures the same
const mk5commandmap_type& make_dom_commandmap( bool buffering = false ); 
const mk5commandmap_type& make_mk5c_commandmap( bool buffering ); 
const mk5commandmap_type& make_generic_commandmap( bool buffering = false );


#endif
