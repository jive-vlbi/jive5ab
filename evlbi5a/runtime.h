// whack all 'global' state variables into a struct
// such that we can pass this around
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
#ifndef EVLBI5A_RUNTIME_H
#define EVLBI5A_RUNTIME_H

// Own stuff
#include <playpointer.h>
#include <xlrdevice.h>
#include <ioboard.h>
#include <transfermode.h>
#include <countedpointer.h>
#include <bqueue.h>
#include <userdir.h>
#include <rotzooi.h>
#include <chain.h>
#include <trackmask.h>
#include <netparms.h>
#include <constraints.h>
#include <chainstats.h>

// c++ stuff
#include <vector>
#include <algorithm>

// for mutex
#include <pthread.h>


// tie evlbi transfer statistics together
struct evlbi_stats_type {
    unsigned long long int      pkt_total;
    unsigned long long int      pkt_lost;
    unsigned long long int      pkt_ooo;
    unsigned long long int      pkt_rpt;

    evlbi_stats_type();
};
std::ostream& operator<<(std::ostream& os, const evlbi_stats_type& es);


// Uniquely link codes -> number of tracks
struct codemapentry {
    unsigned short   code;
    int              numtracks;

    codemapentry( unsigned short c, int ntrk ):
        code( c ), numtracks( ntrk )
    {}
};
// link CODE to NTRACK
const codemapentry   codemaps[] = {codemapentry(0, 32), codemapentry(4, 32),
                                   codemapentry(1, 64), codemapentry(2, 16),
                                   codemapentry(3, 8), codemapentry(8, 32) };
// And stick the entries in a std::vector so we can use STL goodies on it!
typedef std::vector<codemapentry>  codemap_type;
const codemap_type   codemap = codemap_type( &codemaps[0],
                                             &codemaps[(sizeof(codemaps)/sizeof(codemaps[0]))+1] );

// helper for finding an entry based on code
struct codefinder {
    codefinder( unsigned short c ):
        code2look4( c )
    {}

    bool operator()( const codemapentry& cme ) const {
        return cme.code==code2look4;
    }
    unsigned short code2look4;
};
// helper for finding an entry based on ntrack
struct ntrkfinder {
    ntrkfinder( int nt ):
        ntrk2look4( nt )
    {}

    bool operator()( const codemapentry& cme ) const {
        return cme.numtracks==ntrk2look4;
    }
    int ntrk2look4;
};

#define code2ntrack(a, b) \
    std::find_if(a.begin(), a.end(), codefinder(b))
#define ntrack2code(a, b) \
    std::find_if(a.begin(), a.end(), ntrkfinder(b))


// Inputboard mode setting
// hmmmm ... really, this should be in ioboard.h or somewhere
struct inputmode_type {
    enum setup_type {
        empty, mark5adefault
    };
    inputmode_type( setup_type setup = mark5adefault );

    std::string mode;
    int         ntracks;
    bool        notclock;
    char        errorbits; 
};
std::ostream& operator<<(std::ostream& os, const inputmode_type& ipm);

// a mark5b input mode is sufficiently different from Mk5A ...
// to warrant its own type
struct mk5b_inputmode_type {
    enum setup_type {
        empty, mark5bdefault
    };

    // same as with the mark5a inputmode, we have a choice
    // between empty and mk5b default
    mk5b_inputmode_type( setup_type setup = mark5bdefault );

    // Mark5B specific inputmode stuff

    // The datasource for the DIM. Can be 'ext' [take data from VSI 80-pin connector],
    // 'tvg' [use internal test-vector-generator or 'ramp', internal incrementing datapattern.
    // This is not to/from the hardware but just for human readable status
    std::string   datasource;
    // The on-board clock-generator frequency [it cannot be read back from the H/W so
    // we save it in here]. < some magic value (.03?) means: leave untouched.
    // It may NOT be >40MHz.
    // Implicit unit of this value is MHz.
    double        clockfreq;

    // -1 implies: do not use/overwrite/interpret [when setting
    // the inputmode]
    // cf Parse5A.c:
    // TVG status: ('5' being default)
    // 0 = external data, not TVG; 
    // 1 = internal TVG; 
    // 2 = expect external TVG 
    // 3 = external data, not TVG, AMAZON FPDP II 
    // 4 = internal TVG, AMAZON FPDP II 
    // 5 = expect external TVG, AMAZON FPDP II 
    // 7 = internal "ramp", 32-bit incrementing pattern 
    // 8 = internal "ramp", AMAZON FPDP II */ 
    int           tvg; // TVG status
    
    // As per Mark5B-DIM-Registers.pdf, the bitstream mask
    // is a 32bit value. A 0 value (no bits set, that is)
    // is taken to be the "do not alter" value; configuring
    // zero tracks is .... well ... pretty useless ;)
    unsigned int  bitstreammask;

    // Clock frequency and decimation.
    // See ioboard.h for constraints and meaning
    // Actually, these registers are rly named like this ...
    // values <0 indicate: "do not alter"
    int            k;
    int            j;

    // Which pulse-per-second to sync to? 0->3 are defined,
    // <0 implies: "do not alter"
    int            selpps;

    // select cg-clock (?) cg = clock-generator, so:
    // selcgclock==true? => use clockgenerator generated clock, VSI clock otherwise
    bool           selcgclk;

    unsigned short userword;

    bool           fpdp2;
    bool           startstop;

    // hdr2 and hdr3 come from read-only registers so
    // setting/modifying them (when setting an inputmode)
    // has no effect
    unsigned int   hdr2;
    unsigned int   hdr3;

    unsigned int   tvrmask;
    bool           gocom;
    bool           seldim;
    bool           seldot;
    bool           erf; // read-only [ERF => "Error Found By TVR"]
    bool           tvgsel;
};

// Show it 'nicely' formatted on a std::ostream
std::ostream& operator<<( std::ostream& os, const mk5b_inputmode_type& ipm );


// Outputboard mode settings
// Note: the clockfreq cannot be read back from the H/W
//       so that gets set to a default of 8 (MHz)
struct outputmode_type {
    // the constructor may be used to create an empty
    // or a default outputmode (ie not empty but
    // with the defaults that Mark5A comes up with)
    enum setup_type {
        empty, mark5adefault
    };
    // by default it comes up with a non-empty, Mark5A default
    // outputmode ("straight-through/32tracks/mark4")
    outputmode_type( setup_type setup = mark5adefault );

    std::string mode; // vlbi, st, tvr or ?
    double      freq; 
    bool        active; /* TRUE or FALSE */
    bool        synced; /* TRUE or FALSE */ 
    int         tracka; /* Decoder channel A */
    int         trackb; /* Decoder channel B */
    int         ntracks; /* VLBI track mode, 8, 16, 32, 64, or 0 if unknown */ 
    int         numresyncs; /* Number of re-syncs */ 
    bool        throttle; /* Throttled (recording suspend flag), TRUE of FALSE */ 
    std::string format; /* mark4, vlba, tvr, or "" if unknown */
};
std::ostream& operator<<(std::ostream& os, const outputmode_type& opm);


// enumerate 'device types' to indicate direction
// of transfers, eg FIFO, disk, memory
// Set by thread functions in the runtime.
// the 'tstat_fn' reads these out
enum devtype {
    dev_none, dev_network, dev_disk, dev_fifo
};
std::ostream& operator<<(std::ostream& os, devtype dt);


struct runtime {
    static const unsigned int invalid_taskid = (unsigned int)-1;
    // create a default runtime.
    // for now it's the only c'tor we support so we have
    // a well defined initial state
    // check the .cc file for what the actual defaults are
    runtime();

    // shared access between multiple threads.
    // please grab/release lock. Use the scoped lock to make it automatic.
    void lock( void );
    void unlock( void );

    // cleanup the runtime
    // - will stop running threads (if any)
    ~runtime();


    // the attributes of the runtime 

    // The actual processing chain. We make it a public variable; it is
    // up to the program to (1) make sure you're doing the right thing
    // and (2) it makes it obvious where the object is [number of layers
    // of abstraction = 0] and (3) you have full control over anything
    // the chain offers. Use it well.
    chain                  processingchain;

    // The global transfermode and submode/status
    transfer_type          transfermode;
    transfer_submode       transfersubmode;

    // The network-related parameters
    netparms_type          netparms;
   
    // the streamstor device to talk to
    xlrdevice              xlrdev;

    // and an ioboard
    ioboard_type           ioboard;

    // If someone set a trackmask [ie dropping/retaining a selection of the
    // available bitstreams] then this solution is the series of steps to
    // compress the data. You can generate code from this solution to both
    // compress/decompress a block of data. If the solution is "true" then
    // all *2net transfers will send the inputdata through a compressor and
    // all net2* transfers will decompress the received data
    solution_type          solution;

    // when doing transfers over the network use the sizes in this object.
    // they are based on the values set in the netparms and/or trackmask(aka
    // compression) and are set to meet constraints for efficient
    // datatransfers - eg that a packet divides into a read which divides
    // into a block which, optionally, divides into a tape/diskframe [that
    // last one may also be reversed: a block could, possibly, hold an
    // integer amount of frames]
    constraintset_type     sizes;

    // A step may keep statistics in here. the tstat command
    // uses these values.
    chainstats_type        statistics;

    // if you request these, they
    // will be filled with current values from the h/w
    // first so they're always up-to-date

    // Interface change. overload set() and get()
    // for different types of modes (most notably
    // mark5a or mark5b)
    // Setting or getting the one which is not compatible
    // with the hardware typically results in xceptions bein'
    // thrown. just so you know.

    // retrieve current inputMode
    void                   get_input( inputmode_type& ipm ) const;
    void                   get_input( mk5b_inputmode_type& ipm ) const;
    // output
    void                   get_output( outputmode_type& opm ) const;
    //void                   get_output( mk5b_outputmode_type& ipm );

    void                   reset_ioboard( void ) const;

    // and set the mode(s)
    void                   set_input( const inputmode_type& ipm );
    void                   set_input( const mk5b_inputmode_type& ipm );
    void                   set_output( const outputmode_type& opm );

    // We must be able to find out how many tracks are active
    // This works both on Mark5A and Mark5B/DIM.
    // (no Mark5B/DOM support (yet, not even planned))
    unsigned int           ntrack( void ) const;
    // the ... trackbitrate (d'oh) in bits/s
    double                 trackbitrate( void ) const;

    playpointer            pp_current;

    // for 'tstat?'
    // 'D' => disk, 'F' => fifo 'M' => memory '*' => nothing
    volatile devtype            tomem_dev;
    volatile devtype            frommem_dev;
    volatile unsigned long long nbyte_to_mem;
    volatile unsigned long long nbyte_from_mem;

    // evlbi stats. Currently only carries meaningful data when
    // udp is chosen as network transport
    evlbi_stats_type            evlbi_stats;

    // keep a mapping of jobid => rot-to-systemtime mapping
    // taskid == -1 => invalid/unknown taskid
    unsigned int                current_taskid;
    task2rotmap_type            task2rotmap;


    private:
        // keep these private so outsiders cannot mess with *those*

        // The mutex for locking
        pthread_mutex_t             rte_mutex;

        // Oef! This is a real Kludge (tm).
        // The I/O modes for Mk5A and Mk5B are so different
        // that I didn't want to mix them both into the
        // same datastructures.
        // And didn't want to set up fancy stuff with
        // inheritance and/or other stuff ... I ruled that
        // we keep both I/O modes in here.
        // Access will be verified, that is, if you try to
        // read/write a config incompatible with the detected
        // h/w you'll get an exception thrown.
        //
        // outputmode is read-only to the outside world
        // ppl may request to set mode/playrate so we can do
        // that in a controlled manner
        mutable inputmode_type       mk5a_inputmode;
        mutable outputmode_type      mk5a_outputmode;

        mutable mk5b_inputmode_type  mk5b_inputmode;
        //mutable mk5b_outputmode_type mk5b_outputmode;
        //

        // Cache the number of active tracks.
        // It will be set when an input-mode is set
        // so it will automagically be hardware-agnostic [Mk5A & B]
        unsigned int                 n_trk;
        // Id. for track bitrate. Is set when play_rate/clock_set
        // are called.
        double                       trk_bitrate;

        // This one shouldn't be copyable/assignable.
        // It should be passed by reference or by pointer
        runtime( const runtime& );
        const runtime& operator=( const runtime& );
};

struct scopedrtelock {
    public:
        scopedrtelock(runtime& rte);
        ~scopedrtelock();
    private:
        // nice. a reference as datamember. since the lifetime of this
        // object is small and its undefaultcreatable/copyable/assignable
        // that's ... doable.
        runtime&   rteref;

        // no default c'tor
        scopedrtelock();
        // nor copy
        scopedrtelock(const scopedrtelock&);
        // nor assignment
        const scopedrtelock& operator=(const scopedrtelock&);
};

#define RTEEXEC(r, f) \
    { scopedrtelock  sC0p3dL0KkshZh(r); \
      f;\
    }

#endif
