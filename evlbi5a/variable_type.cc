// implementation
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
//
#include <variable_type.h>
#include <sstream>

using namespace std;

variable_type::variable_type(const string& nm) :
    name( nm )
{}


    // standard dereferencing delegates to derived class' dereference
    // with action_type==noop
string variable_type::ref( void ) const {
    return this->ref(noop);
}

// standard declaration is w/o initializer
string variable_type::declare( void ) const {
    return this->declare( "" );
}

string variable_type::operator*( void ) const {
    return this->ref();
}

string variable_type::operator&( const variable_type& v ) const {
    return this->ref()+"&"+v.ref();
}
string variable_type::operator+( unsigned int v ) const {
    ostringstream s;
    s << name << "+" << v;
    return s.str();
}

bool variable_type::operator==(const variable_type& v) const {
    return v.name == this->name;
}

variable_type::~variable_type() {}


ostream& operator<<(ostream& os, const variable_type& v) {
    return os << v.name;
}

pointer_variable::pointer_variable(const string& nm):
    variable_type(nm)
{}

string pointer_variable::ref( action_type a ) const {
    string  rv( "*" );

//    rv += ((a!=noop)?("("):(""));
    rv += ((a==pre_inc_addr)?("++"):((a==pre_dec_addr)?("--"):("")));
    rv += this->variable_type::name;
    rv += ((a==post_inc_addr)?("++"):((a==post_dec_addr)?("--"):("")));
//  rv += ((a!=noop)?(")"):(""));
    return rv;
}

string pointer_variable::declare( const string& init ) const {
    string  rv("unsigned long long int");
    
    rv += (string(" *")+this->variable_type::name);

    if( !init.empty() )
        rv += string(" = (")+init+")";
    return rv;
}




local_variable::local_variable(const string& nm):
    variable_type(nm)
{}

// local variables cannot have their address incremented/decremented
string local_variable::ref( action_type ) const {
    return this->variable_type::name;
}
string local_variable::declare( const string& init ) const {
    string  rv("unsigned long long int");
    
    rv += (string(" ")+this->variable_type::name);

    if( !init.empty() )
        rv += string(" = (")+init+")";
    return rv;
}

