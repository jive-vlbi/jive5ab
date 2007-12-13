// bookeeping of transfermodes + submodes

#ifndef EVLBI5A_TRANSFERMODE_H
#define EVLBI5A_TRANSFERMODE_H

#include <iostream>
#include <map>
#include <string>
#include <exception>


struct tmexception:
    public std::exception
{
    tmexception( const std::string& m);
    virtual const char* what( void )const throw();
    virtual ~tmexception() throw();

    const std::string __m;
};


// What are we doing?
// [Note: record is an alias for in2disk and play is an alias for disk2out...]
enum transfer_type {
    no_transfer, disk2net, in2net, net2out
};

// states a major transfer mode could be in. Which one(s) apply is
// entirely defined by the major mode itself...
// People should *never* assume that the enum corresponds to a specific value
enum submode_flag {
    pause_flag = 56, run_flag = 109, wait_flag = 42, connected_flag = 271
};


// bind an unsigned int (taken the be the actual flag value)
// and a string, the human-readable form/name of the flag
// together
struct flagtype {
    flagtype(unsigned int f, const std::string& name);

    const unsigned int __f;
    const std::string  __nm;
};

struct transfer_submode {
    // by mapping enum => flag we can ensure that 
    // no unknown flags get set. If the flag is not in
    // the map, it cannot be set/cleared
    typedef std::map<submode_flag, flagtype> flagmap_type;

    // default: no flags set (what a surprise)
    transfer_submode();

    // set a flag
    transfer_submode& operator|=(submode_flag f);

    // for balancing with "clr()" [it is functionally equal to "*this|=f"]
    transfer_submode& set( submode_flag f );

    // Clear a flag
    transfer_submode& clr( submode_flag f );

    // ...
    void clr_all( void );

    // check if a flag is set
    bool operator&( submode_flag f ) const;

    // Return a new object which is the bitwise or of
    // this + the new flag
    transfer_submode operator|(submode_flag f) const;

    private:
        unsigned int   flgs;

};
// get ro access to the defined flags
const transfer_submode::flagmap_type& get_flagmap( void );

// Show the major transfermode in human-readable format
std::ostream& operator<<(std::ostream& os, const transfer_type& tt);

// format the flags that are set within submode with textrepresentations
// as "<[flag,]*>"
// eg:
// if the run_flag and wait_flag are set it will show:
// <RUN, WAIT,>
std::ostream& operator<<(std::ostream& os, const transfer_submode& tsm );


#endif
