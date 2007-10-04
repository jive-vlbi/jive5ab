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

// c++ stuff
#include <vector>
#include <algorithm>


// Collect together the network related parameters
// typically, net_protocol modifies these
struct netparms_type {
    // comes up with 'sensible' defaults
    netparms_type();

    // netprotocol values
    int                rcvbufsize;
    int                sndbufsize;
    std::string        protocol;
    unsigned int       mtu;
    unsigned int       nblock;
    unsigned int       blocksize;
    // if we ever want to send datagrams larger than 1 MTU,
    // make this'un non-const and clobber it to liking
    const unsigned int nmtu;

    // In order to compute the size of a datagram
    // the mtu is used:
    // it is assumed that only one datagram per MTU
    // is sent. Size starts off with MTU. Protocol
    // specific headersize is subtracted, then
    // internal protocol headersize is subtracted
    // and the remaining size is truncated to be
    // a multiple of 8.
    unsigned int get_datagramsize( void ) const;
};


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

// interthread buffer settings
// net_protocol may modify those as well
//
// a 'buffer' should point at 'nblock' blocks of
// size 'blocksize' and each block should
// have a size which is an integral multiple
// of 'datagramsize' and 'datagramsize' should
// be an integral multiple of 8.
struct buffer_type {
    // no buffer at all.
    buffer_type();

    // create a buffer based upon network params
    // and (default) blocksize/number of blocks.
    // The blocksize will be adjusted to contain
    // an integral number of datagrams.
    buffer_type( const netparms_type& np );

    unsigned int        blocksize( void ) const;
    unsigned int        nblock( void ) const;
    unsigned int        datagramsize( void ) const;
    unsigned long long* buffer( void ) const;

    // release memory if no-one is referencing it anymore
    ~buffer_type();

    private:
        struct buf_impl {
            buf_impl();
            buf_impl( const netparms_type& np );
            ~buf_impl();

            unsigned int        blockSize;
            unsigned int        nBlock;
            unsigned int        datagramSize;
            unsigned long long* ullPointer;
        };
        countedpointer<buf_impl> mybuffer;
};


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
    void start_threads( void* (*rdfn)(void*), void* (*wrfn)(void*) );

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
    const inputmode_type&  inputMode( void ) const;
    const outputmode_type& outputMode( void ) const;

    void                   reset_ioboard( void ) const;

    // and set the mode(s)
    void                   inputMode( const inputmode_type& ipm );
    void                   outputMode( const outputmode_type& opm );

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
    int                   fd;
    // if accepted fd >=0, this fd will be added to the poll() list
    // and if an incoming connection is accepted, the 
    // 'fd' will be set to the accepted fd and 
    // the 'run' will be set to true and a condition broadcast
    // will be done.
    int                   acceptfd; 
    bool                  repeat;
    std::string           lasthost;
    playpointer           pp_start;
    playpointer           pp_end;
    playpointer           pp_current;
    buffer_type           buffer;
    volatile bool         run;
    volatile bool         stop;
    volatile unsigned int n_empty;
    volatile unsigned int n_full;

    // for 'tstat?'
    // 'D' => disk, 'F' => fifo 'M' => memory '*' => nothing
    volatile devtype            tomem_dev;
    volatile devtype            frommem_dev;
    volatile unsigned long long nbyte_to_mem;
    volatile unsigned long long nbyte_from_mem;

    private:
        // keep these private so outsiders cannot mess with *those*
        pthread_t*              rdid;
        pthread_t*              wrid;

        // outputmode is read-only to the outside world
        // ppl may request to set mode/playrate so we can do
        // that in a controlled manner
        mutable inputmode_type  inputmode;
        mutable outputmode_type outputmode;

        // This one shouldn't be copyable/assignable.
        // It should be passed by reference or by pointer
        runtime( const runtime& );
        const runtime& operator=( const runtime& );
};



#endif
