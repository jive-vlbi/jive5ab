// available thread-functions
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
#ifndef JIVE5A_THREADFNS_H
#define JIVE5A_THREADFNS_H

#include <map>
#include <string>
#include <runtime.h>
#include <chain.h>
#include <block.h>
#include <blockpool.h>
#include <headersearch.h>
#include <trackmask.h>
#include <constraints.h>
#include <circular_buffer.h>

#include <stdint.h> // for [u]int<N>_t  types

// usually the name of the threadfunctions is enough
// info as to what it (supposedly) does.

// threadargument for the delayed_play_fn
struct dplay_args {
    double      rot;
    runtime*    rteptr;
    playpointer pp_start;

    dplay_args();
};
void* delayed_play_fn( void* dplay_args_ptr );


// Prototypes for the available producers, steps and consumers


// Available producers. Typically they produce "block"s of raw data on
// their output queue
struct fillpatargs;
struct fdreaderargs;
struct fiforeaderargs;
struct diskreaderargs;
struct framerargs;
struct compressorargs;
struct networkargs;
struct reorderargs;
struct buffererargs;
struct fakerargs;

// A dataframe
struct frame {
    format_type     frametype;
    unsigned int    ntrack;
    struct timespec frametime;
    block           framedata;

    frame();
    frame(format_type tp, unsigned int n, block data);
    frame(format_type tp, unsigned int n, struct timespec ft, block data);
};

template <typename T>
struct tagged {
    T            item;
    unsigned int tag;

    tagged():
        tag( (unsigned int)-1 )
    {}
    tagged(unsigned int t, const T& i):
        item(i), tag(t)
    {}
};


// Possible datasources
void fillpatterngenerator(outq_type<block>*, sync_type<fillpatargs>*);
void framepatterngenerator(outq_type<block>*, sync_type<fillpatargs>*);
void fillpatternwrapper(outq_type<block>*, sync_type<fillpatargs>*);

void fiforeader(outq_type<block>*, sync_type<fiforeaderargs>* );
void diskreader(outq_type<block>*, sync_type<diskreaderargs>* );
void fdreader(outq_type<block>*, sync_type<fdreaderargs>* );
void netreader(outq_type<block>*, sync_type<fdreaderargs>*);
#if 0
void udps_pktreader(outq_type<block>*, sync_type<fdreaderargs>*);
#endif

// steps

// takes in raw data and outputs complete dataframes as per
//  * mark4/vlba format as taken from Mark4 memo 230, revision 1.21, 10 June 2005
//      "Mark IIIA/IV/VLBA Tape Formats, Recording Modes and Compatibility"
//  * mark5b format as described in "Mark 5B User's manual", 8 August 2006 
//      (http://www.haystack.mit.edu/tech/vlbi/mark5/docs/Mark%205B%20users%20manual.pdf)
void framer(inq_type<block>*, outq_type<frame>*, sync_type<framerargs>*);

// inputs dataframes and outputs compressed blocks
void framecompressor(inq_type<frame>*, outq_type<block>*, sync_type<compressorargs>*);

// At the moment just decodes (discards) the timecode from the frame
void timedecoder(inq_type<frame>*, outq_type<frame>*, sync_type<headersearch_type>*);

// inputs full dataframes and outputs only the binary frame
//    you lose knowledge of the actual type of frame
void frame2block(inq_type<frame>*, outq_type<block>*);

// these two do "blind" compression and decompression
void blockcompressor(inq_type<block>*, outq_type<block>*, sync_type<runtime*>*);
void blockdecompressor(inq_type<block>*, outq_type<block>*, sync_type<runtime*>*);

void faker(inq_type<block>*, outq_type<block>*, sync_type<fakerargs>*);

// will simply keep a number of bytes buffered (after it has filled them)
void bufferer(inq_type<block>*, outq_type<block>*, sync_type<buffererargs>*);

#if 0
// Takes in UDPs packets (first 8 bytes == 64bit sequencenumber),
// outputs blocks of size constraints[blocksize]
void udpspacket_reorderer(inq_type<block>*, outq_type<block>*, sync_type<reorderargs>*);
#endif

// The consumers
void fifowriter(inq_type<block>*, sync_type<runtime*>*);
void diskwriter(inq_type<block>*);

// write to the network. will choose appropriate "real" writer based on
// actual networkprotocol
void netwriter(inq_type<block>*, sync_type<fdreaderargs>*);

// Prepends a sequencenumber and writes each block to the filedescriptor 
//void vtpwriter(inq_type<block>*, sync_type<fdreaderargs>*);

// generic write to filedescriptor. does NOT look at sizes, writes whatever
// it receives on the filedescriptor.
void fdwriter(inq_type<block>*, sync_type<fdreaderargs>*);
void sfxcwriter(inq_type<block>*, sync_type<fdreaderargs>*);

// a checker. checks received data against expected pattern
// (use 'fill2net' + 'net2check')
void checker(inq_type<block>*, sync_type<fillpatargs>*);

// Just print out the timestamps of all frames that wizz by
void timeprinter(inq_type<frame>*, sync_type<headersearch_type>*);

// Name says it all, really
void bitbucket(inq_type<block>*);

// information for the framer - it must know which
// kind of frames to look for ... 
struct framerargs {
    runtime*           rteptr;
    blockpool_type*    pool;
    headersearch_type  hdr;

    framerargs(headersearch_type h, runtime* rte);
    ~framerargs();
};

struct fillpatargs {
    bool                   run;
    uint64_t               fill;
    uint64_t               inc;
    runtime*               rteptr;
    uint64_t               nword;
    blockpool_type*        pool;

    // seems silly to do it like this but making it a memberfunction makes
    // it eligible for using with chain::communicate(), ie we can be sure
    // that the memberfunction gets executed in a critical section so it's
    // MT safe to use it LIKE THAT!
    // If you do NOT use chain::communicate() you're on your own. Not that
    // it would be possible since from outside the chain you have no idea
    // where the heck this actual object lives. Only the thread and the
    // chain know. You don't. 
    void set_run(bool newval);

    // set amount of words(!) of fillpattern to generate.
    // one word of fillpattern == 8 bytes; the StreamStor
    // dictated minimum-transferrable-amount/every transfer
    // must be a multiple of 8 bytes.
    // default/-1 implies (2^64 - 1) words of pure unadulterated
    // fillpattern. Which I hardly need to remind you is an
    // awfull lot ...
    void set_nword(uint64_t n);

    // defaults: run==false, rteptr==0, buffer==0,
    //           nbyte==-1 (several petabytes generated)
    //           fill == 0x1122334411223344
    //           inc  == 0
    fillpatargs();
    // almost same as default, save for rteptr, wich will be == r
    fillpatargs(runtime* r);

    // calls delete on buffer.
    ~fillpatargs();
};

struct fakerargs {
    runtime*        rteptr;
    unsigned char   header[20];
    unsigned char*  buffer;
    size_t          size;
    blockpool_type* framepool;

    void init_mk4_frame();
    void init_mk5b_frame();
    void update_mk4_frame(time_t);
    void update_mk5b_frame(time_t);
    void init_frame();
    void update_frame(time_t);

    fakerargs();
    fakerargs(runtime* rte);

    // calls delete [] on buffer.
    ~fakerargs();
};

// settings for the compressor
// start with the expected blocksize and the (de)compression solution
struct compressorargs {
    runtime*   rteptr;

    compressorargs();
    compressorargs(runtime* p);
};

// The fiforeader 
struct fiforeaderargs {
    bool            run;       // the reader blocks until this one becomes "true" 
    runtime*        rteptr;    // will update statistics in this one
    blockpool_type* pool;      // block allocator

    void set_run( bool newrunval );

    // defaults: run==false, rteptr==0, buffer==0
    fiforeaderargs();
    fiforeaderargs(runtime* r);
    ~fiforeaderargs();
};

struct diskreaderargs {
    bool               run;      // the reader blocks until this one becomes "true" 
    bool               repeat;   // ...
    runtime*           rteptr;   // will update statistics in this one
    playpointer        pp_start, pp_end; // range to play back. both==0 => whole disk
    blockpool_type*    pool;     // block allocator

    void set_start( playpointer s );
    void set_end( playpointer e );
    void set_run( bool b );
    void set_repeat( bool b );

    // run==false, repeat==false, buffer==0, runtime==0
    diskreaderargs();
    // run==false, repeat==false, buffer==0, runtime==r
    diskreaderargs(runtime* r);
    ~diskreaderargs();
};

struct reorderargs {
    bool*          dgflag;
    bool*          first;
    runtime*       rteptr;
    unsigned char* buffer;

    reorderargs();
    reorderargs(runtime* r);
    ~reorderargs();
};

struct networkargs {
    runtime*           rteptr;

    networkargs();
    networkargs(runtime* r);
};

struct fdreaderargs {
    int             fd;
    bool            doaccept;
    runtime*        rteptr;
    pthread_t*      threadid;
    unsigned int    blocksize;
    blockpool_type* pool;

    fdreaderargs();
    ~fdreaderargs();
};


struct buffererargs {
    runtime*         rte;
    unsigned int     bytestobuffer;

    buffererargs();
    buffererargs(runtime* rteptr, unsigned int n);

    unsigned int get_bufsize( void );

    ~buffererargs();
};

struct splitterargs {
    runtime*           rte;
    blockpool_type*    pool;
    const unsigned int nchunk;
    const unsigned int multiplier;

    // rte==0 && buffer==0 && nchunk==0
    splitterargs();

    // rte==rteptr && buffer==0
    splitterargs(runtime* rteptr, unsigned int n, unsigned int m=0);

    // deletes buffer (not rte)
    ~splitterargs();
};

// Deal with >1 destination
//    * initializer taking a mapping of
//      chunk -> destination; it opens
//      all unique destinations
//      returns a new multifdargs thingy
//    * the multiwriter consumer will
//      pop tagged blocks from its input queue
//      and send each block to its accompanying
//      destination
typedef std::map<unsigned int, int> dest_fd_map_type;

// tag -> destination (string) mapping 
typedef std::map<unsigned int, std::string> chunkdestmap_type;

struct multidestparms {
    runtime*          rteptr;
    chunkdestmap_type chunkdestmap;

    multidestparms(runtime* rte, const chunkdestmap_type& cdm);
};

// Reserve room for all the fdreaderargs in multifdargs.
// This allows the multicloser() to close all the
// filedescriptors and inform all the threads.
typedef std::list<fdreaderargs*> fdreaderlist_type;

struct multifdargs {
    runtime*          rteptr;
    // tag -> filedescriptor mapping
    dest_fd_map_type  dstfdmap;
    // all readers associated with these multiple
    // destinatations (one for each filedescriptor)
    // We reuse the fdreaderargs type (the knowledge
    // is already internal in the multinetwriter)
    // but by duplicating this bit of info we 
    // can easily re-use the ::close_filedescriptor()
    // function
    fdreaderlist_type fdreaders;

    multifdargs( runtime* rte );
};

// The reframer breaks stuff up into chunks no larger than
// chunk_size. Note that the actual payload carried in
// chunk_size may be smaller as it may include
// reframed header data.
// So, e.g. when reframing to VDIF, the (data)payload
// carried in the output is at most
// 'chunk_size - sizeof(vdif_header)' bytes
//
struct reframe_args {
    const uint16_t      station_id;
    blockpool_type*     pool;
    const unsigned int  bitrate;
    const unsigned int  input_size;
    const unsigned int  output_size;

    reframe_args(uint16_t sid, unsigned int br, unsigned int ip, unsigned int op);
    ~reframe_args();
};

multifdargs*   multiopener( multidestparms mdp );
multifdargs*   multifileopener( multidestparms mdp );
void           multicloser( multifdargs* );

void           tagger( inq_type<frame>*, outq_type<tagged<frame> >*, sync_type<unsigned int>* );
void           splitter( inq_type<frame>*, outq_type<tagged<frame> >*, sync_type<splitterargs>* );
void           coalescing_splitter( inq_type<tagged<frame> >*, outq_type<tagged<frame> >*, sync_type<splitterargs>* );
void           reframe_to_vdif(inq_type<tagged<frame> >*, outq_type<tagged<block> >*, sync_type<reframe_args>* );
void           multinetwriter( inq_type<tagged<block> >*, sync_type<multifdargs>* );
void           multifilewriter( inq_type<tagged<block> >*, sync_type<multifdargs>* );

// helperfunctions 

// these functions set up a network server or client based on the
// information in networkargs. They return a "new fdreaderargs()"
// with the filedescriptorfield filled in.
// threadfunctions that take a "sync_type<fdreaderargs>*" as second 
// parameter can make use of these, in combination with the chain framework:
//     stepid = chain.add(&datasink, &net_server, networkargsinstance); 
//                 or
//     stepid = chain.add(&datasource, &net_client, networkargsinstance); 
// when the chain those threadfunctions are in is run,
// the net_server/net_client routines will set up the network connection
// and return an "fdreaderargs*" with the freshly created networkconnection
// filled in. the "&datasource" and "&datasink" functions can then
// use that filedescriptor at will.
// If they fill in the "thread_id" pthread_t pointer and install an empty
// signalhandler for SIGUSR1, you may register
// the "close_network" routine for the step "stepid". this function
// will close the networksocket, set it to "-1" and inform the thread via a SIGUSR1.
// this allows the "&datasource" and "&datasink" functions to see wether they
// need to stop
fdreaderargs* net_server(networkargs net);
fdreaderargs* net_client(networkargs net);
fdreaderargs* open_file(std::string fnam, runtime* r = 0);
fdreaderargs* open_socket(std::string fnam, runtime* r = 0);

void close_filedescriptor(fdreaderargs*);


void install_zig_for_this_thread(int);

#endif
