// implementation
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
#include <dosyscall.h>

#include <errno.h>
#include <string.h>


using namespace std;

//
//  The 'last system-error type'
//
lastsyserror_type::lastsyserror_type():
    sys_errno( errno ), sys_errormessage( ((sys_errno!=0)?(::strerror(sys_errno)):("<success>")) )
{}

// if errno == 0, don't show anything
ostream& operator<<( ostream& os, const lastsyserror_type& lse ) {
    if( lse.sys_errno!=0 )
        os << " - " << lse.sys_errormessage << "(" << lse.sys_errno << ")";
    return os;
}

//
// the exception
//
syscallexception::syscallexception( const string& s ):
    msg( s )
{}

const char* syscallexception::what() const throw() {
    return msg.c_str();
}
syscallexception::~syscallexception() throw()
{}


