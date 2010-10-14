// helper structs for codegeneration
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
#ifndef JIVE5A_VARIABLE_TYPE_H

#include <string>
#include <iostream>

// Base struct for actual types of variables.
// Keeps track of shared information and
// defines basic operations. some are actual
// type agnostic, others are not.
struct variable_type {
    // a variable MUST have a name
    variable_type(const std::string& nm);

    // standard declaration is w/o initializer
    // (calls declare(<string>) with empty string)
    std::string declare( void ) const;
    virtual std::string declare( const std::string& init ) const = 0;

    // actions that can be taken on the variable before or after
    // dereferencing
    enum action_type { noop, post_inc_addr, post_dec_addr, pre_inc_addr, pre_dec_addr};

    // standard dereferencing delegates to derived class' dereference
    // with action_type==noop
    std::string ref( void ) const;
    virtual std::string ref( action_type a ) const = 0;

    // Support pre- and post incrementing
    // operator++(void) === pre-*
    // operator++(int)  === post-*
    inline std::string operator++() const {
        return std::string("++")+name;
    }
    inline std::string operator--() const {
        return std::string("--")+name;
    }
    inline std::string operator++(int) const {
        return name+"++";
    }
    inline std::string operator--(int) const {
        return name+"--";
    }
    std::string operator*( void ) const;

    std::string operator&( const variable_type& v ) const;
    std::string operator+( unsigned int v ) const;

    const std::string name;

    virtual ~variable_type();

    private:
        // forbid default constructor!
        variable_type();
};
std::ostream& operator<<(std::ostream& os, const variable_type& v);

// actual implementations
struct pointer_variable:
    public variable_type
{
  using variable_type::declare;
  using variable_type::ref;

  pointer_variable(const std::string& nm);

  virtual std::string ref( action_type a ) const;
  virtual std::string declare( const std::string& init ) const;
};

struct local_variable:
    public variable_type
{
  using variable_type::declare;
  using variable_type::ref;

  local_variable(const std::string& nm);

  // local variables cannot have their address incremented/decremented
  virtual std::string ref( action_type ) const;
  virtual std::string declare( const std::string& init ) const;
};

#endif
