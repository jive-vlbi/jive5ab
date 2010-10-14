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
#include <headersearch.h>
#include <trackmask.h>
#include <constraints.h>

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

struct frame {
    format_type  frametype;
    unsigned int ntrack;
    block        framedata;

    frame();
    frame(format_type tp, unsigned int n, block data);
};

// Possible datasources
void fillpatterngenerator(outq_type<block>*, sync_type<fillpatargs>*);
void fiforeader(outq_type<block>*, sync_type<fiforeaderargs>* );
void diskreader(outq_type<block>*, sync_type<diskreaderargs>* );
void fdreader(outq_type<block>*, sync_type<fdreaderargs>* );
void netreader(outq_type<block>*, sync_type<fdreaderargs>*);

// steps

// takes in raw data and outputs complete dataframes as per
//  * mark4/vlba format as taken from Mark4 memo 230, revision 1.21, 10 June 2005
//      "Mark IIIA/IV/VLBA Tape Formats, Recording Modes and Compatibility"
//  * mark5b format as described in "Mark 5B User's manual", 8 August 2006 
//      (http://www.haystack.mit.edu/tech/vlbi/mark5/docs/Mark%205B%20users%20manual.pdf)
void framer(inq_type<block>*, outq_type<frame>*, sync_type<framerargs>*);

// inputs dataframes and outputs compressed blocks
void framecompressor(inq_type<frame>*, outq_type<block>*, sync_type<compressorargs>*);

// inputs full dataframes and outputs only the binary frame
//    you lose knowledge of the actual type of frame
void frame2block(inq_type<frame>*, outq_type<block>*);

// these two do "blind" compression and decompression
void blockcompressor(inq_type<block>*, outq_type<block>*, sync_type<runtime*>*);
void blockdecompressor(inq_type<block>*, outq_type<block>*, sync_type<runtime*>*);


// The consumers
void fifowriter(inq_type<block>*, sync_type<runtime*>*);
void diskwriter(inq_type<block>*);

// write to the network. will choose appropriate "real" writer based on
// actual networkprotocol
void netwriter(inq_type<block>*, sync_type<fdreaderargs>*);

// generic write to filedescriptor. does NOT look at sizes, writes whatever
// it receives on the filedescriptor.
void fdwriter(inq_type<block>*, sync_type<fdreaderargs>*);





// information for the framer - it must know which
// kind of frames to look for ... 
struct framerargs {
    runtime*           rteptr;
    unsigned char*     buffer;
    headersearch_type  hdr;

    framerargs(headersearch_type h, runtime* rte);
    ~framerargs();
};

struct fillpatargs {
    bool                   run;
    runtime*               rteptr;
    unsigned int           nword;
    unsigned char*         buffer;
    unsigned long long int fill;


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
    // default/-1 implies ~4G words (~32GBytes) of pure unadulterated
    // fillpattern.
    void set_nword(unsigned int n);

    // defaults: run==false, rteptr==0, buffer==0,
    //           nbyte==-1 (=> ~4GB of data generated)
    //           fill==0x1122334411223344
    fillpatargs();
    // almost default save for rteptr, wich will be == r
    fillpatargs(runtime* r);

    // calls delete [] on buffer.
    ~fillpatargs();
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
    bool           run;       // the reader blocks until this one becomes "true" 
    runtime*       rteptr;    // will update statistics in this one
    unsigned char* buffer;    // will be automagically delete []'d when 
                              // the chain is not used anymore. within the
                              // fiforeader you can fill this one in with
                              // new [] and forget about the memory management!
    void set_run( bool newrunval );

    // defaults: run==false, rteptr==0, buffer==0
    fiforeaderargs();
    ~fiforeaderargs();
};

struct diskreaderargs {
    bool               run;      // the reader blocks until this one becomes "true" 
    bool               repeat;   // ...
    runtime*           rteptr;   // will update statistics in this one
    playpointer        pp_start, pp_end; // range to play back. both==0 => whole disk
    unsigned char*     buffer;   // will be automagically delete []'d when 
                                 // the chain is not used anymore

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
    unsigned char*  buffer;

    fdreaderargs();
    ~fdreaderargs();
};

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

void close_filedescriptor(fdreaderargs*);

#endif
