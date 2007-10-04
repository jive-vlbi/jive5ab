// bookeeping of transfermodes + submodes

#ifndef EVLBI5A_TRANSFERMODE_H
#define EVLBI5A_TRANSFERMODE_H

#include <iostream>

// What are we doing?
// [Note: record is an alias for in2disk and play is an alias for disk2out...]
enum transfer_type {
    no_transfer, disk2net, in2net, net2out
};

// states a major transfer mode could be in. Which one(s) apply is
// entirely defined by the major mode itself... by making them single
// bits, we can OR them together
enum submode_flag {
    pause_flag = 0x1, run_flag = 0x2, wait_flag = 0x4, connected_flag = 0x8
};

struct transfer_submode {
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

// Show the major transfermode in human-readable format
std::ostream& operator<<(std::ostream& os, const transfer_type& tt);

// format the flags that are set within submode with textrepresentations
// as "<[flag,]*>"
// eg:
// if the run_flag and wait_flag are set it will show:
// <RUN, WAIT,>
std::ostream& operator<<(std::ostream& os, const transfer_submode& tsm );


#endif
