// whack all 'global' state variables into a struct
// such that we can pass this around
#ifndef EVLBI5A_RUNTIME_H
#define EVLBI5A_RUNTIME_H

// Own stuff
#include <playpointer.h>
#include <xlrdevice.h>
#include <ioboard.h>
#include <transfermode.h>
#include <countedpointer.h>
#include <bqueue.h>

// c++ stuff
#include <vector>
#include <algorithm>


// Collect together the network related parameters
// typically, net_protocol modifies these
extern const std::string  defProtocol;// = std::string("tcp");
extern const std::string  defUDPHelper;// = std::string("smart");

struct netparms_type {
    // Defaults, for easy readability

    // default inter-packet-delay (none)
    // [meant for links that don't get along with bursty traffik]
    static const unsigned int defIPD       = 0;
    // default MTU + how manu mtu's a datagram should span
    static const unsigned int defMTU       = 1500;
    static const unsigned int nMTU         = 1;
    // number of blocks + size of individual blocks
    static const unsigned int defNBlock    = 8;
    static const unsigned int defBlockSize = 128*1024;
    // OS socket rcv/snd bufsize
    static const unsigned int defSockbuf   = 4 * 1024 * 1024;

    // comes up with 'sensible' defaults
    netparms_type();

    // netprotocol values
    int                rcvbufsize;
    int                sndbufsize;
    std::string        udphelper;
    unsigned int       interpacketdelay;
    unsigned int       nblock;

    // setting properties. if one (or more) are
    // set, other properties may change
    // p.empty()==true => reset to default (defProtocol)
    void set_protocol( const std::string& p="" );
    // m==0 => reset to default (defMTU)
    void set_mtu( unsigned int m=0 );
    // bs==0 => reset to default (defBlockSize)
    void set_blocksize( unsigned int bs=0 );

    // Note: the following method is implemented but 
    // we're not convinced that nmtu/datagram > 1
    // is usefull. At least this allows us to
    // play around with it, if we feel like it.
    // Passing '0' => reset to default
    //void set_nmtu( unsigned int n ); 

    // and be able to read them back
    inline std::string  get_protocol( void ) const {
        return protocol;
    }
    inline unsigned int get_mtu( void ) const {
        return mtu;
    }
    inline unsigned int get_blocksize( void ) const {
        return blocksize;
    }
    unsigned int get_datagramsize( void ) const {
        return datagramsize;
    }

    private:
        // keep mtu and blocksize private.
        // this allows us to automagically
        // enforce the constraint-relation(s)
        // between the variables.
        //
        // Changing one(or more) may have an
        // effect on the others
        // [eg: changing the protocol
        //  changes the datagramsize, which
        //  affects the blocksize]
        // In order to compute the size of a datagram
        // the mtu is used:
        // it is assumed that only one datagram per MTU
        // is sent. Size starts off with MTU. Protocol
        // specific headersize is subtracted, then
        // internal protocol headersize is subtracted
        // and the remaining size is truncated to be
        // a multiple of 8.
        std::string        protocol;
        unsigned int       mtu;
        unsigned int       datagramsize;
        unsigned int       blocksize;

        // if we ever want to send datagrams larger than 1 MTU,
        // make this'un non-const and clobber it to liking
        const unsigned int nmtu;

        // when called it will (re)compute the privates
        // to match any desired constraints
        void constrain( void );

        // helper functions for "constrain()"
        void compute_datagramsize( void );
};

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
    
    
    // As per Mark5B-DIM-Registers.pdf, the bitstream mask
    // is a 32bit value. A 0 value (no bits set, that is)
    // is taken to be the "do not alter" value; configuring
    // zero tracks is .... well ... pretty useless ;)
    unsigned int  bitstreammask;

    // Clock frequency and decimation.
    // See ioboard.h for constraints and meaning
    // Actually, these registers are rly named like this ...
    unsigned int   k;
    unsigned int   j;

    // Which pulse-per-second?
    unsigned int   selpps;

    // select cg-clock (?) cg = clock-generator, so:
    // selcgclock==true? => use clockgenerator generated clock, VSI clock otherwise
    bool           selcgclk;

    unsigned short userword;

    bool           fpdp2;
    bool           startstop;

    unsigned int   hdr2;
    unsigned int   hdr3;

    unsigned int   tvrmask;
    bool           gocom;
};


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
    // create a default runtime.
    // for now it's the only c'tor we support so we have
    // a well defined initial state
    // check the .cc file for what the actual defaults are
    runtime();

    // start the threads. throws up if something fishy - eg
    // attempting to start whilst already running...
    // Why the throwing? Well, the exception contains the
    // reason *why* it failed. Just returning false doesn't
    // tell anyone a lot does it?
    // The threads will receive "this" as argument
    // The 'initstartval' will be assigned to the
    // "start" datamember so, if you want to, you
    // can kick the threads off immediately.
    // By default, the threads should go in a blocking
    // wait for either 'stop' or 'run' to become true..
    // (and maybe other conditions, but that's up to you).
    // Having the possibility to force 'stop' to an initial
    // true value would be quite silly wouldn't you agree?
    void start_threads( void* (*rdfn)(void*),
                        void* (*wrfn)(void*),
                        bool initstartval = false );

    // stop any running threads in a decent manner
    void stop_threads( void );

    // cleanup the runtime
    // - will stop running threads (if any)
    // - close ".fd" member if it's >= 0
    ~runtime();


    // the attributes of the runtime 

    // The global transfermode and submode/status
    transfer_type          transfermode;
    transfer_submode       transfersubmode;

    // The network-related parameters
    netparms_type          netparms;
   
    // the streamstor device to talk to
    xlrdevice              xlrdev;

    // and an ioboard
    ioboard_type           ioboard;

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

    // optionally threads may be executing and
    // they need these variables
    pthread_cond_t*        condition;
    pthread_mutex_t*       mutex;

    // as well as these variables...
    // basically, they determine what the threads
    // do. Before actually starting threads, make
    // sure you have filled in the appropriate field [which
    // those are, are totally dependant on which transfer 
    // you want to start - basically you're on your own :)]
    int                    fd;
    // if accepted fd >=0, then "main()" will add this fd 
    // to the poll() list and if an incoming connection is accepted,
    // the 'fd' datamember will be set to the accepted fd, the
    // acceptdfd will be closed, 'run' will be set to true and a
    // condition broadcast will be done to signal any waiting threads
    // that it's ok to go.
    int                    acceptfd; 
    bool                   repeat;

    bqueue                 queue;

    // For the 'skip' command: remember the last amount skipped
    long long              lastskip;

    // For dropping pakkits. If !0, every packet_drop_rate'th packet
    // will be dropped. So "packet_drop_rate==25" means: every 25th
    // packet/datagram will be dropped (4%). Typically only used
    // with UDP [but may be 'ported' to TCP as well]
    unsigned long long int packet_drop_rate;

    std::string            lasthost;
    playpointer            pp_start;
    playpointer            pp_end;
    playpointer            pp_current;
    volatile bool          run;
    volatile bool          stop;


    // for 'tstat?'
    // 'D' => disk, 'F' => fifo 'M' => memory '*' => nothing
    volatile devtype            tomem_dev;
    volatile devtype            frommem_dev;
    volatile unsigned long long nbyte_to_mem;
    volatile unsigned long long nbyte_from_mem;

    // evlbi stats. Currently only carries meaningful data when
    // udp is chosen as network transport
    evlbi_stats_type            evlbi_stats;


    private:
        // keep these private so outsiders cannot mess with *those*
        pthread_t*              rdid;
        pthread_t*              wrid;

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

        // This one shouldn't be copyable/assignable.
        // It should be passed by reference or by pointer
        runtime( const runtime& );
        const runtime& operator=( const runtime& );
};



#endif
