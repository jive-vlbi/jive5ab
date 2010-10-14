// prettyprint value + unit in scientific notation into a C++ stream
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
// sciprint - manipulator that pretty prints 
//            values + units in scientific notation.
//            Give it a value and a unit and
//            it will determine the prefix for the
//            unit:
//
//            e.g:
//
//            cout << sciprint(1.6193654E9, "Hz") << endl
//                 << sciprint(3345.356E-6, "m") << endl;
//
//            would print:
//			  1.6193654 GHz
//            3.345 um
//
// $Id$
//
// $Log$
// Revision 1.2  2005-03-18 10:48:37  verkout
//
// Revision 1.1  2003/03/26 10:22:11  verkout
// HV: Added this to enable 'scientific' printing.
//     The manipulator will print the number with
//     the proper thousands-indicator, from atto,
//     via milli to Tera and Exa..
//
// 
//
#ifndef JIVE5A_SCIPRINT_H
#define JIVE5A_SCIPRINT_H

#include <string>
#include <iostream>
#include <math.h>

struct prfx {
	// The prefixes..
	static const std::string _prfx[];

	static int               _findStartIndex( void );
	static int               _findLastIndex( void );
};

// Add the 'unsigned' thous in the template decl; we use it
// to determine the thousand-folds. If, eg. you want to 
// display units of bytes in M,k,T or whatever, the thousands
// are actually 1024's...
template <class T,unsigned thous=1000>
struct sciprint {
	sciprint( T val, const std::string& unit ) :
		data( val ),
		u( unit )
	{}

	T           data;
	std::string u;

private:
	// prohibit default c'tor 
	sciprint();
};


template <typename T>
T absfn(const T& t) {
    return ::abs(t);
}
// let the compiler know there are specializations
template <>
float absfn(const float& t);
template <>
double absfn(const double& t);

template <typename T>
bool nearzero(const T& t) {
    return t==T(0);
}
template <typename T>
bool nearzero(const float& t);
template <typename T>
bool nearzero(const double& t);

template <class T,unsigned _thous>
std::ostream& operator<<( std::ostream& os, const sciprint<T,_thous>& sp ) {
	T               tmp( sp.data );
	int             idx( prfx::_findStartIndex() );
    bool            ltone;
	static const T  _one( T(1.0) );
	static const T  _thousand( (T)_thous );

	while( !nearzero(tmp) && ((ltone=(absfn(tmp)<_one)) || (absfn(tmp)>_thousand)) ) {
		if( ltone ) {
			tmp *= _thousand;
			idx--;
		} else {
			tmp /= _thousand;
			idx++;
		}
	}
	if( idx<0 || idx>=prfx::_findLastIndex() )
		os << sp.data << sp.u;
    else
		os << tmp << prfx::_prfx[idx] << sp.u;
	return os;
}

typedef sciprint<float>       sciprintf;
typedef sciprint<double>      sciprintd;
typedef sciprint<double,1024> byteprint;

#endif
