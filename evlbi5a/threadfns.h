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
#include <splitstuff.h>
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

template <unsigned int N>
struct emergency_type {
    enum     { nrElements = N };
    READTYPE buf[N];
};

// Possible datasources
void fillpatterngenerator(outq_type<block>*, sync_type<fillpatargs>*);
void framepatterngenerator(outq_type<block>*, sync_type<fillpatargs>*);
void fillpatternwrapper(outq_type<block>*, sync_type<fillpatargs>*);

void fiforeader(outq_type<block>*, sync_type<fiforeaderargs>* );
void diskreader(outq_type<block>*, sync_type<diskreaderargs>* );
void fdreader(outq_type<block>*, sync_type<fdreaderargs>* );
void netreader(outq_type<block>*, sync_type<fdreaderargs>*);

// steps

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

// The consumers
void fifowriter(inq_type<block>*, sync_type<runtime*>*);
void diskwriter(inq_type<block>*);

// generic write to filedescriptor. does NOT look at sizes, writes whatever
// it receives on the filedescriptor.
//void fdwriter(inq_type<block>*, sync_type<fdreaderargs>*);
void sfxcwriter(inq_type<block>*, sync_type<fdreaderargs>*);

// a checker. checks received data against expected pattern
// (use 'fill2net' + 'net2check')
void checker(inq_type<block>*, sync_type<fillpatargs>*);

// Just print out the timestamps of all frames that wizz by
void timeprinter(inq_type<frame>*, sync_type<headersearch_type>*);
void timechecker(inq_type<frame>*, sync_type<headersearch_type>*);


// information for the framer - it must know which
// kind of frames to look for ... 
struct framerargs {
    bool               strict;
    runtime*           rteptr;
    blockpool_type*    pool;
    headersearch_type  hdr;

    framerargs(headersearch_type h, runtime* rte, bool s=false);

    // Make this a method so it can be called via
    // chain::communicate() in order to change the
    // strictness at runtime
    void set_strict(bool b);

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

    // Set fill pattern and increment
    void set_fill(uint64_t f);
    void set_inc(uint64_t i);

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
    netparms_type      netparms;

    networkargs();
    networkargs(runtime* r);
    networkargs(runtime* r, const netparms_type& np);
};

struct fdreaderargs {
    int             fd;
    bool            doaccept;
    runtime*        rteptr;
    pthread_t*      threadid;
    unsigned int    blocksize;
    netparms_type   netparms;
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

// 'fname' will be used to look up the actual
// splitfunction (+properties), see splitstuff.h
// the tag-chunk will be called with the incoming
// tag and the current chunk number and should return
// a new tag for that chunk.
//
// Consider the following situation:
// split in two steps:
//  * 16bitby2, which will yield blocks
//    with tags 0 and 1
//  * 8bitby4, which will split each incoming
//    block into 4 parts. If we would disregard
//    the tag of the incoming block (== 0|1) we
//    would end up with only 4 tags: 0..3
//    But probably you want:
//    original block 0 -> split into 4 chunks 
//      -> new tags 0, 1, 2, 3
//    original block 1 -> split into 4 chunks
//      -> new tags 4, 5, 6, 7
//
//   so tag-chunk(0, 0) -> 0, (0,1) -> 1, (1,0) -> 4
//      (1,1) -> 5 etc
//
struct splitterargs {
    runtime*             rte;
    blockpool_type*      pool;
    headersearch_type    inputhdr;
    headersearch_type    outputhdr;
    splitproperties_type splitprops;

    // The splitter needs to know the splittingroutine
    // and the input-dataformat [described by 'inhdr'].
    // It will work out what it outputs [stored in the
    // 'outputhdr' member]
    splitterargs(runtime* rteptr,
                 const splitproperties_type& sp,
                 const headersearch_type& inhdr,
                 unsigned int naccumulate = splitproperties_type::natural_accumulation);

    // deletes blockpool (not rte)
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
    netparms_type     netparms;
    chunkdestmap_type chunkdestmap;

    // copy network parms out of rte
    multidestparms(runtime* rte, const chunkdestmap_type& cdm);
    // use the passed networkargs i.s.o. those from rte
    multidestparms(runtime* rte, const chunkdestmap_type& cdm, const netparms_type& np);
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
    ~multifdargs();
};

// a tag remapper
typedef std::map<unsigned int, unsigned int> tagremapper_type;

// The reframer breaks stuff up into chunks no larger than
// chunk_size. Note that the actual payload carried in
// chunk_size may be smaller as it may include
// reframed header data.
// So, e.g. when reframing to VDIF, the (data)payload
// carried in the output is at most
// 'chunk_size - sizeof(vdif_header)' bytes
//
// The tagremapper is there to accomodate assigning
// the correct "VDIF datathread id" to a tagged
// frame after splitting. The splitters split
// into tags 0..N-1, whereas the actual data channels
// may be reordered. SFXC's VDIF reader does not
// support the VEX VDIF-thread block (yet). So
// we do it from here.
//
// If the map is empty no remapping is done.
// If the map is NOT empty and the source-tag
// is not a key in the map then the data associated
// with that tag is discarded.
struct reframe_args {
    const uint16_t      station_id;
    blockpool_type*     pool;
    tagremapper_type    tagremapper;
    const unsigned int  bitrate;
    const unsigned int  input_size;
    const unsigned int  output_size;
    const unsigned int  bits_per_channel;

    reframe_args(uint16_t sid, unsigned int br, unsigned int ip, unsigned int op, unsigned int bpc);
    ~reframe_args();
};

multifdargs*   multiopener( multidestparms mdp );
multifdargs*   multifileopener( multidestparms mdp );
void           multicloser( multifdargs* );

void           tagger( inq_type<frame>*, outq_type<tagged<frame> >*, sync_type<unsigned int>* );
void           splitter( inq_type<frame>*, outq_type<tagged<frame> >*, sync_type<splitterargs>* );
void           coalescing_splitter( inq_type<tagged<frame> >*, outq_type<tagged<frame> >*, sync_type<splitterargs>* );
void           reframe_to_vdif(inq_type<tagged<frame> >*, outq_type<tagged<miniblocklist_type> >*, sync_type<reframe_args>* );
// the reframe-to-vdif step assumes that frames with only payload are being sent to it
// so we must have a headestripper in case no splitting is done
void           header_stripper(inq_type<tagged<frame> >*, outq_type<tagged<frame> >*, sync_type<headersearch_type>*);

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

#include <tthreadfns.h>

#endif
