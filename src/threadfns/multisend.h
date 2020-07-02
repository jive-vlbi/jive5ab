// Copyright (C) 2007-2019 Harro Verkouter
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
#ifndef EVLBI5A_MULTISEND_H
#define EVLBI5A_MULTISEND_H

#include <runtime.h>
#include <threadfns.h>
#include <ezexcept.h>
#include <mountpoint.h>

#include <list>
#include <string>
#include <map>

DECLARE_EZEXCEPT(FileSizeException)

// generic tagged item
template <typename Tag, typename Item>
struct taggeditem {

    taggeditem() :
        tag( Tag() ), item( Item() )
    {}
    taggeditem(const Tag& t, const Item& i) :
        tag( t ), item( i )
    {}
    Tag     tag;
    Item    item;
};

// Description of a chunk of data
// fileName should be a relative path "<scan>/<scan>.<number>"
// such that it can be appended to any old mountpoint or root
struct filemetadata {
    off_t        fileSize;
    uint32_t     chunkSequenceNr;
    std::string  fileName;

    filemetadata();
    filemetadata(const std::string& fn, off_t sz, uint32_t csn);
};


// A chunk consists of some meta data + the contents
typedef taggeditem<filemetadata, block>  chunk_type;

// For each chunk's location we keep it in two parts:
//   the mountpoint/rootdir + the relative path wrt to that
struct chunk_location {
    std::string  mountpoint;         // e.g. "/mnt/disk19"
    std::string  relative_path;      // e.g. "te110_Mh_No0019/te110_Mh_No0019.00012035"

    chunk_location();

    chunk_location(std::string mp, std::string rel);
};

// The chunkmakers take an argument of this type as shared data type
struct chunkmakerargs_type {

    runtime*    rteptr;
    std::string recording_name;

    // asserts that rte != null && rec != empty 
    chunkmakerargs_type(runtime* rte, std::string const& rec);

    private:
        // no default c'tor
        // [declared but not defined - we're not in C11 yet :-(]
        chunkmakerargs_type();
};


// The list of filenames (chunks) and a function to build
// a list of chunks for a specific scan
typedef std::list<chunk_location>        chunklist_type;

chunklist_type get_chunklist(std::string scan, const mountpointlist_type& mountpoints);

// and keep a mapping of which thread is handling which file descriptor
typedef std::map<pthread_t,int>          threadfdlist_type;

// Keep a map of "filesize" => memory pool instance
// Ideally there should be only one
typedef std::map<off_t, blockpool_type*> mempool_type;


// Parameter for the multi-file reader
// The only thing we require is the memory pool and
// the threadfdlist
struct multireadargs {
    mempool_type      mempool;
    threadfdlist_type threadlist;

    // delete all block pools!
    ~multireadargs();
};

// Map from mountpoint => file descriptor
typedef std::map<std::string, int> fdmap_type;

// Mark6 info
struct mark6_vars_type {
    const bool                            mk6;
    const int32_t                         packet_size;
    const mk6_file_header::packet_formats packet_format;


    // Default c'tor => No Mk6 emulation
    mark6_vars_type();

    // Non-default => set Mk6 emulation info
    mark6_vars_type(int32_t ps, mk6_file_header::packet_formats pf);
};

struct multifileargs {

    multifileargs(runtime* ptr, filelist_type fl, mark6_vars_type mk6);

    // some situations require to keep track of how many
    // items there *could* be in the filelist_type
    // Upon construction listlen==fl.size()
    //  E.g. the parallelwriter() below will
    //       pop a mountpoint from the list, attempt
    //       to write a file there. If succesfully written
    //       the mountpoint gets added to the back of the list.
    //       If the writing of the file failed, the mountpoint
    //       is bad and is discarded. It is not put back on
    //       the list and listlen is decremented by one.
    //       This allows threads to detect "oh crap we
    //       ran out of possible mount points" (i.e. listlen==0,
    //       filelist.size()==0)
    //       which is different from "ok, no mountpoints currently
    //       available, I'll wait indefinitely until someone finishes writing"
    //       (listlen!=0, filelist.size()==0)
    //
    //       As you can see the only way to discriminate between the
    //       two cases is by keeping track of how many items there
    //       *could* be in the list. If that drops to zero there's no
    //       point in waiting and the threads exit.
    size_t            listlength;
    runtime*          rteptr;
    fdmap_type        fdmap;
    mempool_type      mempool;
    filelist_type     filelist;
    mark6_vars_type   mk6vars;
    threadfdlist_type threadlist;

    ~multifileargs();
};


typedef ssize_t (*writefnptr)(int, const void*, size_t, int);
typedef ssize_t (*readfnptr)(int, void*, size_t, int);
typedef void    (*setipdfnptr)(int, int);
typedef int     (*closefnptr)(int);

struct fdoperations_type {
    // will create something with NULL function pointers!
    fdoperations_type();

    // this is the c'tor to use
    fdoperations_type(const std::string& proto);


    writefnptr  writefn;
    readfnptr   readfn;
    setipdfnptr setipdfn;
    closefnptr  closefn;

    // These functions will implement read/write using
    // loops, in order to make the read/write work even 
    // if "n" is HUGE (e.g. 512MB). Single read/write
    // operations on sockets of this size typically just fail.
    //
    // They don't throw, just check if return value == how many
    // you wanted to read/write. If unequal, inspect errno
    ssize_t     read(int fd, void* ptr, size_t n, int f=0) const;
    ssize_t     write(int fd, const void* ptr, size_t n, int f=0) const;
    void        set_ipd(int fd, int ipd) const;
    int         close(int fd) const;
};

// Helper function to read the "itcp_id" style header (see "kvmap.h")
std::string read_itcp_header(int fd, const fdoperations_type& fdops);

// For the mulitple net readers we need an fdreaderargs type
// to hold the server fd to accept() on and a mempool where to
// get the data blocks from
struct multinetargs {

    multinetargs(fdreaderargs* fd);

    mempool_type       mempool;
    fdreaderargs*      fdreader;
    fdoperations_type  fdoperations;
    threadfdlist_type  threadlist;

    ~multinetargs();
};

struct rsyncinitargs {
    std::string         scanname;
    fdreaderargs*       conn;
    networkargs         netargs;

    rsyncinitargs(std::string n, networkargs na);
    ~rsyncinitargs();
};

// Compile the list of files to read. Make sure we strip across 
// the file systems

//multifileargs* get_filelist(runtime* rteptr, std::string scan);
multifileargs* get_mountpoints(runtime* rteptr, mark6_vars_type mk6);
multinetargs*  mk_server(runtime* rteptr, netparms_type np);

// Send SIGUSR1 to all threads in mnaptr->threadlist or
// mfaptr->threadlist
void           mfa_close(multifileargs* mfaptr);
void           mna_close(multinetargs* mnaptr);
void           rsyncinit_close(rsyncinitargs* mnaptr);

// The initiator. Negotiates with the remote side about which files
// to actually transfer. Also takes care of the striping of the local
// read.
void rsyncinitiator(outq_type<chunk_location>*, sync_type<rsyncinitargs>*);

// The idea is that >1 filereader is active and is pushing on the output
// queue the contents of file #N, tagging each block with the number N.
// multifileargs will contain a list of files to read
//void parallelreader(outq_type<chunk_location>*, sync_type<multifileargs>*);
void parallelreader2(inq_type<chunk_location>*, outq_type<chunk_type>*, sync_type<multireadargs>*);

// For each popped item a new connection will be opened; i.e. the
// blocks will be transferred in parallel.
void parallelsender(inq_type<chunk_type>*, sync_type<networkargs>*);


// >1 parallel net reader should be active, each does an "accept()" on
// the server and sucks the data out of the listen socket.
void parallelnetreader(outq_type<chunk_type>*, sync_type<multinetargs>*);

// For each popped item, open a new file and dump the contents.
// Wait until an item in the file list becomes available; the file list
// now is a list of mount points "/mnt/diskN" where we can write to.
// As soon as we're finished writing we put it back onto the list.
void parallelwriter(inq_type<chunk_type>*, sync_type<multifileargs>*);
void parallelsink(inq_type<chunk_type>*, sync_type<multifileargs>*);

// The chunkmaker step transfers big blocks of data
// into chunk_type so's they can be processed further.
// The only useful information for this step is the actual scan name
void chunkmaker(inq_type<block>*, outq_type<chunk_type>*, sync_type<chunkmakerargs_type>*);
void mk6_chunkmaker(inq_type<block>*, outq_type<chunk_type>*, sync_type<chunkmakerargs_type>*);
void chunkmaker_stream(inq_type< tagged<block> >*, outq_type<chunk_type>*, sync_type<chunkmakerargs_type>*);
void mk6_chunkmaker_stream(inq_type< tagged<block> >*, outq_type<chunk_type>*, sync_type<chunkmakerargs_type>*);

#endif
