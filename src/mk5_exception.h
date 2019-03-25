// exceptions to make proper reporting of error codes possible
// Copyright (C) 2007-2013 Harro Verkouter
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
#ifndef JIVE5AB_MK5_EXCEPTION_H
#define JIVE5AB_MK5_EXCEPTION_H
#include <ezexcept.h>

// Generic exception when dealing with Mk5 commands,
// much like setting errno to EINVAL
DECLARE_EZEXCEPT(cmdexception)

// Specific error codes - see Mark5A, B, C command set manuals
DECLARE_EZEXCEPT(Error_Code_6_Exception)
DECLARE_EZEXCEPT(Error_Code_8_Exception)

#endif
