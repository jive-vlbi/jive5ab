// Provide pre- and post C++11 compatible macro for "noexcept(false)"
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
#ifndef JIVE5AB_NOTHROW_H
#define JIVE5AB_NOTHROW_H

// Are we in C++11 happy land?
// https://stackoverflow.com/a/40512515
// https://stackoverflow.com/a/63491508
#if __cplusplus >= 201103L 

#   define THROWS(exception) noexcept(false)
#   define EMPTY_THROW       noexcept

#else

#   define THROWS(exception) throw(exception)
#   define EMPTY_THROW       throw()

#endif // not c++11 yet

#endif
