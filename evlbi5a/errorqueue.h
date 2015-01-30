// Provide an error queueing mechanism so errors can be inspected after they occur
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
//
#ifndef EVLBI5AB_ERRORQUEUE_H
#define EVLBI5AB_ERRORQUEUE_H

#include <iostream>
#include <string>
#include <sys/time.h>

// Errors, when they occur, can be pushed on the error queue.
// Queries like "status?" and "error?" will inspect and/or
// take errors off the queue.

// In this file we only have to declare the actual error object
// and some interface functions for errorqueue manipulation.
// The implementation of these methods is thread-safe.
// There is only one error queue for ALL runtimes in the
// system.

struct error_type {
  
    // default c'tor: 
    //    error_number == 0, error_message == "" 
    error_type();

    // It is asserted that the error number != 0
    // such that "error_number == 0" can be guaranteed
    // to have only come from the default c'tor
    error_type(int n, const std::string& m = "");

    // allow interpretation as bool - "number!=0" => true
    // because only *default* objects have number==0 this
    // can be used to tell "empty" and "non empty" errors apart
    operator bool( void ) const;

    // 0 => no error/empty
    const int            number;
    const std::string    message;

    // For error queue compression we have two extra fields:
    // the last time the error occurred and the number of times the error
    // occurred.
    mutable struct timeval time;
    mutable struct timeval time_last;
    mutable unsigned int   occurrences;
};

// Show in friendly format
std::ostream& operator<<(std::ostream& os, const error_type& eo);


// Append an error to the queue
void push_error( int e, const std::string& m );
void push_error( const error_type& et );

// Peek if there is an error pending, but leave
// it on the queue. You get a default object
// back if there are no pending error(s)
error_type peek_error( void );

// Almost the same as peek_error(), other than
// this pops the first entry off the queue,
// if there is an element
error_type pop_error( void );

#endif
