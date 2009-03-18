//          Implementation of DayConversion 
//
//  Author:  Harro Verkouter, 15-06-1999
//
//  $Id$
//
// Redone: January 2009
#include <dayconversion.h>


//  Note: month 0 is January!
const unsigned int DayConversion::secondsPerDay          = 24*60*60;
const unsigned int DayConversion::daysPerMonth[ 12 ]     = { 31, 28, 31, 30,
															 31, 30, 31, 31,
															 30, 31, 30, 31 };
const unsigned int DayConversion::daysPerMonthLeap[ 12 ] = { 31, 29, 31, 30,
															 31, 30, 31, 31,
															 30, 31, 30, 31 };

// year may be negative (BC!)
bool DayConversion::dayNrToMonthDay( unsigned int& month, unsigned int& day,
                                     unsigned int daynr, int year ) {
    const unsigned int*  daysPerMonthptr( DayConversion::daysPerMonth );
   
    // init default/error return values
    month = (unsigned int)-1;
    day   = (unsigned int)-1;

    // see what we can make of it
    if( DayConversion::isLeapYear(year) )
    	daysPerMonthptr = DayConversion::daysPerMonthLeap;
    
    for( unsigned int monthcnt=0, cumulativeday=0;
         cumulativeday<daynr && monthcnt<12;
    	 (cumulativeday+=daysPerMonthptr[monthcnt++]) ) {
    	if( (cumulativeday+daysPerMonthptr[monthcnt])>=daynr ) {
			month  = monthcnt;
			day    = (daynr-cumulativeday);
    	    break;
		}
	}
    return (month!=(unsigned int)-1);
}

// year could be negative...
bool DayConversion::dayMonthDayToNr( unsigned int& daynr, unsigned int month,
                                     unsigned int day, int year ) {
    unsigned int         monthcnt;
    const unsigned int*  daysPerMonthptr( DayConversion::daysPerMonth );
    
    if( DayConversion::isLeapYear(year) )
    	daysPerMonthptr = DayConversion::daysPerMonthLeap;
    
    for( monthcnt=0, daynr=day;
         monthcnt<month && monthcnt<12;
         daynr+=daysPerMonthptr[monthcnt++] );
   
    if( monthcnt>11 )
		daynr=(unsigned int)-1;
    
    return (daynr!=(unsigned int)-1);
}


bool DayConversion::isLeapYear( int year ) {
    //  Year is leap year if:
    //
    //  1) It's divisible by 4
    //  2) but not if it's divisible by 100 (centuries)
    //  3) but then again it is if it is divisible by 400
    return ( ((year%4)==0) && (!((year%100)==0) || ((year%400)==0)) );
}
