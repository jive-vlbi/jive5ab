// implement somewhat calibrated busywait [to ~microsec accuracy]
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
#ifndef EVLBI_BUSYWAIT_H
#define EVLBI_BUSYWAIT_H

#include <string>
#include <exception>



// does a (surprise surprise!) busywait of n microseconds
// can throw up if the calibration fails. the thrown exception
// is derived from std::exception
void busywait(unsigned int n);


// an instance of this will be thrown upon failure to calibrate
struct calibrationfail:
    public std::exception
{
    calibrationfail(const std::string& s);
    virtual const char* what( void ) const throw();
    virtual ~calibrationfail() throw();

    const std::string __m;
};

#endif
