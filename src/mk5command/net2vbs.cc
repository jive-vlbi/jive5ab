// Copyright (C) 2007-2013 Harro Verkouter
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
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <threadfns.h>    // for all the processing steps + argument structs
#include <threadutil.h>
#include <threadfns/multisend.h>
#include <headersearch.h>
#include <directory_helper_templates.h>
#include <regular_expression.h>
#include <interchainfns.h>
#include <mountpoint.h>   // for mp_thread_create
#include <sciprint.h>

#include <inttypes.h>     // For SCNu64 and friends
#include <limits.h>
//#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
//   #include <ifaddrs.h>
//   #include <stdio.h>
//   #include <stdlib.h>
//   #include <unistd.h>
#include <map>


#include <iostream>

using namespace std;



// Get interface => IP address mapping
//   Note: we only work with IPv4!!
typedef map<string, string>  if2addr_type;

if2addr_type mk_if2addr( void );

static if2addr_type if2addr = mk_if2addr();


// Scan the mountpoint(s) for existance of directories named
//     "<scanname>[a-zA-Z]?"
// (We must see if any of the possible prefixes (see Mark5 command set
// documentation for "record=on" command:

// "An attempt to record scan with the same name will augment scan name by
// appending a suffix 'a'-'z', then 'A'-'Z' and restarts at 'a' after 52
// recordings of the same scan name."

// Given a path, return the matchresult of matching it against the regex
struct regex_predicate {
    typedef matchresult value_type;

    regex_predicate(Regular_Expression const* reptr):
        __m_regular_expression_ptr(reptr)
    { }

    matchresult operator()(string const& path) const {
        return __m_regular_expression_ptr->matches(path);
    }

    Regular_Expression const*    __m_regular_expression_ptr;
};

// This typedef allows us to map the regex_predicate over a directory and
// return a list of match objects. The match objects will
//   1.) tell us if any of the entries matched
//   2.) it could capture useful parts out of the match, 
//       in our case an existing suffix, if any
typedef dir_mapper<regex_predicate>  regex_matcher_type;


// For each suffix we count how often it occurs
struct duplicate_counter_data {
    duplicate_counter_data():
        duplicate_detected( false )
    {}
    bool                     duplicate_detected;
    map<char, unsigned int>  duplicate_count;
};

// Wrap the duplicate_counter_data in an output iterator. That way we can
// just copy the results from the directory-mapper directly into the counter
// data!
struct duplicate_counter: public std::iterator<std::output_iterator_tag, duplicate_counter> {
    // Implement output iterator interface
    duplicate_counter(duplicate_counter_data* p):
        __m_data_ptr( p )
    {}
    
    // Implement output iterator interface
    duplicate_counter& operator*( void )  { return *this; }
    duplicate_counter& operator++( void ) { return *this; }
    duplicate_counter& operator++( int  ) { return *this; }

    // assignment is the only one that actually does something ;-)
    duplicate_counter& operator=( regex_matcher_type::value_type::value_type const& kvref ) {
        matchresult const& mo = kvref.second;

        // No match? Nothing to do!
        if( !mo )
            return *this;
        // do we have a group? [it could be the empty group (ie no
        // suffix) - in which case it would be the exact scan name]
        if( mo[1] )
            __m_data_ptr->duplicate_count[ mo.group(1)[0] ]++;
        else 
            __m_data_ptr->duplicate_detected = true;
        return *this;
    }
    duplicate_counter_data*  __m_data_ptr;

    private:
        duplicate_counter();
};

struct nameScannerArgs {
    nameScannerArgs(string const& mp, string const& scanname, pthread_mutex_t* mtx,
                    duplicate_counter_data* dupcntrptr, const bool /*mk6*/):
        mountPoint( mp ), mutexPointer( mtx ),
        dupCounterDataPtr( dupcntrptr ),
        //rxScanName( string("^")+mp+"/"+scanname+"([a-zA-Z])?"+(mk6?"\\.mk6":"")+"$" )
        rxScanName( string("^")+mp+"/"+scanname+"([a-zA-Z])?$" )
    {}

    const string              mountPoint;
    pthread_mutex_t*          mutexPointer;
    duplicate_counter_data*   dupCounterDataPtr;
    const Regular_Expression  rxScanName;

    private:
        nameScannerArgs();
};

///////// The thread function
void* nameScanner(void* args) {
    nameScannerArgs*    nsa = (nameScannerArgs*)args;

    // Don't let exceptions escape
    try {
        // Find directories in mountpoint having a name like requested scan.
        const regex_matcher_type::value_type  matchresults = regex_matcher_type( regex_predicate(&nsa->rxScanName) )( nsa->mountPoint );

        // 'copy' them all into the duplicate counter
        ::pthread_mutex_lock( nsa->mutexPointer );
        copy(matchresults.begin(), matchresults.end(), duplicate_counter(nsa->dupCounterDataPtr));
        ::pthread_mutex_unlock( nsa->mutexPointer );
    }
    catch( const exception& e ) {
        DEBUG(-1, "nameScanner[" << nsa->mountPoint << "]: caught exception - " << e.what() << endl);
    }
    catch( ... ) {
        DEBUG(-1, "nameScanner[" << nsa->mountPoint << "]: caught unknown exception" << endl);
    }

    // we don't need the nameScannerArgs anymore
    delete nsa;
    return (void*)0;
}

typedef std::list<pthread_t*>   threadidlist_type;

string mk_scan_name(string const& scanname, mountpointlist_type const& mps, const bool mk6) {
    int                    create_error;
    string                 rv( scanname );
    pthread_mutex_t        mtx = PTHREAD_MUTEX_INITIALIZER;
    threadidlist_type      threads;
    duplicate_counter_data cnt;

    // Nothing given or the null disk set ("set_disks=null")? Nothing to do!
    if( rv.empty() || is_null_diskset(mps) )
        return rv;

    // loop over all mountpoints
    create_error = 0;
    for(mountpointlist_type::const_iterator mp=mps.begin(); mp!=mps.end(); mp++) {
        pthread_t*   tid = new pthread_t();

        if( (create_error=mp_pthread_create(tid, &nameScanner, new nameScannerArgs(*mp, scanname, &mtx, &cnt, mk6)))!=0 )
            break;
        threads.push_back( tid );
    }

    // Join all threads we created, discarding the return value
    for( threadidlist_type::iterator thrdptrptr=threads.begin(); thrdptrptr!=threads.end(); thrdptrptr++ )
        ::pthread_join( **thrdptrptr, 0 );

    // If create_error != 0, something went wrong starting all the threads
    EZASSERT2(create_error==0, cmdexception, EZINFO(" - failed to create one or more threads: " << evlbi5a::strerror(create_error)));

    // Now we can get to analyzing the result
    if( cnt.duplicate_detected==false )
        return rv;

    // Duplicates found!
    char         extension        = '\0';
    unsigned int suffix_use_count = UINT_MAX;

    for( char extension_candidate='a'; 
              extension_candidate!= ('Z' + 1);
              extension_candidate = (extension_candidate=='z'?'A':extension_candidate+1) ) {
        if( cnt.duplicate_count[extension_candidate]<suffix_use_count ) {
            suffix_use_count = cnt.duplicate_count[extension_candidate];
            extension        = extension_candidate;
        }
    }
    // On FlexBuf we can not support more than 52 recordings of the same
    // name. Because each recording shares the same directory for storing
    // its chunks in, we cannot have two recordings called e.g. <scanname>A.
    // On the Mark5s this IS possible - the name only points at a recorded
    // byte range ...
    EZASSERT2(suffix_use_count==0, cmdexception, EZINFO(" - more than 52 recordings with the same scan name is not possible"));
    DEBUG(-1, "mk_scan_name: duplicate scan name " << scanname << ", extending scan name with '" << extension << "'" << endl);
    rv += extension;
    return rv;
}


void net2vbsguard_fn(runtime* rteptr) {
    try {
        DEBUG(3, "net2vbs guard function: transfer done" << endl);
        RTEEXEC( *rteptr, rteptr->transfermode = no_transfer; rteptr->transfersubmode.clr( run_flag ) );
    }
    catch ( const std::exception& e) {
        DEBUG(-1, "net2vbs finalization threw an exception: " << e.what() << std::endl );
    }
    catch ( ... ) {
        DEBUG(-1, "net2vbs finalization threw an unknown exception" << std::endl );        
    }
    rteptr->transfermode = no_transfer;
}

// Per runtime we keep the settings of how many parallel network readers
// (only with "net2vbs", not used when used as "record") + how many
// parallel file writers are started.
// The default c'tor assumes 1 each - the absolute minimum
struct nthread_type {
    unsigned long int    nParallelReader;
    unsigned long int    nParallelWriter;

    nthread_type() :
        nParallelReader( 1 ), nParallelWriter( 1 )
    {}
};


void restore_blocksize(runtime* rteptr, unsigned int obs) {
    DEBUG(4, "restore_blocksize/restoring block size to " << obs << endl);
    rteptr->netparms.set_blocksize( obs );
}

///////////////////////////////////////////////////////////////////////////////////
//
//                         Support net2vbs
//
///////////////////////////////////////////////////////////////////////////////////
string net2vbs_fn( bool qry, const vector<string>& args, runtime& rte, bool forking) {
    ostringstream                     reply;
    const transfer_type               rtm( args[0]=="record" ? vbsrecord : string2transfermode(args[0]) ); // requested transfer mode
    const transfer_type               ctm( rte.transfermode ); // current transfer mode
    static per_runtime<nthread_type>  nthread;
    static per_runtime<chain::stepid> use_closefd;

    // Assert that the requested transfermode is one that we support
    EZASSERT2(rtm==net2vbs || rtm==fill2vbs || rtm==vbsrecord || rtm==mem2vbs, cmdexception,
              EZINFO("This implementation of net2vbs_fn does not support '" << args[0] << "'"));

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is *always* possible, command will register 'busy'
    // if not doing nothing or the requested transfer mode does not match the
    // current transfermode
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer || ctm==rtm))

    // Good. See what the usr wants
    if( qry ) {
        // may query 'nthread' rather than vbs2net status
        //    vbs2net?         => vbs2net status
        //    vbs2net? nthread => query how many threads configured
        const string    what( OPTARG(1, args) );

        // Queries always work
        reply << " 0 : ";

        if( what=="nthread" ) {
            reply << nthread[&rte].nParallelReader << " : " << nthread[&rte].nParallelWriter;
        } else if( what=="mk6" ) {
            reply << rte.mk6info.mk6;
        } else {
            if( ctm==no_transfer || rtm!=ctm ) {
                // GiuseppeM suggests to return "on/off" for record?
                reply << (rtm==vbsrecord ? "off" : "inactive");
            } else {
                // we ARE running so we must be able to retrieve the lasthost
                // GiuseppeM suggests to return "on/off" for record?
                reply << (ctm==vbsrecord ? "on" : "active");
                // If we are doing something that behaves like 'record=on'
                // insert the recording name in the query reply
                if( rtm==vbsrecord || rtm==mem2vbs || rtm==fill2vbs )
                    reply << " : " << rte.mk6info.dirList.size() << " : " << *rte.mk6info.dirList.begin();
                // And add the byte counter
                reply << " : " << rte.statistics.counter(0);
            }
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 8 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    bool  recognized = false;


    // net2vbs  = open [no options yet]
    // fill2vbs = on : <scan name>   JQ/HV: 26Oct2016: better to switch to "on/off" semantics;
    //                                                 it was "open/close" ...
    // record   = on : <scan name>
    // mem2vbs  = on : <scan name>
    if( (rtm==net2vbs && args[1]=="open") ||
        ((rtm==vbsrecord || rtm==mem2vbs || rtm==fill2vbs) && args[1]=="on") ) {
        recognized = true;
        // if transfermode is not no_transfer, we ARE already doing stuff
        if( rte.transfermode==no_transfer ) {
            // build up a new instance of the chain
            const nthread_type&             nthreadref = nthread[&rte];
            chain                           c;
            const bool                      rsync = (rtm==net2vbs);
            unsigned int                    m6pkt_sz = (unsigned int)-1;
            const string                    protocol( rte.netparms.get_protocol() ); 
            const string                    org_scanname( OPTARG(2, args) );
            mk6info_type const&             mk6info( rte.mk6info );
            const string                    scanname( rsync ? string() : mk_scan_name(org_scanname, mk6info.mountpoints, mk6info.mk6) );
            chain::stepid                   s1, s2;
            mk6_file_header::packet_formats m6fmt = mk6_file_header::UNKNOWN_FORMAT;

            // At the moment we can only do rsync over tcp or udt 
            if( rsync  ) {
                EZASSERT2( protocol=="tcp" || protocol=="udt", cmdexception,
                           EZINFO("only supported on tcp or udt protocol") )
            } else {
                // Not rsync, so must provide a scan name
                EZASSERT2( !scanname.empty(), cmdexception,
                           EZINFO("must provide a scan name") )

                // Also, when doing recording, we must constrain our 
                // block size and other packet parameters
                const unsigned int      obs   = rte.netparms.get_blocksize();
                // Depending on what we're doing, we prolly should set a
                // sensible block size (the default, 128kB, is totally not
                // adequate for neither VBS nor Mark6 ...)
                // Let's go with 128 MB minimum VBS file size and 8M for Mark6.
                const unsigned int      minbs = mk6info_type::minBlockSizeMap[ mk6info.mk6 ]; //( mk6info.mk6 ? 8*1024*1024 : 128 * 1024 * 1024 );
                const headersearch_type dataformat(rte.trackformat(), rte.ntrack(),
                                                   rte.trackbitrate(),
                                                   rte.vdifframesize());

                // Set new block size if necessary
                if( obs < minbs )
                    rte.netparms.set_blocksize( minbs );
                DEBUG(4, "Selecting scatter block size " << byteprint(rte.netparms.get_blocksize(), "byte") << " [minimum " <<  byteprint(minbs, "byte") << "]" << endl);

                // fill2vbs can record both no data or valid,
                // non-fill2vbs only valid
                if( dataformat.valid() ) {
                    // on the flexbuff we must always constrain our blocks to an integral number of frames
                    rte.sizes = constrain(rte.netparms, dataformat, rte.solution, constraints::BYFRAMESIZE);
                } else {
                    // Unknown data format can only be recorded by fill2vbs
                    EZASSERT2( rtm==fill2vbs, cmdexception,
                               EZINFO(rtm << " can only record a known data format");
                               if( obs<minbs ) rte.netparms.set_blocksize(obs); );

                    // because data format is invalid, we can't
                    // constrain by frame size, can we now?
                    rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
                }

                // If we passed this, we may optionally have to put back the
                // old block size
                if( obs < minbs )
                    c.register_final(&restore_blocksize, &rte, obs);

                // Translate dataformat into Mark6 enum
                if( is_vdif(dataformat.frameformat) )
                    m6fmt = mk6_file_header::VDIF;
                else if( dataformat.frameformat==fmt_mark5b )
                    m6fmt = mk6_file_header::MK5B;

                // And record packet size for Mk6. For some reason some
                // ppl think that's interesting / valuable to record in
                // the data file ... 
                m6pkt_sz = dataformat.framesize;
            }

            // add the steps to the chain. 
            if( rsync ) {
                // rsync version
                s1 = c.add( &parallelnetreader, 4, &mk_server, &rte, rte.netparms );
                c.register_cancel(s1, &mna_close);
                // Five parallel readers
                c.nthread( s1, nthreadref.nParallelReader );
            } else if( rtm==fill2vbs ) {
                // Known format?
                if( rte.trackformat()!=fmt_none ) {
                    fillpatargs   fpargs( &rte );
                    chain::stepid gen;
                    fpargs.run = true;
                    fpargs.inc = 0;
                    gen = c.add( &fillpatternwrapper, 2, fpargs);
                    // Set number of parallel writers as configured
                    c.nthread( gen, nthreadref.nParallelReader );
                } else {
                    // Produce empty blocks - as efficiently as possible
                    c.add( &emptyblockmaker, nthreadref.nParallelWriter+1, emptyblock_args(&rte, rte.netparms));
                }

                // Must add a step which transforms block => chunk_type,
                // i.e. count the chunks and generate filenames
                chunkmakerargs_type  chunkmakerargs(&rte, scanname);
                if( mk6info.mk6 )
                    c.add( &mk6_chunkmaker, 2, chunkmakerargs );
                else
                    c.add( &chunkmaker    , 2, chunkmakerargs );
            } else {
                // just suck the network card or membuf empty,
                // allowing for partial blocks
                bool useStreams = false;

                if( rtm==mem2vbs ) {
                    // Add a queue reader
                    queue_reader_args qra( &rte );
                    qra.run = true;
                    c.register_cancel(c.add(&queue_reader, 4, qra), &cancel_queue_reader);
                    c.register_final(&finalize_queue_reader, &rte);
                } else {
                    // This is not mem2vbs so has to be net2* so when the
                    // recording is to be shut off, we first close the
                    // filedescriptor and only *then* disable the queue
                    // hoping to minimize loss of partially filled block(s)
                    chain::stepid  readstep = chain::invalid_stepid;

                    // VGOS request: can we record threads by themselves?
                    //      answer:  maybe! let's see what we can do. This
                    //      only works for VDIF
                    if( is_vdif(rte.trackformat()) && !mk6info.datastreams.empty() ) {
                        // The netreaders now output tagged blocks
                        readstep = c.add(&netreader_stream, 4, &net_server, networkargs(&rte, true));

                        c.register_cancel( readstep, &close_filedescriptor);
                        if( protocol=="udps" )
                            c.register_cancel( readstep, &wait_for_udps_finish );

                        // If forking requested, splice off the raw data here,
                        // before we make FlexBuff/Mark6 chunks of them
                        if( forking )
                            c.add(&tagged_queue_forker, 1, queue_forker_args(&rte));
                        // Now we have tagged blocks, need to feed them to
                        // chunkmakers that know how to handle tagged blocks
                        useStreams = true;
                    } else {
                        readstep = c.add(&netreader, 4, &net_server, networkargs(&rte, true));

                        // Cancellations are processed in the order they are
                        // registered. Which is good ... in case of UDPS protocol we
                        // need another - 'dangerous' - blocking 'cancellation'
                        // function which allows for the bottom/top half to finish
                        // properly
                        c.register_cancel( readstep, &close_filedescriptor);

                        if( protocol=="udps" )
                            c.register_cancel( readstep, &wait_for_udps_finish );

                        // If forking requested, splice off the raw data here,
                        // before we make FlexBuff/Mark6 chunks of them
                        if( forking )
                            c.add(&queue_forker, 1, queue_forker_args(&rte));
                    }
                    if( readstep!=chain::invalid_stepid )
                        use_closefd[ &rte ] = readstep;
                }

                // Must add a step which transforms block => chunk_type,
                // i.e. count the chunks and generate filenames
                // Set number of buffer positions to number of mountpoints+1 
                // such that there's always enough positions free to be
                // writing to each mountpoint in parallel - or - should
                // there be less mountpoints than parallel writers, use that
                unsigned int const   nMountpoints = 1 + std::max(SAFE_UINT_CAST(rte.mk6info.mountpoints.size()),
                                                                 SAFE_UINT_CAST(nthreadref.nParallelWriter));
                chunkmakerargs_type  chunkmakerargs(&rte, scanname);
                if( mk6info.mk6 ) {
                    if( useStreams )
                        c.add( &mk6_chunkmaker_stream , nMountpoints, chunkmakerargs);
                    else
                        c.add( &mk6_chunkmaker        , nMountpoints, chunkmakerargs);
                } else {
                    if( useStreams )
                        c.add( &chunkmaker_stream     , nMountpoints, chunkmakerargs);
                    else
                        c.add( &chunkmaker            , nMountpoints, chunkmakerargs);
                }
            }

            // Add the striping step. If the selected mountpoint list is
            // the null list, no physical writing will be done. Handy for
            // testin'
            s2 = c.add( is_null_diskset(mk6info.mountpoints) ? &parallelsink : &parallelwriter,
                        // and the step user data creation
                        &get_mountpoints, &rte, mk6info.mk6 ? mark6_vars_type(m6pkt_sz, m6fmt)
                                                            : mark6_vars_type() );
            c.register_cancel(s2, &mfa_close);
            // Set number of parallel writers as configured
            c.nthread( s2, nthreadref.nParallelWriter );

            // register the finalization function
            c.register_final( &net2vbsguard_fn, &rte );

            rte.transfersubmode.clr_all().set( wait_flag );

            // reset statistics counters
            rte.statistics.clear();

            // install the chain in the rte and run it
            rte.processingchain = c;
            rte.processingchain.run();
                
            // Update global transferstatus variables to
            // indicate what we're doing. the submode will
            // be modified by the threads
            rte.transfermode = rtm;

            // Add to dirlist
            if( rtm!=net2vbs )
                rte.mk6info.dirList.push_front(scanname);
        
            reply << " 0 "
                  << ((!rsync && scanname!=org_scanname) ? string(" : ")+scanname : string())
                  << " ;";
        } else {
            reply << " 6 : Already doing " << rte.transfermode << " ;";
        }
    }


    // net2vbs  = close
    // fill2vbs = off                JQ/HV: 26Oct2016: better to switch to "on/off" semantics;
    //                                                 it was "open/close" ...
    // record   = off
    // mem2vbs  = off
    if( (rtm==net2vbs && args[1]=="close") ||
        ((rtm==vbsrecord || rtm==mem2vbs || rtm==fill2vbs) && args[1]=="off") ) {
            recognized = true;
            // Only allow if we're doing net2vbs
            // Don't care if we were running or not
            if( rte.transfermode!=no_transfer ) {
                try {
                    // Check if we're requested to close the filedescriptor
                    // first
                    per_runtime<chain::stepid>::iterator  p = use_closefd.find( &rte );
                    if( p!=use_closefd.end() ) {
                        chain::stepid s = p->second;
                        // Before actually using the stepid, delete the
                        // entry such that even in case of an exception the
                        // entry does not remain set to a possible invalid
                        // stepid
                        use_closefd.erase( p );
                        rte.processingchain.communicate(s, &close_filedescriptor);
                        // Give the code ~ half a second to push its cached
                        // data? There should be sufficient positions
                        // downstream (at least one per mountpoint or
                        // parallel disk writer) 
                        evlbi5a::usleep( 500000 );
                    }
                    // let the runtime stop the threads
                    rte.processingchain.gentle_stop();
                    
                    rte.transfersubmode.clr( connected_flag );
                    reply << " 1 ;";
                }
                catch ( std::exception& e ) {
                    reply << " 4 : Failed to stop processing chain: " << e.what() << " ;";
                }
                catch ( ... ) {
                    reply << " 4 : Failed to stop processing chain, unknown exception ;";
                }
                // When doing vbsrecord, set just recorded scan
                // 02/Nov/2016 JonQ mentions that fill2vbs doesn't
                //             set scan pointers as record=off does
                if( rtm==vbsrecord || rtm==mem2vbs || rtm==fill2vbs ) {
                    rte.mk6info.scanName = *rte.mk6info.dirList.begin();
                    rte.mk6info.fpStart  = 0;
                    rte.mk6info.fpEnd    = 0;
                }
            } else {
                reply << " 6 : Not doing " << args[0] << " ;";
            }
    }
    // net2vbs = nthread : [<nReader>] : [<nWriter>]
    if( args[1]=="nthread" ) {
        char*             eocptr;
        const string      nRd_s( OPTARG(2, args) );
        const string      nWrt_s( OPTARG(3, args) );
        nthread_type&     nthreadref = nthread[&rte];

        // Actually, we don't care if we got arguments. If we have'm we 
        // check + use 'm otherwise it's just a no-op :D
        recognized = true;
        reply << " 0 ;";

        // first up - number of parallel readers
        if( nRd_s.empty()==false ) {
            unsigned long int nRd;

            errno = 0;
            nRd   = ::strtoul(nRd_s.c_str(), &eocptr, 0);

            // Check if it's a sensible "unsigned" value for nthread - must
            // have at least 1!
            EZASSERT2(eocptr!=nRd_s.c_str() && *eocptr=='\0' && errno!=ERANGE && nRd>0 && nRd<=UINT_MAX,
                      cmdexception,
                      EZINFO("nParallelReader '" << nRd_s << "' out of range") );

            // We've verified it ain't bigger than UINT_MAX so this cast is safe
            nthreadref.nParallelReader = (unsigned int)nRd;
        }
        // Number of parallel writers
        if( nWrt_s.empty()==false ) {
            unsigned long int nWrt;

            errno = 0;
            nWrt  = ::strtoul(nWrt_s.c_str(), &eocptr, 0);

            // Check if it's a sensible "unsigned" value for nthread - must
            // have at least 1!
            EZASSERT2(eocptr!=nWrt_s.c_str() && *eocptr=='\0' && errno!=ERANGE && nWrt>0 && nWrt<=UINT_MAX,
                      cmdexception,
                      EZINFO("nParallelSender '" << nWrt << "' out of range") );

            // We've verified it ain't bigger than UINT_MAX so this cast is safe
            nthreadref.nParallelWriter = (unsigned int)nWrt;
        }
    }
    if( args[1]=="mk6" ) {
        char*             eocptr;
        const string      mk6_s( OPTARG(2, args) );

        // Actually, we don't care if we got arguments. If we have'm we 
        // check + use 'm otherwise it's just a no-op :D
        recognized = true;
        reply << " 0 ;";

        // first up - number of parallel readers
        if( mk6_s.empty()==false ) {
            long int m6;

            errno = 0;
            m6   = ::strtol(mk6_s.c_str(), &eocptr, 0);

            // Check if it's a number
            EZASSERT2(eocptr!=mk6_s.c_str() && *eocptr=='\0' && errno!=ERANGE,
                      cmdexception,
                      EZINFO("mk6 '" << mk6_s << "' out of range") );

            // Fine. We don't look at the actual value
            rte.mk6info.mk6 = (m6!=0);
        }
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";

    return reply.str();
}


///////////////// helper stuff

// Adapted from http://man7.org/linux/man-pages/man3/getifaddrs.3.html

if2addr_type mk_if2addr( void ) {
    if2addr_type    rv;
#if 0
    struct ifaddrs* ifaddr;
    // How fatal do we want this to be?
    //ASSERT_ZERO( ::getifaddrs(&ifaddr) );
    
    if( ::getifaddrs(&ifaddr)==-1 ) {
        DEBUG(-1, "mk_if2addr: failed to get ifaddrs - cannot use interface names with record=on" << endl);
        return rv;
    }

    // Walk through linked list, maintaining head pointer so we
    // can free list later
    for( struct ifaddrs* ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next ) {
        int    r;
        char   host[NI_MAXHOST];

        // We only do non-null IPv4 addresses ...
        if( ifa->ifa_addr==NULL || ifa->ifa_addr->sa_family!=AF_INET )
            continue;

        r = ::getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), 
                          host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if( r!=0 ) {
            DEBUG(-1, "mk_if2addr: failed to get nameinfo for " << ifa->ifa_name << " - " 
                      << ::gai_strerror(r) << endl);
            continue;
        }
        rv[ ifa->ifa_name ] = host;
    }
    ::freeifaddrs( ifaddr );
#else
    // http://stackoverflow.com/questions/4937529/polling-interface-names-via-siocgifconf-in-linux
    DEBUG(0, "mk_if2addr/not implemented yet! Need SIOCGIFCONF implementation!" << endl);
#endif
    return rv;
}
