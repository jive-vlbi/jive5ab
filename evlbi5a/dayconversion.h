//      Provide transformations from daynr to (day, month) and vice versa
//
//  Author:  Harro Verkouter,   15-6-1999
//
//  $Id$
// 
// Imported (and 'modernised') in jive5a, Feb. 2009.
// The "Leap-year-bug" was fixed, _quite_ some time before that ;)
// (many implementations, including this one, overlook the 'if mod 400 == 0'
//  condition).
//
#ifndef JIVE5A_DAYCONVERSION_H
#define JIVE5A_DAYCONVERSION_H


// Time conversion stuff
struct DayConversion {

public:
    //  Some useful constants....
    static const unsigned int secondsPerDay;

    //  Convert daynr to day/month and vice versa
    //
    //  Month 0 == January
    //  Day 0   == first day of month
    //  Daynr 0 == January first of year
    static bool  dayNrToMonthDay( unsigned int& month, unsigned int& day,
                                  unsigned int daynr, int year );
    static bool  dayMonthDayToNr( unsigned int& daynr, unsigned int month,
                                  unsigned int day, int year );
    
    //  ....
    static bool  isLeapYear( int year );

private:
    //  All our private parts
    static const unsigned int       daysPerMonth[ ];
    static const unsigned int       daysPerMonthLeap[ ];
};

#endif // DAYCONVERSION_H
