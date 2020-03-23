// implementation of the sciprint stuff
// Copyright (C) 2003-2010 Harro Verkouter
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
//
//  NOTE: this code was imported from the PCInt project into jive5a(b)
//
// $Id$
//
// $Log$
// Revision 1.2  2010/12/14 17:22:18  jive_cc
// HV: * gcc 4 doesn't like empty for-loops
//
// Revision 1.1  2010-10-14 09:19:35  jive_cc
// HV: This is the internal rewrite of jive5a.
//     The threading model has been ripped out and replaced by the
//     more flexible chains - allowing multiple processing/filtering
//     steps in a datatransfer; each step executing in a thread of
//     its own.
//     Channeldropping has been integrated; the algorithm developed
//     by Bob Eldering was wrapped into a new interface.
//     Insofar we can conclude this version (finally ...) includes
//     support for Mark5B+ e-VLBI.
//
// Revision 1.1  2003-03-26 10:22:11  verkout
// HV: Added this to enable 'scientific' printing.
//     The manipulator will print the number with
//     the proper thousands-indicator, from atto,
//     via milli to Tera and Exa..
#include <sciprint.h>


using std::string;

template <>
float absfn(const float& t) {
    return ::fabsf(t);
}
template <>
double absfn(const double& t) {
    return ::fabs(t);
}
template <>
unsigned int absfn(const unsigned int& t) {
    return t;
}

template <>
bool nearzero(const float& t) {
    return ::absfn(t)<=1.0e-7;
}
template <>
bool nearzero(const double& t) {
    return ::absfn(t)<=1.0e-13;
}

// The array of prefixes we know of.
// The two empty entries are essential! So if you
// decide to add new known prefixes, be sure to keep 
// the first empty entry where it is (between milli and kilo)
// and keep the second empty entry as the last entry in the list.
const string prfx::_prfx[] = {
	"atto",
	"femto",
	"p",
	"n",
	"u",
	"m",
	"",   // starting point for unit
	"k",
	"M",
	"G",
	"T",
	"Exa",
	""   // marks end of the list
};

// static method to find the index where to start from,
// the one with no prefix (ie. the first empty value)
int prfx::_findStartIndex( void ) {
	int                retval;
	const std::string* sptr = prfx::_prfx;

	for(retval=0 ; sptr->size()!=0; retval++, sptr++ ) {};
	return retval;
}

// Find the last index, ie. the index that is one less than
// the second empty string...
int prfx::_findLastIndex( void ) {
	int                retval( prfx::_findStartIndex()+1 );
	const std::string* sptr( &prfx::_prfx[retval] );

		
	for( ; sptr->size(); retval++, sptr++ ) {};
	return retval;
}
