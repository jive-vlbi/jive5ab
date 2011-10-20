// implementation of the commands
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
//
// * generic Mk5 commands [Mk5 hardware agnostic]
// * generic jive5a commands [ipd, pdr, tstat, mtu, ...]
// * specializations for
//      - Mk5A
//      - Mk5B flavour agnostic but Mk5B specific
//      - Mk5B/DIM
//      - Mk5B/DOM
// * commandmaps which define which of the commands
//   are allowed for which Mk5 flavour.
//   Currently there's 3 commandmaps:
//      - Mk5A
//      - Mk5B/DIM
//      - Mk5B/DOM
// * Utility functions for Mk5's
//   (eg programming Mk5B/DIM input section for recording:
//    is shared between dim2net and in2disk)
#include <mk5command.h>
#include <dosyscall.h>
#include <threadfns.h>
#include <playpointer.h>
#include <evlbidebug.h>
#include <getsok.h>
#include <streamutil.h>
#include <userdir.h>
#include <busywait.h>
#include <dotzooi.h>
#include <dayconversion.h>
#include <ioboard.h>
#include <stringutil.h>
#include <trackmask.h>
#include <sciprint.h>
#include <version.h>

// c++ stuff
#include <map>
#include <string>

// for setsockopt
#include <sys/types.h>
#include <sys/socket.h>

// and for "struct timeb"/ftime()
#include <sys/timeb.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
// for log/exp/floor
#include <math.h>
// zignal ztuv
#include <signal.h>

// inet functions
#include <netinet/in.h>
#include <arpa/inet.h>

// open(2)
#include <fcntl.h>

// ULLONG_MAX and friends
#include <limits.h>

using namespace std;


// Since the actual functions typically operate on a runtime environment
// and sometimes they need to remember something, it makes sense to do
// this on a per-runtime basis. This struct allows easy per-runtime
// saving of state.
// Usage:
//   per_runtime<string>  lasthost;
//   ...
//   swap(rte.netparms.host, lasthost[&rte]);
//   cout << lasthost[&rte] << endl;
//   lasthost[&rte] = "foo.bar.bz";
template <typename T>
struct per_runtime {
    typedef std::map<const runtime*, T> per_runtime_map_type;

    bool hasData(const runtime* r) {
        return per_runtime_map.find(r)!=per_runtime_map.end();
    }

    T& operator[](const runtime* r) {
        return per_runtime_map[r];
    }
    const T& operator[](const runtime* r) const {
        return per_runtime_map[r];
    }

    void erase(const runtime* r) {
        per_runtime_map.erase( per_runtime_map.find(r) );
        return;
    }
    private:
        per_runtime_map_type  per_runtime_map;
};

// returns the value of s[n] provided that:
//  s.size() > n
// otherwise returns the empty string
#define OPTARG(n, s) \
    ((s.size()>n)?s[n]:string())
//    (s.size()>n && !s[n].empty())?s[n]:string()

// function prototype for fn that programs & starts the
// Mk5B/DIM disk-frame-header-generator at the next
// available moment.
void start_mk5b_dfhg( runtime& rte, double maxsyncwait = 3.0 );



// "implementation" of the cmdexception
cmdexception::cmdexception( const string& m ):
    __msg( m )
{}

const char* cmdexception::what( void ) const throw() {
    return __msg.c_str();
}
cmdexception::~cmdexception() throw()
{}


// From a 'struct tm', compute the Modified Julian Day, cf.
//      http://en.wikipedia.org/wiki/Julian_day
// The Julian day number can be calculated using the following formulas:
// The months January to December are 1 to 12. Astronomical year numbering is used, thus 1 BC is
// 0, 2 BC is −1, and 4713 BC is −4712. In all divisions (except for JD) the floor function is
// applied to the quotient (for dates since 1 March −4800 all quotients are non-negative, so we
// can also apply truncation).
double tm2mjd( const struct tm& tref ) {
    double    a, y, m, jd;

    // As per localtime(3)/gmtime(3), the tm_mon(th) is according to
    // 0 => Jan, 1 => Feb etc
    a   = ::floor( ((double)(14-(tref.tm_mon+1)))/12.0 );

    // tm_year is 'years since 1900'
    y   = (tref.tm_year+1900) + 4800 - a;

    m   = (tref.tm_mon+1) + 12*a - 3;

    // tm_mday is 'day of month' with '1' being the first day of the month.
    // i think we must use the convention that the first day of the month is '0'?
    // This is, obviously, assuming that the date mentioned in 'tref' is 
    // a gregorian calendar based date ...
    jd  = (double)tref.tm_mday + ::floor( (153.0*m + 2.0)/5.0 ) + 365.0*y
          + ::floor( y/4.0 ) - ::floor( y/100.0 ) + ::floor( y/400.0 ) - 32045.0;
    // that concluded the 'integral day part'.

    // Now add the time-of-day, as a fraction
    jd += ( ((double)(tref.tm_hour - 12))/24.0 +
            ((double)(tref.tm_min))/1440.0 +
            (double)tref.tm_sec );

    // finally, return the mjd
    return (jd-2400000.5);
}

int jdboy (int year) {
  int jd, y;
  
  y = year + 4799;
  jd = y * 365 + y / 4 - y / 100 + y / 400 - 31739;
  
  return jd;
}

// encode an unsigned integer into BCD
// (we don't support negative numbahs)
unsigned int bcd(unsigned int v) {
    // we can fit two BCD-digits into each byte
    unsigned int       rv( 0 );
    const unsigned int nbcd_digits( 2*sizeof(unsigned int) );

    for( unsigned int i=0, pos=0; i<nbcd_digits; ++i, pos+=4 ) {
        rv |= ((v%10)<<pos);
        v  /= 10;
    }
    return rv;
}

// go from bcd => 'normal integer'
unsigned int unbcd(unsigned int v) {
    // we can fit two BCD-digits into each byte
    const unsigned int  nbcd_digits( 2*sizeof(unsigned int) );

    unsigned int  rv( 0 );
    unsigned int  factor( 1 );
    for( unsigned int i=0; i<nbcd_digits; ++i, factor*=10 ) {
        rv += ((v&0xf)*factor);
        v >>= 4;
    }
    return rv;

}

// timegm(3) is GNU extension, it is not in POSIX
// so let's re-implement it here, as per 
//    http://linux.die.net/man/3/timegm
time_t my_timegm(struct tm *tm) {
    char*  tz;
    time_t ret;

    tz = ::getenv("TZ");
    ::setenv("TZ", "", 1);
    ::tzset();
    ret = ::mktime(tm);
    if (tz)
        ::setenv("TZ", tz, 1);
    else
        ::unsetenv("TZ");
    ::tzset();
    return ret;
}

// Based on the information found in the runtime compute
// the theoretical IPD. 
// YOU MUST HAVE FILLED "rte.sizes" WITH THE RESULT OF A constrain()
// FUNCTION CALL BEFORE ACTUALLY CALLING THIS ONE!
void compute_theoretical_ipd( runtime& rte ) {
    netparms_type&     net( rte.netparms );
    const unsigned int datagramsize( rte.sizes[constraints::write_size] );

    if( datagramsize>0 ) {
        // total bits-per-second to send divided by the mtu (in bits)
        //  = number of packets per second to send. from this follows
        //  the packet spacing trivially
        // the trackbitrate already includes headerbits; both
        // for VLBA non-data-replacement bitrate and for Mark4 datareplacement.
        // the amount of headerbits for Mk5B format is marginal wrt total
        // bitrate.
        // TODO: take compression into account
        //       30 Jun 2010 HV - hopefully done.
        //
        // 20 Aug 2010: HV - ipd @ 1Gbps comes out as 124 us which is too
        //                   large; see FIFO filling up. Decided to add
        //                   a 0.9 fraction to the theoretical ipd
        const double correctionfactor( 0.9 );
        const double factor((rte.solution)?(rte.solution.compressionfactor()):1.0);
        const double n_pkt_p_s = ((rte.ntrack() * rte.trackbitrate() * factor) / (datagramsize*8)) * correctionfactor;

        // Note: remember! ipd should be in units of microseconds
        //       previous computation (before fix) yielded units 
        //       of seconds .. d'oh!
        if( n_pkt_p_s>0.0 ) {
            // floor(3) the value into integral microseconds;
            // the IPD can better be too small rather than too high.
            net.theoretical_ipd = (int) ::floor(1.0e6/n_pkt_p_s);
            DEBUG(1, "compute_theoretical_ipd: " << net.theoretical_ipd << "us" << endl);
        }
    }
    return;
}



//
//
//   The Mark5 commands
//
//


// Support disk2net and fill2net
string disk2net_fn( bool qry, const vector<string>& args, runtime& rte) {
    bool                atm; // acceptable transfer mode
    const bool          disk = (args[0]=="disk2net");
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer ||
           (disk && ctm==disk2net) ||
           (!disk && ctm==fill2net));

    // If we aren't doing anything nor doing disk/fill 2 net - we shouldn't be here!
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            // we ARE running so we must be able to retrieve the lasthost
            reply << rte.netparms.host << " : "
                  << rte.transfersubmode
                  << " : " << rte.pp_current;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        //
        //  disk2net = connect : <host>
        //     <host> is optional (remembers last host, if any)
        //  fill2net = connect : <host> [ : [<start>] [ : <inc> ] ]
        //     <host> is as with disk2net
        //     <start>, <inc> are the fillpattern start + increment values
        //     both have defaults:
        //        <start>   0x1122334411223344
        //        <inc>     0
        //        which means that by default it creates blocks of
        //        invalid data ["recognized by the Mark5's to be
        //        invalid data"]
        //    If a trackformat other than 'none' is set via the "mode=" 
        //    command the fillpattern will generate frames of the correct
        //    size, with the correct syncword at the correct place. ALL
        //    other bytes have been filled with the current bitpattern of
        //    the fillpattern (including pre-syncwordbytes, eg in the vlba
        //    case).
        if( args[1]=="connect" ) {
            recognized = true;
            // if transfermode is already disk2net, we ARE already connected
            // (only {disk|fill}2net::disconnect clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                // build up a new instance of the chain
                chain                   c;
                const string            protocol( rte.netparms.get_protocol() );
                const string            host( OPTARG(2, args) );
                const headersearch_type dataformat(rte.trackformat(), rte.ntrack());

                // diskplayback/fillpatternplayback has no mode/playrate/number-of-tracks
                // we do offer compression ... :P
                // HV: 08/Dec/2010  all transfers now key their constraints
                //                  off of the set mode. this allows better
                //                  control for all possible transfers
                rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

                // stick in a theoretical ipd close to that of 1Gbps -
                // we have NO information as to what the sustained diskspeed
                // is on this Mark5 nor what the linerate of the the link between 
                // this Mark5 and the destination is.
                const unsigned int payload = rte.sizes[constraints::write_size];
                const unsigned int n_bits_per_pkt( payload*8 );
                const unsigned int n_pkt_per_sec( (unsigned int)::ceil(1.0e9/n_bits_per_pkt) );

                rte.netparms.theoretical_ipd  = (int) ::floor(1.0e6 / n_pkt_per_sec);

                // the networkspecifics. 
                if( !host.empty() )
                    rte.netparms.host = host;

                // add the steps to the chain. depending on the 
                // protocol we add the correct networkwriter
                if( disk ) {
                    SSHANDLE ss = rte.xlrdev.sshandle();
                    // prepare disken/streamstor
                    XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
                    XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );
                    c.add(&diskreader, 10, diskreaderargs(&rte));
                } else {
                    // Do some more parsing
                    char*         eocptr;
                    fillpatargs   fpargs(&rte);
                    const string  start_s( OPTARG(3, args) );
                    const string  inc_s( OPTARG(4, args) );

                    if( start_s.empty()==false ) {
                        fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                        // !(A || B) => !A && !B
                        ASSERT2_COND( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                                      SCINFO("Failed to parse 'start' value") );
                    }
                    if( inc_s.empty()==false ) {
                        fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                        // !(A || B) => !A && !B
                        ASSERT2_COND( !(fpargs.inc==0 && eocptr==start_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                                      SCINFO("Failed to parse 'inc' value") );
                    }
                    c.add(&fillpatternwrapper, 10, fpargs);
                }

                // if the trackmask is set insert a blockcompressor 
                if( rte.solution )
                    c.add(&blockcompressor, 10, &rte);

                // register the cancellationfunction for the networkstep
                // which we will first add ;)
                // it will be called at the appropriate moment
                c.register_cancel(c.add(&netwriter, &net_client, networkargs(&rte)), &close_filedescriptor);

                rte.transfersubmode.clr_all().set( wait_flag );

                // reset statistics counters
                rte.statistics.clear();

                // Update global transferstatus variables to
                // indicate what we're doing. the submode will
                // be modified by the threads
                rte.transfermode    = (disk?disk2net:fill2net);

                // install the chain in the rte and run it
                rte.processingchain = c;
                rte.processingchain.run();

                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }

        // <on> : turn on dataflow
        //   disk2net=on[:[<start_byte>][:<end_byte>|+<amount>][:<repeat:0|1>]]
        //   fill2net=on[:<amount of WORDS @ 8-byte-per-word>]
        if( args[1]=="on" ) {
            recognized = true;
            // only allow if transfermode==disk2net && submode hasn't got the running flag
            // set AND it has the connectedflag set
            if( rte.transfermode==disk2net && rte.transfersubmode&connected_flag
                && (rte.transfersubmode&run_flag)==false ) {
                bool               repeat = false;
                uint64_t           nbyte;
                playpointer        pp_s;
                playpointer        pp_e;

                // Pick up optional extra arguments:
                // note: we do not support "scan_set" yet so
                //       the part in the doc where it sais
                //       that, when omitted, they refer to
                //       current scan start/end.. that no werk

                // start-byte #
                if( args.size()>2 && !args[2].empty() ) {
                    uint64_t v;

                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    // if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[2].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("start-byte# is out-of-range");
                    pp_s.Addr = v;
                }
                // end-byte #
                // if prefixed by "+" this means: "end = start + <this value>"
                // rather than "end = <this value>"
                if( args.size()>3 && !args[3].empty() ) {
                    uint64_t v;
                   
                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    //if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[3].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("end-byte# is out-of-range");
                    if( args[3][0]=='+' )
                        pp_e.Addr = pp_s.Addr + v;
                    else
                        pp_e.Addr = v;
                    ASSERT2_COND(pp_e>pp_s, SCINFO("end-byte-number should be > start-byte-number"));
                }
                // repeat
                if( args.size()>4 && !args[4].empty() ) {
                    long int    v = ::strtol(args[4].c_str(), 0, 0);

                    if( (v==LONG_MIN || v==LONG_MAX) && errno==ERANGE )
                        throw xlrexception("value for repeat is out-of-range");
                    repeat = (v!=0);
                }
                // now compute "real" start and end, if any
                // so the threads, when kicked off, don't have to
                // think but can just *go*!
                if( pp_e.Addr<=pp_s.Addr ) {
                    S_DIR       currec;
                    playpointer curlength;
                    // end <= start => either end not specified or
                    // neither start,end specified. Find length of recording
                    // and play *that*, starting at startbyte#
                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &currec) );
                    curlength = currec.Length;

                    // check validity of start,end
                    if( pp_s>=curlength ||  pp_e>=curlength ) {
                        ostringstream  err;
                        err << "start and/or end byte# out-of-range, curlength=" << curlength;
                        throw xlrexception( err.str() );
                    }
                    // if no end given: set it to the end of the current recording
                    if( pp_e==playpointer(0) )
                        pp_e = curlength;
                }
                // make sure the amount to play is an integral multiple of
                // blocksize
                nbyte = pp_e.Addr - pp_s.Addr;
                DEBUG(1, "start/end [nbyte]=" <<
                         pp_s << "/" << pp_e << " [" << nbyte << "] " <<
                         "repeat:" << repeat << endl);
                nbyte = nbyte/rte.netparms.get_blocksize() * rte.netparms.get_blocksize();
                if( nbyte<rte.netparms.get_blocksize() )
                    throw xlrexception("less than <blocksize> bytes selected to play. no can do");
                pp_e = pp_s.Addr + nbyte;
                DEBUG(1, "Made it: start/end [nbyte]=" <<
                         pp_s << "/" << pp_e << " [" << nbyte << "] " <<
                         "repeat:" << repeat << endl);

                // Now communicate all to the appropriate step in the chain.
                // We know the diskreader step is always the first step ..
                // make sure we do the "run -> true" as last one, as that's the condition
                // that will make the diskreader go
                rte.processingchain.communicate(0, &diskreaderargs::set_start,  pp_s);
                rte.processingchain.communicate(0, &diskreaderargs::set_end,    pp_e);
                rte.processingchain.communicate(0, &diskreaderargs::set_repeat, repeat);
                rte.processingchain.communicate(0, &diskreaderargs::set_run,    true);
                reply << " 0 ;";
            } else if( rte.transfermode==fill2net
                       && (rte.transfersubmode&connected_flag)==true
                       && (rte.transfersubmode&run_flag)==false ) {
                // not running yet!
                // pick up optional <number-of-words>
                if( args.size()>2 && !args[2].empty() ) {
                    unsigned long int  v = ::strtoul(args[2].c_str(), 0, 0);

                    if( v>UINT_MAX )
                        throw xlrexception("value for number of words is out-of-range");
                    // communicate this value to the chain
                    DEBUG(1,args[0] << "=" << args[1] << ": set nword to " << (unsigned int)v << endl);
                    rte.processingchain.communicate(0, &fillpatargs::set_nword, (unsigned int)v);
                }
                // and turn on the dataflow
                rte.processingchain.communicate(0, &fillpatargs::set_run, true);
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or {disk|fill}2net, nothing else
                if( rte.transfermode==disk2net||rte.transfermode==fill2net ) {
                    if( rte.transfersubmode&connected_flag )
                        reply << " 6 : already running ;";
                    else
                        reply << " 6 : not connected yet ;";
                } else 
                    reply << " 6 : not doing anything ;";
            }
        }

        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing disk2net.
            // Don't care if we were running or not
            if( rte.transfermode!=no_transfer ) {
                // let the runtime stop the threads
                rte.processingchain.stop();

                // reset global transfermode variables 
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing " << args[0] << " ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}

// disk2out (alias for 'play')
// should work on both Mark5a and Mark5B/DOM
typedef std::map<runtime*, pthread_t> threadmap_type;

string disk2out_fn(bool qry, const vector<string>& args, runtime& rte) {
    // keep a mapping of runtime -> delayed_play thread such that we
    // can cancel it if necessary
    static threadmap_type delay_play_map;

    // automatic variables
    ostringstream    reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing disk2out - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=disk2out ) {
        reply << " 1 : _something_ is happening and its NOT disk2out(play)!!! ;";
        return reply.str();
    }

    // Good, if query, tell'm our status
    if( qry ) {
        // we do not implement 'arm' so we can only be in one of three states:
        // waiting, off/inactive, on
        if( rte.transfermode==disk2out ) {
            // depending on 'wait' status (implies delayed play) indicate that
            if( rte.transfersubmode&wait_flag )
                reply << " 1 : waiting ;";
            else
                reply << " 0 : on ;";
        } else {
            reply << " 0 : off ;";
        }
        return reply.str();
    }

    // Handle command, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <on>[:<playpointer>[:<ROT>]]
        if( args[1]=="on" ) {
            recognized = true;
            // If ROT is given, then the playback will start at
            // that ROT for the given taskid [aka 'delayed play'].
            // If no taskid set or no rot-to-systemtime mapping
            // known for that taskid we FAIL.
            if( rte.transfermode==no_transfer ) {
                double                     rot( 0.0 );
                SSHANDLE                   ss( rte.xlrdev.sshandle() );
                playpointer                startpp;

                // Playpointer given?
                if( args.size()>2 && !args[2].empty() ) {
                    uint64_t v;

                    // kludge to get around missin ULLONG_MAX missing.
                    // set errno to 0 first and see if it got set to ERANGE after
                    // call to strtoull()
                    // if( v==ULLONG_MAX && errno==ERANGE )
                    errno = 0;
                    v = ::strtoull( args[2].c_str(), 0, 0 );
                    if( errno==ERANGE )
                        throw xlrexception("start-byte# is out-of-range");
                    startpp.Addr = v;
                } else {
                    // get current playpointer
                    startpp = rte.pp_current;
                }
                // ROT given? (if yes AND >0.0 => delayed play)
                if( args.size()>3 && !args[3].empty() ) {
                    threadmap_type::iterator   thrdmapptr;

                    rot = ::strtod( args[3].c_str(), 0 );

                    // only allow if >0.0 AND taskid!=invalid_taskid
                    ASSERT_COND( (rot>0.0 && rte.current_taskid!=runtime::invalid_taskid) );

                    // And there should NOT already be a delayed-play entry for
                    // the current 'runtime'
                    thrdmapptr = delay_play_map.find( &rte );
                    ASSERT2_COND( (thrdmapptr==delay_play_map.end()),
                                  SCINFO("Internal error: an entry for the current rte "
                                         "already exists in the delay-play-map.") );
                }

                // Good - independent of delayed or immediate play, we have to set up
                // the Streamstor device the same.
                XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRBindInputChannel(ss, 0) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );

                // we create the thread always - an immediate play
                // command is a delayed-play with a delay of zero ...
                // afterwards we do bookkeeping.
                sigset_t       oss, nss;
                pthread_t      dplayid;
                dplay_args     thrdargs;
                pthread_attr_t tattr;

                // prepare the threadargument
                thrdargs.rot      = rot;
                thrdargs.rteptr   = &rte;
                thrdargs.pp_start = startpp;

                // reset statistics counters
                rte.statistics.clear();

                // set up for a detached thread with ALL signals blocked
                ASSERT_ZERO( sigfillset(&nss) );
                PTHREAD_CALL( ::pthread_attr_init(&tattr) );
                PTHREAD_CALL( ::pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE) );
                PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &nss, &oss) );
                PTHREAD_CALL( ::pthread_create(&dplayid, &tattr, delayed_play_fn, &thrdargs) );
                // good. put back old sigmask + clean up resources
                PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &oss, 0) );
                PTHREAD_CALL( ::pthread_attr_destroy(&tattr) );

                // save the threadid in the mapping.
                // play=off will clean it
                std::pair<threadmap_type::iterator, bool> insres;
                insres = delay_play_map.insert( make_pair(&rte, dplayid) );
                ASSERT2_COND(insres.second==true, SCINFO("Failed to insert threadid into map?!"));

                // Update running status:
                rte.transfermode = disk2out;
                rte.transfersubmode.clr_all();

                // deping on immediate or delayed playing:
                rte.transfersubmode.set( (rot>0.0)?(wait_flag):(run_flag) );

                // and form response [if delayed play => return code is '1'
                // i.s.o. '0']
                reply << " " << ((rot>0.0)?1:0) << " ;";
            } else {
                // already doing it!
                reply << " 6 : already ";
                if( rte.transfersubmode&wait_flag )
                    reply << " waiting ";
                else
                    reply << " playing ";
                reply << ";";
            }
        }
        //  play=off [: <playpointer>]
        //  cancels delayed play/stops playback
        if( args[1]=="off" ) {
            recognized = true;
            if( rte.transfermode==disk2out ) {
                SSHANDLE                 sshandle( rte.xlrdev.sshandle() );
                threadmap_type::iterator thrdmapptr;

                // okiedokie, cancel & join the thread (if any)
                thrdmapptr = delay_play_map.find( &rte );
                if( thrdmapptr!=delay_play_map.end() ) {
                    // check if thread still there and cancel it if yes.
                    // NOTE: no auto-throwing on error as the
                    // thread may already have gone away.
                    if( ::pthread_kill(thrdmapptr->second, 0)==0 )
                        ::pthread_cancel(thrdmapptr->second);
                    // now join the thread
                    PTHREAD_CALL( ::pthread_join(thrdmapptr->second, 0) );

                    // and remove the current dplay_map entry
                    delay_play_map.erase( thrdmapptr );
                }
                // somehow we must call stop twice if the
                // device is actually playing
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(sshandle) );
                XLRCALL( ::XLRStop(sshandle) );

                // Update the current playpointer
                rte.pp_current += ::XLRGetPlayLength(sshandle);

                // return to idle status
                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;
                reply << " 0 ;";
            } else {
                // nothing to stop!
                reply << " 4 : inactive ;";
            }
            // irrespective of what we were doing, if the user said
            // play = off : <playpointer>  we MUST update our current
            // playpointer to <playpointer>. This is, allegedly, the only
            // way to force the system to to data_check at a given position.
            if( args.size()>2 && !args[2].empty() ) {
                uint64_t v;

                // kludge to get around missin ULLONG_MAX missing.
                // set errno to 0 first and see if it got set to ERANGE after
                // call to strtoull()
                // if( v==ULLONG_MAX && errno==ERANGE )
                errno = 0;
                v = ::strtoull( args[2].c_str(), 0, 0 );
                if( errno==ERANGE )
                    throw xlrexception("start-byte# is out-of-range");
                rte.pp_current = v;
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }

    return reply.str();
}

// set/query the taskid
string task_id_fn(bool qry, const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        const unsigned int tid = rte.current_taskid;

        reply << " 0 : ";
        if( tid==runtime::invalid_taskid )
            reply << "none";
        else 
            reply << tid;
        reply << " ;";
        return reply.str();
    }

    // check if argument given and if we're not doing anything
    if( args.size()<2 ) {
        reply << " 8 : no taskid given ;";
        return reply.str();
    }

    if( rte.transfermode!=no_transfer ) {
        reply << " 6 : cannot set/change taskid during " << rte.transfermode << " ;";
        return reply.str();
    }

    // Gr8! now we can set the actual taskid
    if( args[1]=="none" )
        rte.current_taskid = runtime::invalid_taskid;
    else
        rte.current_taskid = (unsigned int)::strtol(args[1].c_str(), 0, 0);
    reply << " 0 ;";

    return reply.str();
}

// query the current constraints - only available as query
string constraints_fn(bool , const vector<string>& args, runtime& rte) {
    // automatic variables
    ostringstream    reply;

    reply << "!" << args[0] << "= 0 : "
          << rte.ntrack() << "tr : " << rte.trackformat() << " : " << rte.trackbitrate() << "bps/tr : "
          << rte.sizes << " ;";
    return reply.str();
}


unsigned int bufarg_getbufsize(chain* c, chain::stepid s) {
    return c->communicate(s, &buffererargs::get_bufsize);
}

// set up net2out and net2disk
string net2out_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // This points to the scan being recorded, if any
    static ScanPointer             scanptr;    
    static UserDirectory           ud;
    static per_runtime<string>     hosts;
    static per_runtime<curry_type> oldthunk;

    // automatic variables
    bool                atm; // acceptable transfer mode
    const bool          is_mk5a( rte.ioboard.hardware() & ioboard_type::mk5a_flag );
    const bool          disk( args[0]=="net2disk" );
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer ||
           (disk && ctm==net2disk) ||
           (!disk && ctm==net2out));

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            if( rte.transfersubmode&run_flag )
                reply << "active";
            else if( rte.transfersubmode&wait_flag )
                reply << "waiting";
            else
                reply << rte.transfersubmode;
            reply << " : " << 0 /*rte.nbyte_from_mem*/;
        }
        // this displays the flags that are set, in HRF
        //reply << " : " << rte.transfersubmode;
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <open>
        if( args[1]=="open" ) {
            recognized = true;
            // if transfermode is already net2out, we ARE already doing this
            // (only net2out::close clears the mode to doing nothing)
            // Supports 'rtcp' now [reverse tcp: receiver initiates connection
            // rather than sender, which is the default. Usefull for bypassing
            // firewalls enzow].
            // Multicast is detected by the condition:
            //   rte.lasthost == of the multicast persuasion.
            //
            // Note: net2out=open supports an optional argument: the ipnr.
            // Which is either the host to connect to (if rtcp) or a 
            // multicast ip-address which will be joined.
            //
            // net2out=open[:<ipnr>][:<nbytes>]
            //   net2out=open;        // sets up receiving socket based on net_protocol
            //   net2out=open:<ipnr>; // implies either 'rtcp' if net_proto==rtcp,
            //        connects to <ipnr>. If netproto!=rtcp, sets up receiving socket to
            //        join multicast group <ipnr>, if <ipnr> is multicast
            //   <nbytes> : optional 3rd argument. if set and >0 it
            //              indicates the amount of bytes that will
            //              be buffered in memory before data will be
            //              passed further downstream
            //
            // net2disk MUST have a scanname and may have an optional
            // ipaddress for rtcp or connecting to a multicast group:
            //    net2disk=open:<scanname>[:<ipnr>]
            if( rte.transfermode==no_transfer ) {
                chain                   c;
                SSHANDLE                ss( rte.xlrdev.sshandle() );
                const string            nbyte_str( OPTARG(3, args) );
                const headersearch_type dataformat(rte.trackformat(), rte.ntrack());

                // If we're doing net2out on a Mark5B(+) we
                // cannot accept Mark4/VLBA data.
                // A Mark5A+ can accept Mark5B data ("mark5a+ mode")
                if( !disk && !is_mk5a )  {
                    ASSERT2_COND(rte.trackformat()==fmt_mark5b,
                                 SCINFO("net2out on Mark5B can only accept Mark5B data"));
                }

                // Constrain the transfer sizes based on the three basic
                // parameters
                rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

                // depending on disk or out, the 2nd arg is optional or not
                if( disk && (args.size()<3 || args[2].empty()) )
                    THROW_EZEXCEPT(cmdexception, " no scanname given");

                // save the current host and clear the value.
                // we may write our own value in there (optional 2nd parameter)
                // but most of the times it must be empty. 
                // getsok() uses that value to ::bind() to if it's
                // non-empty. For us that's only important if it's a
                // multicast we want to receive.
                // we'll put the original value back later.
                hosts[&rte] = rte.netparms.host;
                rte.netparms.host.clear();

                // pick up optional ip-address, if given.
                if( (!disk && args.size()>2) || (disk && args.size()>3) )
                    rte.netparms.host = args[(disk?3:2)];


                // also, if writing to disk, we should ascertain that
                // the disks are ready-to-go
                if( disk ) {
                    S_DIR         disk_dir;
                    SSHANDLE      ss( rte.xlrdev.sshandle() );
                    S_DEVINFO     devInfo;

                    // Verify that there are disks on which we *can*
                    // record!
                    XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                    ASSERT_COND( devInfo.NumDrives>0 );

                    // and they're not full or writeprotected
                    XLRCALL( ::XLRGetDirectory(ss, &disk_dir) );
                    ASSERT_COND( !(disk_dir.Full || disk_dir.WriteProtected) );
                }

                // Start building the chain

                // Read from network
                c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                                   &close_filedescriptor);

                // if necessary, decompress
                if( rte.solution )
                    c.add(&blockdecompressor, 10, &rte);

                // optionally buffer
                // for net2out we may optionally have to buffer 
                // an amount of bytes. Check if <nbytes> is
                // set and >0
                //  note: (!a && !b) <=> !(a || b)
                if( !(disk || nbyte_str.empty()) ) {
                    char*         eocptr;
                    unsigned long b;
                    chain::stepid stepid;

                    // strtoul(3)
                    //   * before calling, set errno=0
                    //   -> result == ULONG_MAX + errno == ERANGE
                    //        => input value too big
                    //   -> result == 0 + errno == EINVAL
                    //        => no conversion whatsoever
                    // !(a && b) <=> (!a || !b)
                    errno = 0;
                    b     = ::strtoul(nbyte_str.c_str(), &eocptr, 0);
                    ASSERT2_COND( (b!=ULONG_MAX || errno!=ERANGE) &&
                                  (b!=0         || eocptr!=nbyte_str.c_str()) &&
                                  b>0 && b<UINT_MAX,
                                  SCINFO("Invalid amount of bytes " << nbyte_str << " (1 .. " << UINT_MAX << ")") );

                    // We now know that 'b' has a sane value 
                    stepid = c.add(&bufferer, 10, buffererargs(&rte, (unsigned int)b));

                    // Now install a 'get_buffer_size()' thunk in the rte
                    // We store the previous one so's we can put it back
                    // when we're done.
                    oldthunk[&rte] = rte.set_bufsizegetter(
                         makethunk(&bufarg_getbufsize, stepid)
                            );
                }

                // and write the result
                c.add(fifowriter, &rte);
                // done :-)

                // switch on recordclock, not necessary for net2disk
                if( !disk && is_mk5a )
                    rte.ioboard[ mk5areg::notClock ] = 0;

                // now program the streamstor to record from PCI -> FPDP
                XLRCALL( ::XLRSetMode(ss, (disk?SS_MODE_SINGLE_CHANNEL:SS_MODE_PASSTHRU)) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_PCI) );

                // program where the output should go
                if( disk ) {
                    // must prepare the userdir
                    ud  = UserDirectory( rte.xlrdev );
                    ScanDir& scandir( ud.scanDir() );

                    scanptr = scandir.getNextScan();

                    // new recording starts at end of current recording
                    // note: we have already ascertained that:
                    // disk => args[2] exists && !args[2].empy()
                    scanptr.name( args[2] );
                    scanptr.start( ::XLRGetLength(ss) );
                    scanptr.length( 0 );

                    // write the userdirectory
                    ud.write( rte.xlrdev );

                    // and start the recording
                    XLRCALL( ::XLRAppend(ss) );
                } else {
                    XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_FPDP_TOP) );
                    XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                    XLRCALL( ::XLRSetFPDPMode(ss, SS_FPDP_XMIT, 0) );
                    XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 1) );
                }


                rte.transfersubmode.clr_all();
                // reset statistics counters
                rte.statistics.clear();

                // Update global transferstatus variables to
                // indicate what we're doing
                // Do this before we actually run the chain - something may
                // go wrong and we must cleanup later
                rte.transfermode    = (disk?net2disk:net2out);

                // install and run the chain
                rte.processingchain = c;
                rte.processingchain.run();

                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        // <close>
        if( args[1]=="close" ) {
            recognized = true;

            // only accept this command if we're active
            // ['atm', acceptable transfermode has already been ascertained]
            if( rte.transfermode!=no_transfer ) {
                // switch off recordclock (if not disk)
                if( !disk && is_mk5a )
                    rte.ioboard[ mk5areg::notClock ] = 1;
                // Ok. stop the threads
                rte.processingchain.stop();

                // And tell the streamstor to stop recording
                // Note: since we call XLRecord() we MUST call
                //       XLRStop() twice, once to stop recording
                //       and once to, well, generically stop
                //       whatever it's doing
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                // Update bookkeeping in case of net2disk
                if( disk ) {
                    S_DIR    diskDir;
                    ScanDir& sdir( ud.scanDir() );

                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &diskDir) );
               
                    // Note: appendlength is the amount of bytes 
                    // appended to the existing recording using
                    // XLRAppend().
                    scanptr.length( diskDir.AppendLength );

                    // update the record-pointer
                    sdir.recordPointer( diskDir.Length );

                    // and update on disk
                    ud.write( rte.xlrdev );
                }
                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;

                // put back original host and bufsizegetter
                rte.netparms.host = hosts[&rte];

                if( oldthunk.hasData(&rte) ) {
                    rte.set_bufsizegetter( oldthunk[&rte] );
                    oldthunk.erase( &rte );
                }

                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing " << args[0] << " yet ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}


string net2file_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember previous host setting
    static per_runtime<string> hosts;
    // automatic variables
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==net2file);

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            reply << "active : " << 0 /*rte.nbyte_from_mem*/;
        }
        // this displays the flags that are set, in HRF
        //reply << " : " << rte.transfersubmode;
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // open : <filename> [: <strict> ]
        //   <strict>: if given, it must be "1" to be recognized
        //      "1": IF a trackformat is set (via the "mode=" command)
        //           then the (when necessary, decompressed) datastream 
        //           will be run through a filter which ONLY lets through
        //           frames of the datatype indicated by the mode.
        //   <extrastrict>: if given it must be "0"
        //           this makes the framechecking less strict by
        //           forcing only a match on the syncword. By default it is
        //           on, you can only turn it off. 
        //       
        //       default <strict> = 0
        //           (false/not strict/no filtering/blind dump-to-disk)
        //
        if( args[1]=="open" ) {
            recognized = true;
            if( rte.transfermode==no_transfer ) {
                bool                    strict( false );
                chain                   c;
                const string            filename( OPTARG(2, args) );
                const string            strictarg( OPTARG(3, args) ); 
                const string            proto( rte.netparms.get_protocol() );
                
                // these arguments MUST be given
                ASSERT_COND( filename.empty()==false );

                // We could replace this with
                //  strict = (strictarg=="1")
                // but then the user would not know if his/her value of
                // strict was actually used. better to cry out loud
                // if we didn't recognize the value
                if( strictarg.size()>0 ) {
                    ASSERT2_COND(strictarg=="1", SCINFO("<strict>, when set, MUST be 1"));
                    strict = true;
                }

                // Conflicting request: at the moment we cannot support
                // strict mode on reading compressed Mk4/VLBA data; bits of
                // the syncword will also be compressed and hence, after 
                // decompression, the syncword will contain '0's rather
                // than all '1's, making the framesearcher useless
                ASSERT2_COND( !strict || (strict && !(rte.solution && (rte.trackformat()==fmt_mark4 || rte.trackformat()==fmt_vlba))),
                              SCINFO("Currently we cannot have strict mode with compressed Mk4/VLBA data") );

                // Now that we have all commandline arguments parsed we may
                // construct our headersearcher
                const headersearch_type dataformat(rte.trackformat(), rte.ntrack());

                // set read/write and blocksizes based on parameters,
                // dataformats and compression
                rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

                // Start building the chain
                // clear lasthost so it won't bother the "getsok()" which
                // will, when the net_server is created, use the values in
                // netparms to decide what to do.
                // Also register cancellationfunctions that will close the
                // network and file filedescriptors and notify the threads
                // that it has done so - the threads pick up this signal and
                // terminate in a controlled fashion
                hosts[&rte] = rte.netparms.host;
                rte.netparms.host.clear();

                // Add a step to the chain (c.add(..)) and register a
                // cleanup function for that step, in one go
                c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                                   &close_filedescriptor);

                // Insert a decompressor if needed
                if( rte.solution )
                    c.add(&blockdecompressor, 10, &rte);

                // Insert a framesearcher, if strict mode is requested
                // AND there is a dataformat to look for ...
                if( strict && dataformat ) {
                    c.add(&framer, 10, framerargs(dataformat, &rte));
                    // only pass on the binary form of the frame
                    c.add(&frame2block, 3);
                }

                // And write into a file
                c.register_cancel( c.add(&fdwriter,  &open_file, filename, &rte),
                                   &close_filedescriptor);

                // reset statistics counters
                rte.statistics.clear();
                rte.transfersubmode.clr_all().set( wait_flag );

                rte.transfermode    = net2file;
                rte.processingchain = c;
                rte.processingchain.run();

                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        } else if( args[1]=="close" ) {
            recognized = true;
            if( rte.transfermode!=no_transfer ) {
                // Ok. stop the threads
                rte.processingchain.stop();
                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;

                // put back original host
                rte.netparms.host = hosts[&rte];

                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing " << args[0] << " yet ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}

string net2check_fn(bool qry, const vector<string>& args, runtime& rte ) {
    // remember previous host setting
    static per_runtime<string> hosts;
    // automatic variables
    bool                atm; // acceptable transfer mode
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==net2check);

    // If we aren't doing anything nor doing net2out - we shouldn't be here!
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive : 0";
        } else {
            reply << "active : 0";
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // open [: [<start>] : <inc> ]
        //    initialize the fillpattern start value with <start>
        //    and increment <inc> [64bit numbers!]
        //
        //    Defaults are:
        //      <start> = 0x1122334411223344
        //      <int>   = 0
        //
        if( args[1]=="open" ) {
            recognized = true;
            if( rte.transfermode==no_transfer ) {
                char*                   eocptr;
                chain                   c;
                fillpatargs             fpargs(&rte);
                const string            start_s( OPTARG(2, args) );
                const string            inc_s( OPTARG(3, args) );
                const headersearch_type dataformat(rte.trackformat(), rte.ntrack());

                if( start_s.empty()==false ) {
                    fpargs.fill = ::strtoull(start_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.fill==0 && eocptr==start_s.c_str()) && !(fpargs.fill==~((uint64_t)0) && errno==ERANGE),
                            SCINFO("Failed to parse 'start' value") );
                }
                if( inc_s.empty()==false ) {
                    fpargs.inc = ::strtoull(inc_s.c_str(), &eocptr, 0);
                    // !(A || B) => !A && !B
                    ASSERT2_COND( !(fpargs.inc==0 && eocptr==start_s.c_str()) && !(fpargs.inc==~((uint64_t)0) && errno==ERANGE),
                            SCINFO("Failed to parse 'inc' value") );
                }
                
                // set read/write and blocksizes based on parameters,
                // dataformats and compression
                rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

                // Start building the chain
                // clear lasthost so it won't bother the "getsok()" which
                // will, when the net_server is created, use the values in
                // netparms to decide what to do.
                // Also register cancellationfunctions that will close the
                // network and file filedescriptors and notify the threads
                // that it has done so - the threads pick up this signal and
                // terminate in a controlled fashion
                hosts[&rte] = rte.netparms.host;
                rte.netparms.host.clear();

                // Add a step to the chain (c.add(..)) and register a
                // cleanup function for that step, in one go
                c.register_cancel( c.add(&netreader, 32, &net_server, networkargs(&rte)),
                                   &close_filedescriptor);

                // Insert a decompressor if needed
                if( rte.solution )
                    c.add(&blockdecompressor, 10, &rte);

                // And write to the checker
                c.add(&checker, fpargs);

                // reset statistics counters
                rte.statistics.clear();
                rte.transfersubmode.clr_all().set( wait_flag );

                rte.transfermode    = net2check;
                rte.processingchain = c;
                rte.processingchain.run();

                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        } else if( args[1]=="close" ) {
            recognized = true;
            if( rte.transfermode!=no_transfer ) {
                // Ok. stop the threads
                rte.processingchain.stop();
                rte.transfersubmode.clr_all();
                rte.transfermode = no_transfer;

                // put back original host
                rte.netparms.host = hosts[&rte];

                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing " << args[0] << " yet ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}

// obviously, these numbers are chosen completely at random ...
enum hwtype { mark5a = 666, mark5b = 42 };


// Abstract out the phases of an in2{net|fork} command into setup, start,
// pause, resume and stop. If you instantiate an actual "in2net_transfer"
// for a type which is not specialized below (as it will be for "mark5a" and
// "mark5b" (see the enum just above this text)) you will have exceptions
// thrown when trying to access any of them.
template <unsigned int _Blah>
struct in2net_transfer {
    static void setup(runtime&) {
        throw cmdexception("in2net_transfer::setup not defined for this hardware!");
    }
    static void start(runtime&) {
        throw cmdexception("in2net_transfer::start not defined for this hardware!");
    }
    static void pause(runtime&) {
        throw cmdexception("in2net_transfer::pause not defined for this hardware!");
    }
    static void resume(runtime&) {
        throw cmdexception("in2net_transfer::resume not defined for this hardware!");
    }
    static void stop(runtime&) {
        throw cmdexception("in2net_transfer::stop not defined for this hardware!");
    }
};

// Now make specializations which do the Right Thing (tm) for the indicated
// hardware. 


// For the old Mark5A and Mark5A+
template <>
struct in2net_transfer<mark5a> {
    static void setup(runtime& rte) {
        // switch off clock
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2,"setup: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
        notclock = 1;
        DEBUG(2,"setup: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
    }

    // start/resume the recordclock. for Mark5A they are the same
    static void start(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2, "in2net_transfer<mark5a>=on: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
        notclock = 0;
        suspendf  = 0;
        DEBUG(2, "in2net_transfer<mark5a>=on: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
    }
    static void resume(runtime& rte) {
        start(rte);
    }

    static void pause(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];
        ioboard_type::mk5aregpointer  suspendf = rte.ioboard[ mk5areg::SF ];

        DEBUG(2, "in2net_transfer<mark5a>=pause: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
        notclock = 1;
        DEBUG(2, "in2net_transfer<mark5a>=pause: notclock: " << hex_t(*notclock)
                << " SF: " << hex_t(*suspendf) << endl);
    }
    static void stop(runtime& rte) {
        ioboard_type::mk5aregpointer  notclock = rte.ioboard[ mk5areg::notClock ];

        // turn off clock
        DEBUG(2, "in2net_transfer<mark5a>=stop: notclock: " << hex_t(*notclock) << endl);
        notclock = 1;
        DEBUG(2, "in2net_transfer<mark5a>=stop: notclock: " << hex_t(*notclock) << endl);
    }
};

// For Mark5B/DIM and Mark5B+/DIM
template <>
struct in2net_transfer<mark5b> {
    static void setup(runtime&) {
        DEBUG(2, "in2net_transfer<mark5b>=setup" << endl);
    }
    static void start(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=start" << endl);
        start_mk5b_dfhg( rte );
    }
    // start/resume the recordclock
    static void resume(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=resume" << endl);
        // Good. Unpause the DIM. Will restart datatransfer on next 1PPS
        rte.ioboard[ mk5breg::DIM_PAUSE ] = 0;
    }

    static void pause(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=pause" << endl);
        // Good. Unpause the DIM. Will restart datatransfer on next 1PPS
        rte.ioboard[ mk5breg::DIM_PAUSE ] = 1;
    }
    static void stop(runtime& rte) {
        DEBUG(2, "in2net_transfer<mark5b>=stop" << endl);
        rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;
    }
};

// A templated "in2net" function (which can also be called as in2fork or
// in2file).
// Since the steps and states an in2net/in2fork/in2file transfer must go through/can
// be in are identical on both mark5a/mark5b it makes sense to abstract that
// out. The only thing they differ in is in which registers in the IOBoard
// they access/address in order to make the transfer start/stop/resume etc.
//
// It is templated on the actual mark5 for which this function applies (see
// the in2net_transfer<> just above this).
// You may obtain a function pointer to an instantiated function (this is,
// after all, a template) by:
//     &in2net_fn<[hardwareenum]>;
// e.g.:
//   
//     fptr = &in2net_fn<mark5a>;
//
// The actual transfers:
//
//  in2net=connect:<ip|host> [ : <strict> ]
//       initiate a networktransfer to the mentioned ip|host 
//
//  in2fork=connect:<ip|host>:<scanname> [ : <strict> ]
//       initiate networktransfer to ip|host AND prepare for recording to
//       local disk, adding a new scan named <scanname> to the UserDirectory
//       on the disk
//
//  in2file=connect:/path/to/some/file,<openmode> [ : <strict> ]
//       with <openmode>:
//          w   truncate file, create if not exist yet
//          a   append to file, create if not exist yet
//
//       IF a dataformat is set AND compression is requested, THEN a
//       framesearcher is inserted into the streamprocessor unconditionally.
//       The framesearcher only checks for the appearance
//       of the syncword of the expected dataformat 
//
//
//  NOTE NOTE NOTE NOTE NOTE NOTE
//
//    when running in in2fork mode the recording mode is slightly different:
//    WRAP_ENABLE is off (in2net=>WRAP_ENABLE=on). what this means is that
//    in2net can run forever (it wraps) but in2fork does NOT since if
//    WRAP_ENABLE == true and writing to disk == true, then the disk will be
//    overwritten when it's full since it'll continue recording at the beginning
//    of the disk.
template <unsigned int Mark5>
string in2net_fn( bool qry, const vector<string>& args, runtime& rte ) {
    typedef std::map<runtime*, chain::stepid> fifostepmap_type;

    // needed for diskrecording - need to remember across fn calls
    static ScanPointer                scanptr;
    static UserDirectory              ud;
    static per_runtime<chain::stepid> fifostep;

    // automatic variables
    bool                atm; // acceptable transfer mode
    const bool          fork( args[0]=="in2fork" );
    const bool          tonet( args[0]=="in2net" );
    const bool          tofile( args[0]=="in2file" );
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Test if the current transfermode is acceptable for this
    // function: either doing nothing, in2fork or in2net
    // (and depending on 'fork' or not we accept those)
    atm = (ctm==no_transfer ||
           (tonet && ctm==in2net) ||
           (tofile && ctm==in2file) ||
           (fork && ctm==in2fork) );

    // good, if we shouldn't even be here, get out
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.netparms.host << (fork?"f":"") << " : " << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        if( args[1]=="connect" ) {
            recognized = true;
            // if transfermode is already in2{net|fork}, we ARE already connected
            // (only in2{net|fork}::disconnect clears the mode to doing nothing)
            if( rte.transfermode==no_transfer ) {
                chain                   c;
                string                  filename;
                SSHANDLE                ss      = rte.xlrdev.sshandle();
                const bool              rtcp    = (rte.netparms.get_protocol()=="rtcp");

                const headersearch_type dataformat(rte.trackformat(), rte.ntrack());

                // good. pick up optional hostname/ip to connect to
                // unless it's rtcp
                if( fork || tonet ) {
                    if( args.size()>2 && !args[2].empty() ) {
                        if( !rtcp )
                            rte.netparms.host = args[2];
                        else
                            DEBUG(0, args[0] << ": WARN! Ignoring supplied host '" << args[2] << "'!" << endl);
                    }
                } else if( tofile ) {
                    filename = OPTARG(2, args);
                    ASSERT2_COND( filename.empty()==false, SCINFO("in2file MUST have a filename as argument"));
                }

                // in2fork requires extra arg: the scanname
                // NOTE: will throw up if none given!
                // Also perform some extra sanity checks needed
                // for disk-recording
                if( fork ) {
                    S_DIR         disk;
                    S_DEVINFO     devInfo;

                    if(args.size()<=3 || args[3].empty())
                        THROW_EZEXCEPT(cmdexception, "No scannanme given for in2fork!");

                    // Verify that there are disks on which we *can*
                    // record!
                    XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                    ASSERT_COND( devInfo.NumDrives>0 );

                    // and they're not full or writeprotected
                    XLRCALL( ::XLRGetDirectory(ss, &disk) );
                    ASSERT_COND( !(disk.Full || disk.WriteProtected) );
                } 

                in2net_transfer<Mark5>::setup(rte);

                // now program the streamstor to record from FPDP -> PCI
                XLRCALL( ::XLRSetMode(ss, (fork?SS_MODE_FORK:SS_MODE_PASSTHRU)) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_PCI) );
                XLRCALL( ::XLRBindOutputChannel(ss, CHANNEL_PCI) );

                // Check. Now program the FPDP channel
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );

                // Code courtesy of Cindy Gold of Conduant Corp.
                //   Have to distinguish between old boards and 
                //   new ones (most notably the Amazon based boards)
                //   (which are used in Mark5B+ and Mark5C)
                UINT     u32recvMode, u32recvOpt;

                if( rte.xlrdev.boardGeneration()<4 ) {
                    // This is either a XF2/V100/VXF2
                    u32recvMode = SS_FPDP_RECVMASTER;
                    u32recvOpt  = SS_OPT_FPDPNRASSERT;
                } else {
                    // Amazon or Amazon/Express
                    u32recvMode = SS_FPDPMODE_RECVM;
                    u32recvOpt  = SS_DBOPT_FPDPNRASSERT;
                }
                XLRCALL( ::XLRSetDBMode(ss, u32recvMode, u32recvOpt) );

                // Start the recording. depending or fork or !fork
                // we have to:
                // * update the scandir on the discpack (if fork'ing)
                // * call a different form of 'start recording'
                //   to make sure that disken are not overwritten
                if( fork ) {
                    ud  = UserDirectory( rte.xlrdev );
                    ScanDir& scandir( ud.scanDir() );

                    scanptr = scandir.getNextScan();

                    // new recording starts at end of current recording
                    scanptr.name( args[3] );
                    scanptr.start( ::XLRGetLength(ss) );
                    scanptr.length( 0 );

                    // write the userdirectory
                    ud.write( rte.xlrdev );

                    // when fork'ing we do not attempt to record for ever
                    // (WRAP_ENABLE==1) otherwise the disken could be overwritten
                    XLRCALL( ::XLRAppend(ss) );
                } else {
                    // in2net can run indefinitely
                    // 18/Mar/2011 - As per communication with Cindy Gold
                    //               of Conduant Corp. (the manuf. of the
                    //               Mark5-en) MODE_PASSTHRU should imply
                    //               WRAP_ENABLE==false. Or rather:
                    //               the wording was "wrap-enable was never
                    //               meant to apply to nor tested in
                    //               passthru mode"
                    XLRCALL( ::XLRRecord(ss, XLR_WRAP_DISABLE/*XLR_WRAP_ENABLE*/, 1) );
                }

                // constrain sizes based on network parameters and optional
                // compression. If this is the Mark5A version of in2{net|fork}
                // it can only yield mark4/vlba data and for these
                // formats the framesize/offset is irrelevant for
                // compression since each individual bitstream has full
                // headerinformation.
                // If, otoh, we're running on a mark5b we must look for
                // frames first and compress those.
                rte.sizes = constrain(rte.netparms, dataformat, rte.solution);

                // come up with a theoretical ipd
                compute_theoretical_ipd(rte);

                // The hardware has been configured, now start building
                // the processingchain.
                fifostep[&rte] = c.add(&fiforeader, 10, fiforeaderargs(&rte));

                // If compression requested then insert that step now
                if( rte.solution ) {
                    // In order to prevent bitshift (when the datastream
                    // does not start exactly at the start of a dataframe)
                    // within a dataframe (leading to throwing away the
                    // wrong bitstream upon compression) we MUST always
                    // insert a framesearcher.
                    // This guarantees that only intact frames are sent
                    // to the compressor AND the compressor knows exactly
                    // where all the bits of the bitstreams are
                    compressorargs cargs( &rte );

                    DEBUG(0, "in2net: enabling compressor " << dataformat << endl);
                    if( dataformat ) {
                        c.add(&framer, 10, framerargs(dataformat, &rte));
                        c.add(&framecompressor, 10, compressorargs(&rte));
                    } else {
                        c.add(&blockcompressor, 10, &rte);
                    }
                }

                // Write to file or to network
                if( tofile ) {
                    c.register_cancel(c.add(&fdwriter, &open_file, filename, &rte),
                                      &close_filedescriptor);
                } else {
                    // and finally write to the network
                    c.register_cancel(c.add(&netwriter, &net_client, networkargs(&rte)),
                                      &close_filedescriptor);
                }

                rte.transfersubmode.clr_all();
                // reset statistics counters
                rte.statistics.clear();

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.transfermode    = (fork?in2fork:(tofile?in2file:in2net));

                // The very last thing we do is to start the
                // system - running the chain may throw up and we shouldn't
                // be in an indefinite state
                rte.processingchain = c;
                rte.processingchain.run();

                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        // <on> : turn on dataflow
        if( args[1]=="on" ) {
            recognized = true;
            // only allow if transfermode==in2{net|fork} && has the connected flag +
            //   either not started yet (!runflag && !pauseflag) OR
            //   started but paused (runflag && pause)
            if( rte.transfermode!=no_transfer &&
                rte.transfersubmode&connected_flag &&
                ((rte.transfersubmode&run_flag && rte.transfersubmode&pause_flag) ||
                 (!(rte.transfersubmode&run_flag) && !(rte.transfersubmode&pause_flag))) ) {

                // If not running yet, start the transfer.
                // Otherwise we were already running and all we
                // need to do is re-enable the inputclock.
                if( !(rte.transfersubmode&run_flag) ) {
                    in2net_transfer<Mark5>::start(rte);
                    rte.processingchain.communicate(fifostep[&rte], &fiforeaderargs::set_run, true);
                } else {
                    // resume the hardware
                    in2net_transfer<Mark5>::resume(rte);
                }

                // no matter which transfer we were doing, we must clear the
                // pauseflag
                rte.transfersubmode.clr( pause_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer, in2net, or in2fork, nothing else
                if( rte.transfermode!=no_transfer )
                    if( rte.transfersubmode&run_flag )
                        reply << " 6 : already running ;";
                    else
                        reply << " 6 : not yet connected ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        if( args[1]=="off" ) {
            recognized = true;
            // only allow if transfermode=={in2net|in2fork} && submode has the run flag
            if( rte.transfermode!=no_transfer &&
                (rte.transfersubmode&run_flag)==true &&
                (rte.transfersubmode&pause_flag)==false ) {

                // Pause the recording
                in2net_transfer<Mark5>::pause(rte);

                // indicate paused state
                rte.transfersubmode.set( pause_flag );
                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or {in2net|in2fork}, nothing else
                if( rte.transfermode!=no_transfer )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        // <disconnect>
        if( args[1]=="disconnect" ) {
            recognized = true;
            // Only allow if we're doing in2net.
            // Don't care if we were running or not
            if( rte.transfermode!=no_transfer ) {
                // whatever we were doing make sure it's stopped
                in2net_transfer<Mark5>::stop(rte);

                // do a blunt stop. at the sending end we do not care that
                // much processing every last bit still in our buffers
                rte.processingchain.stop();

                // stop the device
                // As per the SS manual need to call 'XLRStop()'
                // twice: once for stopping the recording
                // and once for stopping the device altogether?
                XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(rte.xlrdev.sshandle()) );

                // Need to do bookkeeping if in2fork was active
                if( fork ) {
                    S_DIR    diskDir;
                    ScanDir& sdir( ud.scanDir() );

                    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &diskDir) );
               
                    // Note: appendlength is the amount of bytes 
                    // appended to the existing recording using
                    // XLRAppend().
                    scanptr.length( diskDir.AppendLength );

                    // update the record-pointer
                    sdir.recordPointer( diskDir.Length );

                    // and update on disk
                    ud.write( rte.xlrdev );
                }

                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
                reply << " 0 ;";
            } else {
                reply << " 6 : Not doing " << args[0] << " ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}

string spill2net_fn(bool qry, const vector<string>& args, runtime& rte ) {
    bool                atm;
    ostringstream       reply;
    const transfer_type ctm( rte.transfermode ); // current transfer mode

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    atm = (ctm==no_transfer || ctm==spill2net);

    // good, if we shouldn't even be here, get out
    if( !atm ) {
        reply << " 1 : _something_ is happening and its NOT " << args[0] << "!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.netparms.host << " : " << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    if( args.size()<2 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // <connect>
        if( args[1]=="connect" ) {
            recognized = true;
            if( rte.transfermode==no_transfer ) {
                chain                   c;
                chunkdestmap_type       cdm;
                const headersearch_type dataformat(rte.trackformat(), rte.ntrack());

                DEBUG(2, "spill2net=connect" << endl);
                cdm.insert( make_pair(0, "127.0.0.1@60000") );
                cdm.insert( make_pair(1, "127.0.0.1@60001") );
                rte.sizes = constrain(rte.netparms, dataformat, rte.solution);
                DEBUG(2, "spill2net: constrained sizes = " << rte.sizes << endl);
                c.add( &framepatterngenerator, 10, fillpatargs(&rte) );
                c.add( &framer, 10, framerargs(dataformat, &rte) );
                c.add( &splitter, 10, splitterargs(&rte) );
                c.add( &multinetwriter, &multiopener, multidestparms(&rte, cdm) );

                rte.processingchain = c;
                DEBUG(2, "spill2net: starting to run" << endl);
                rte.processingchain.run();
                DEBUG(2, "spill2net running" << endl);
                rte.transfermode    = spill2net;
                rte.transfersubmode.clr_all().set(connected_flag).set(wait_flag);
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        } else if( args[1]=="on" ) {
            recognized = true;
            if( rte.transfermode==spill2net ) {
                if( ((rte.transfersubmode&run_flag)==false) ) {
                    // turn on the dataflow
                    rte.processingchain.communicate(0, &fillpatargs::set_run, true);
                    recognized = true;
                    rte.transfersubmode.clr(wait_flag).set(run_flag);
                    reply << " 0 ;";
                } else {
                    reply << " 6 : already running ;";
                }
            } else {
                reply << " 6 : not doing spill2net ;";
            }
        } else if( args[1]=="disconnect" ) {
            recognized = true;
            if( rte.transfermode==spill2net ) {
                DEBUG(2, "Stopping spill2net ..." << endl);
                rte.processingchain.stop();
                DEBUG(2, "spill2net disconnected" << endl);
                rte.processingchain = chain();
                recognized = true;
                reply << " 0 ;";
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();
            } else {
                reply << " 6 : not doing spill2net ;";
            }
        }
        
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();

    reply << " 7 : Not implemented as command yet";
    return reply.str();
}

#if 0
string getlength_fn( bool, const vector<string>&, runtime& rte ) {
    ostringstream  reply;
    S_DIR          curDir;

    try {
        UserDirectory  ud( rte.xlrdev );
        const ScanDir& sd( ud.scanDir() );

        for( unsigned int i=0; i<sd.nScans(); ++i ) {
            DEBUG(0, sd[i] << endl);
        }
    }
    catch( ... ) 
    {}

    XLRCALL( ::XLRGetDirectory(rte.xlrdev.sshandle(), &curDir) );
    reply << "!getlength = 0 : L" << curDir.Length << " : AL" << curDir.AppendLength
          << ": XLRGL" << ::XLRGetLength(rte.xlrdev.sshandle()) << " ;";
    return reply.str();
}

string erase_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;
    UserDirectory ud( rte.xlrdev );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
       reply << "0 : " << ((ud==UserDirectory())?(""):("not ")) << "erased ;";
       return reply.str();
    }

    // Ok must be command.
    // Erasen met die hap!
    ud = UserDirectory();
    ud.write( rte.xlrdev );
    XLRCALL( ::XLRErase(rte.xlrdev.sshandle(), SS_OVERWRITE_NONE) );
    reply << " 0;";
    return reply.str();
}

#endif

string mk5a_clock_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream               reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing record - we shouldn't be here!
    if( qry ) {
        reply << " 0 : " << !(rte.ioboard[ mk5areg::notClock ]) << " ;";
        return reply.str();
    }

    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        //rte.ioboard[ mk5areg::notClock ] = (args[1]=="off");
        if( args[1]=="on" ) {
            in2net_transfer<mark5a>::start(rte);
            reply << " 0 ; ";
        } else if (args[1]=="off" ) {
            in2net_transfer<mark5a>::stop(rte);
            reply << " 0 ; ";
        } else  {
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
        }
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}

// Really, in2disk is 'record'. But in lieu of naming conventions ...
// the user won't see this name anyway :)
// Note: do not stick this one in the Mark5B/DOM commandmap :)
// Oh well, you'll get exceptions when trying to execute then
// anyway
string in2disk_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // This points to the scan being recorded, if any
    static UserDirectory    userdir;
    static ScanPointer      curscanptr;    
    // automatic variables
    ostringstream               reply;
    ioboard_type::iobflags_type hardware( rte.ioboard.hardware() );

    // If we're not supposed to be here!
    ASSERT_COND( (hardware&ioboard_type::mk5a_flag || hardware&ioboard_type::dim_flag) );

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // If we aren't doing anything nor doing record - we shouldn't be here!
    if( rte.transfermode!=no_transfer && rte.transfermode!=in2disk ) {
        reply << " 1 : _something_ is happening and its NOT in2disk!!! ;";
        return reply.str();
    }

    // Good. See what the usr wants
    if( qry ) {
        reply << " 0 : ";
        if( rte.transfermode==no_transfer ) {
            reply << "inactive";
        } else {
            reply << rte.transfersubmode;
        }
        reply << " ;";
        return reply.str();
    }

    // Handle commands, if any...
    if( args.size()<=1 ) {
        reply << " 3 : command w/o actual commands and/or arguments... ;";
        return reply.str();
    }

    try {
        bool  recognized = false;
        // record=<on>:<scanlabel>[:[<experiment-name>][:[<station-code]][:[<source>]]
        // so we require at least three elements in args:
        //      args[0] = command itself (record, in2disk, ...)
        //      args[1] = "on"
        //      args[2] = <scanlabel>
        // As per Mark5A.c the optional fields - if any - will be reordered in
        // the name as:
        // experiment_station_scan_source
        if( args[1]=="on" ) {
            ASSERT2_COND( args.size()>=3, SCINFO("not enough parameters to command") );
            recognized = true;
            // if transfermode is already in2disk, we ARE already recording
            // so we disallow that
            if( rte.transfermode==no_transfer ) {
                S_DIR         disk;
                string        scan( args[2] );
                string        experiment( OPTARG(3, args) );
                string        station( OPTARG(4, args) );
                string        source( OPTARG(5, args) );
                string        scanlabel;
                SSHANDLE      ss( rte.xlrdev.sshandle() );
                S_DEVINFO     devInfo;

                // Verify that there are disks on which we *can*
                // record!
                XLRCALL( ::XLRGetDeviceInfo(ss, &devInfo) );
                ASSERT_COND( devInfo.NumDrives>0 );

                // Should check bank-stuff:
                //   * if we are in bank-mode
                //   * if so, if the current bank
                //     is available
                //     and not write-protect0red
                //  ...
                // Actually, the 'XLRGetDirectory()' tells us
                // most of what we want to know!
                // [it knows about banks etc and deals with that
                //  silently]
                XLRCALL( ::XLRGetDirectory(ss, &disk) );
                ASSERT_COND( !(disk.Full || disk.WriteProtected) );

                // construct the scanlabel
                if( !experiment.empty() )
                    scanlabel = experiment;
                if( !station.empty() ) {
                    if( !scanlabel.empty() )
                        station = "_"+station;
                    scanlabel += station;
                }
                if( !scan.empty() ) {
                    if( !scanlabel.empty() )
                        scan = "_"+scan;
                    scanlabel += scan;
                }
                // and finally, optionally, the source
                if( !source.empty() ) {
                    if( !scanlabel.empty() )
                        source = "_"+source;
                    scanlabel += source;
                }
                // Now then. If the scanlabel is *still* empty
                // we give it the value of '+'
                if( scanlabel.empty() )
                    scanlabel = "+";

                // Depending on Mk5A or Mk5B/DIM ...
                // switch off clock (mk5a) or
                // stop the DFH-generator
                if( hardware&ioboard_type::mk5a_flag )
                    rte.ioboard[ mk5areg::notClock ] = 1;
                else
                    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;

                // Already program the streamstor, do not
                // start Recording otherwise we can't read/write
                // the UserDirectory.
                // Let it record from FPDP -> Disk
                XLRCALL( ::XLRSetMode(ss, SS_MODE_SINGLE_CHANNEL) );
                XLRCALL( ::XLRClearChannels(ss) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRBindInputChannel(ss, CHANNEL_FPDP_TOP) );
                XLRCALL( ::XLRSelectChannel(ss, CHANNEL_FPDP_TOP) );
                // HV: Take care of Amazon - as per Conduant's
                //     suggestion
                UINT     u32recvMode, u32recvOpt;

                if( rte.xlrdev.boardGeneration()<4 ) {
                    // This is either a XF2/V100/VXF2
                    u32recvMode = SS_FPDP_RECVMASTER;
                    u32recvOpt  = SS_OPT_FPDPNRASSERT;
                } else {
                    // Amazon or Amazon/Express
                    u32recvMode = SS_FPDPMODE_RECVM;
                    u32recvOpt  = SS_DBOPT_FPDPNRASSERT;
                }
                XLRCALL( ::XLRSetDBMode(ss, u32recvMode, u32recvOpt) );

                // Update the UserDirectory, at least we know the
                // streamstor programmed Ok. Still, a few things could
                // go wrong but we'll leave that for later ...
                userdir  = UserDirectory( rte.xlrdev );
                ScanDir& scandir( userdir.scanDir() );

                curscanptr = scandir.getNextScan();

                // new recording starts at end of current recording
                curscanptr.name( scanlabel );
                curscanptr.start( ::XLRGetLength(ss) );
                curscanptr.length( 0 );

                // write the userdirectory
                userdir.write( rte.xlrdev );

                // Great, now start recording & kick off the I/O board
                //XLRCALL( ::XLRRecord(ss, XLR_WRAP_ENABLE, 0) );
                XLRCALL( ::XLRAppend(ss) );

                if( hardware&ioboard_type::mk5a_flag )
                    rte.ioboard[ mk5areg::notClock ] = 0;
                else
                    start_mk5b_dfhg( rte );

                // Update global transferstatus variables to
                // indicate what we're doing
                rte.statistics.clear();
                rte.transfermode    = in2disk;
                rte.transfersubmode.clr_all();
                // in2disk is running immediately
                rte.transfersubmode |= run_flag;
                reply << " 0 ;";
            } else {
                reply << " 6 : Already doing " << rte.transfermode << " ;";
            }
        }
        if( args[1]=="off" ) {
            recognized = true;
            // only allow if transfermode==in2disk && submode has the run flag
            if( rte.transfermode==in2disk && (rte.transfersubmode&run_flag)==true ) {
                S_DIR    diskDir;
                ScanDir& sdir( userdir.scanDir() );
                SSHANDLE handle( rte.xlrdev.sshandle() );

                // Depending on the actual hardware ...
                // stop transferring from I/O board => streamstor
                if( hardware&ioboard_type::mk5a_flag )
                    rte.ioboard[ mk5areg::notClock ] = 1;
                else
                    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 0;

                // stop the device
                // As per the SS manual need to call 'XLRStop()'
                // twice: once for stopping the recording
                // and once for stopping the device altogether?
                XLRCALL( ::XLRStop(handle) );
                if( rte.transfersubmode&run_flag )
                    XLRCALL( ::XLRStop(handle) );

                // reset global transfermode variables 
                rte.transfermode = no_transfer;
                rte.transfersubmode.clr_all();

                // Need to do bookkeeping
                XLRCALL( ::XLRGetDirectory(handle, &diskDir) );
               
                // Note: appendlength is the amount of bytes 
                // appended to the existing recording using
                // XLRAppend().
                curscanptr.length( diskDir.AppendLength );

                // update the record-pointer
                sdir.recordPointer( diskDir.Length );

                // and update on disk
                userdir.write( rte.xlrdev );

                reply << " 0 ;";
            } else {
                // transfermode is either no_transfer or in2disk, nothing else
                if( rte.transfermode==in2disk )
                    reply << " 6 : already running ;";
                else 
                    reply << " 6 : not doing anything ;";
            }
        }
        if( !recognized )
            reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    }
    catch( const exception& e ) {
        reply << " 4 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 4 : caught unknown exception ;";
    }
    return reply.str();
}


// The 1PPS source command for Mk5B/DIM
string pps_source_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // Pulse-per-second register value in HumanReadableFormat
    static const string pps_hrf[4] = { "none", "altA", "altB", "vsi" };
    const unsigned int  npps( sizeof(pps_hrf)/sizeof(pps_hrf[0]) );
    // variables
    unsigned int                 selpps;
    ostringstream                oss;
    ioboard_type::mk5bregpointer pps = rte.ioboard[ mk5breg::DIM_SELPP ];

    oss << "!" << args[0] << (qry?('?'):('='));
    if( qry ) {
        oss << " 0 : " << pps_hrf[ *pps ] << " ;";
        return oss.str();
    }
    // It was a command. We must have (at least) one argument [the first, actually]
    // and it must be non-empty at that!
    if( args.size()<2 || args[1].empty() ) {
        oss << " 3 : Missing argument to command ;";
        return oss.str();
    }
    // See if we recognize the pps string
    for( selpps=0; selpps<npps; ++selpps )
        if( ::strcasecmp(args[1].c_str(), pps_hrf[selpps].c_str())==0 )
            break;
    if( selpps==npps ) {
        oss << " 4 : Unknown PPS source '" << args[1] << "' ;";
    } else {
        // write the new PPS source into the hardware
        pps = selpps;
        oss << " 0 ;";
    }
    return oss.str();
}

// mtu function
string mtu_fn(bool q, const vector<string>& args, runtime& rte) {
    ostringstream  oss;
    netparms_type& np( rte.netparms );

    oss << "!" << args[0] << (q?('?'):('='));
    if( q ) {
        oss << " 0 : " << np.get_mtu() << " ;";
        return oss.str();
    }
 
    // only allow command when no transfer is running
    if( rte.transfermode!=no_transfer ) {
        oss << " 6 : Not allowed to change during transfer ;";
        return oss.str();
    } 

    // command better have an argument otherwise 
    // it don't mean nothing
    if( args.size()>=2 && args[1].size() ) {
        unsigned int  m = (unsigned int)::strtol(args[1].c_str(), 0, 0);

        np.set_mtu( m );
        oss << " 0 ;";
    } else {
        oss << " 1 : Missing argument to command ;";
    }
    return oss.str();
}

// netstat. Tells (actual) blocksize, mtu and datagramsize
string netstat_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream        oss;

    oss << "!" << args[0] << (q?('?'):('=')) << " = 0";
    oss << " : " << rte.sizes << ";";
    return oss.str();
}

// query only
string tstat_fn(bool, const vector<string>&, runtime& rte ) {
    double                          dt;
    const double                    fifosize( 512 * 1024 * 1024 );
    unsigned long                   fifolen;
    ostringstream                   reply;
    chainstats_type                 current;
    static struct timeb             time_cur;
    static struct timeb*            time_last( 0 );
    static chainstats_type          laststats;
    chainstats_type::const_iterator lastptr, curptr;

    if( rte.transfermode==no_transfer )
        return "!tstat = 0 : no active transfer ; ";

    // must serialize access to the StreamStor
    // (hence the do_xlr_[un]lock();
    do_xlr_lock();
    ftime( &time_cur );
    fifolen = ::XLRGetFIFOLength(rte.xlrdev.sshandle());
    do_xlr_unlock();

    // make a copy of the statistics with the lock on the runtimeenvironment
    // held
    RTEEXEC(rte, current=rte.statistics);

    // Must check if the current transfer matches the saved one - if not we
    // must restart our timing
    for(lastptr=laststats.begin(), curptr=current.begin();
        lastptr!=laststats.end() && curptr!=current.end() &&
            lastptr->first==curptr->first && // check that .first (==stepid) matches
            lastptr->second.stepname==curptr->second.stepname; // check that stepnames match
        lastptr++, curptr++) {};
    // If not both lastptr & curptr point at the end of their respective
    // container we have a mismatch and must start over
    if( !(lastptr==laststats.end() && curptr==current.end()) ) {
        delete time_last;
        time_last = 0;
    }

    if( !time_last ) {
        time_last  = new struct timeb;
        *time_last = time_cur;
    }

    // Compute 'dt'. If it's too small, do not even try to compute rates
    dt = (time_cur.time + time_cur.millitm/1000.0) - 
         (time_last->time + time_last->millitm/1000.0);

    if( dt>0.1 ) {
        double fifolevel    = ((double)fifolen/fifosize) * 100.0;

        reply << "!tstat=0: "
              // dt in seconds
              << format("%5.2lfs", dt) << " ";
        // now, for each step compute the rate. we've already established
        // equivalence making the stop condition simpler
        for(curptr=current.begin(), lastptr=laststats.begin();
            curptr!=current.end(); curptr++, lastptr++) {
            double rate = (((double)(curptr->second.count-lastptr->second.count))/dt)*8.0;
            reply << curptr->second.stepname << " " << sciprintd(rate,"bps") << " ";
        }
        reply << "F" << format("%4.1lf%%", fifolevel) << " ;";
    } else {
        reply << "!tstat = 1 : Retry - we're initialized now : " << rte.transfermode << " ;";
    }

    // Update statics
    *time_last  = time_cur;
    laststats   = current;
    return reply.str();
}

string evlbi_fn(bool, const vector<string>& args, runtime& rte ) {
    ostringstream reply;

    reply << "!" << args[0] << "? 0 : " << rte.evlbi_stats << " ;";
    return reply.str();
}

#if 0
string reset_fn(bool, const vector<string>&, runtime& rte ) {
    rte.reset_ioboard();
    return "!reset = 0 ;";
}
#endif


// specialization for Mark5B/DIM
// We do *not* test for DIM; others should've
// checked for us
// mode={ext|tvg|ramp}:<bitstreammask>[:<decimation ratio>[:<fpdpmode>]]
// fpdpmode not supported by this code.
// We allow 'tvg+<num>' to set a specific tvg mode. See runtime.h for 
// details. Default will map to 'tvg+1' [internal tvg]
string mk5bdim_mode_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream       reply;
    mk5b_inputmode_type curipm;

    // Wether this is command || query, we need the current inputmode
    rte.get_input( curipm );

    // This part of the reply we can already form
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    if( qry ) {
        int    decimation;
        // Decimation = 2^j
        decimation = (int)::round( ::exp(curipm.j * M_LN2) );
        reply << "0 : " << curipm.datasource << " : " << hex_t(curipm.bitstreammask)
              << " : " << decimation << " ;";
        return reply.str();
    }

    // Must be the mode command. Only allow if we're not doing a transfer
    if( rte.transfermode!=no_transfer ) {
        reply << "4: cannot change during " << rte.transfermode << " ;";
        return reply.str();
    }
    // We require at least two non-empty arguments
    // ('data source' and 'bitstreammask')
    // (unless the mode == "none", in which case no ekztra arguments
    //  are req'd)
    if( (args.size()<=1) || /* only the command or nothing at all? that is never any good */
        (args.size()==2 && args[1]!="none") || /* only "mode = none" is acceptable in this case */
        (args.size()==3 && (args[1].empty() || args[2].empty())) /* not two non-empty arguments */
      ) {
        reply << "3: must have at least two non-empty arguments ;";
        return reply.str(); 
    }
    // Start off with an empty inputmode.
    int                     tvgmode;
    mk5b_inputmode_type     ipm( mk5b_inputmode_type::empty );

    // Get the current inputmode. _some_ parameters must be left the same.
    // For (most, but not all) non-boolean parameters we have 'majik' values
    // indicating 'do not change this setting' but for booleans (and some other
    // 'verbatim' values that impossible).
    // So we just copy the current value(s) of those we want to keep unmodified.

    // use 'clock_set' to modify these!
    ipm.selcgclk  = curipm.selcgclk; 
    ipm.seldim    = curipm.seldim;
    ipm.seldot    = curipm.seldot;

    ipm.userword  = curipm.userword;
    ipm.startstop = curipm.startstop;
    ipm.tvrmask   = curipm.tvrmask;
    ipm.gocom     = curipm.gocom;

    // Other booleans (fpdp2/tvgsel a.o. are explicitly set below)
    // or are fine with their current default

    // Argument 1: the datasource
    // If the 'datasource' is "just" tvg, this is taken to mean "tvg+1"
    ipm.datasource     = ((args[1]=="tvg")?(string("tvg+1")):(args[1]));

    DEBUG(2, "Got datasource " << ipm.datasource << endl);

    // Now check what the usr wants
    if( ipm.datasource=="ext" ) {
        // aaaaah! Usr want REAL data!
        ipm.tvg        = 0;
        ipm.tvgsel     = false;
    } else if( ipm.datasource=="ramp" ) {
        // Usr want incrementing test pattern. Well, let's not deny it then!
        ipm.tvg        = 7;
        ipm.tvgsel     = true;
    } else if( ::sscanf(ipm.datasource.c_str(), "tvg+%d", &tvgmode)==1 ) {
        // Usr requested a specific tvgmode.
        ipm.tvg        = tvgmode;
        // Verify that we can do it

        // tvgmode==0 implies external data which contradicts 'tvg' mode.
        // Also, a negative number is out-of-the-question
        ASSERT2_COND( ipm.tvg>=1 && ipm.tvg<=8, SCINFO(" Invalid TVG mode number requested") );

        ipm.tvgsel     = true;

        // these modes request FPDP2, verify the H/W can do it
        if( ipm.tvg==3 || ipm.tvg==4 || ipm.tvg==5 || ipm.tvg==8 ) {
           ASSERT2_COND( rte.ioboard.hardware()&ioboard_type::fpdp_II_flag,
                         SCINFO(" requested TVG mode needs FPDP2 but h/w does not support it") );
           // do request FPDP2
           ipm.fpdp2   = true;
        }
    } else if( ipm.datasource=="none" ) {
        // Set mode directly - do not try to parse bitstreammask etc
        rte.set_input( ipm );
        reply << "0 ; ";
        return reply.str();
    } else {
        reply << "3: Unknown datasource " << args[1] << " ;";
        return reply.str();
    }

    // Argument 2: the bitstreammask in hex.
    // Be not _very_ restrictive here. "The user will know
    // what he/she is doing" ... HAHAHAAA (Famous Last Words ..)
    // The 'set_input()' will do the parameter verification so
    // that's why we don't bother here
    ipm.bitstreammask  = ::strtoul( args[2].c_str(), 0, 16 );

    // Optional argument 3: the decimation.
    // Again, the actual value will be verified before it is sent to the H/W
    // The decimation is 'j', not 'k'! Bah!
    // Also: the argument is/should be given as one of: 1,2,4,8,16
    // the 'j' value is the exponent we must write into the H/W.
    if( args.size()>=4 && !args[3].empty() ) {
        int     i_decm;
        double  decm_req( ::strtod(args[3].c_str(), 0) ), decm_closest;

        // from the double value, find the closest exponent
        // of 2 that yields the requested decimation.
        i_decm       = (int)::round( ::log(decm_req)/M_LN2 );
        decm_closest = ::exp(i_decm * M_LN2);

        // We only allow decimation up to 16 [0 < i_decm <= 4]
        ASSERT2_COND( (i_decm>=0 && i_decm<=4),
                      SCINFO(" Requested decimation is not >=1 and <=16") );
        // And it must be a power of two!
        ASSERT2_COND( ::fabs(decm_req - decm_closest)<=0.01,
                      SCINFO(" Requested decimation is not a power of 2") );

        // Great. Now transfer the integer value to the h/w
        ipm.j = i_decm;
    }

    // Optional argument 4: d'oh, don't do anything

    // Make sure other stuff is in correct setting
    ipm.gocom         = false;

    rte.set_input( ipm );

    reply << "0 ; ";
    // Return answer to caller
    return reply.str();
}

// specialization for Mark5A(+)
string mk5a_mode_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;

    // query can always be done
    if( qry ) {
        inputmode_type  ipm;
        outputmode_type opm;

        rte.get_input( ipm );
        rte.get_output( opm );

        reply << "!" << args[0] << "? 0 : "
              << ipm.mode << " : " << ipm.ntracks << " : "
              << opm.mode << " : " << opm.ntracks << " : "
              << (opm.synced?('s'):('-')) << " : " << opm.numresyncs
              << " ;";
        return reply.str();
    }

    // Command only allowed if doing nothing
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "!" << args[0] << "= 3 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    inputmode_type  ipm( inputmode_type::empty );
    outputmode_type opm( outputmode_type::empty );

    reply.str( string() );

    // first argument. the actual 'mode'
    if( args.size()>=2 && args[1].size() ) {
        opm.mode = ipm.mode = args[1];
    }

    if( ipm.mode!="none" ) {
        // Looks like we're not setting the bypassmode for transfers

        // 2nd arg: number of tracks
        if( args.size()>=3 && args[2].size() ) {
            unsigned long v = ::strtoul(args[2].c_str(), 0, 0);

            if( v<=0 || v>64 )
                reply << "!" << args[0] << "= 8 : ntrack out-of-range ("
                    << args[2] << ") usefull range <0, 64] ;";
            else
                ipm.ntracks = opm.ntracks = (int)v;
        }
    }


    try {
        // set mode to h/w
        rte.set_input( ipm );
        rte.set_output( opm );
    }
    catch( const exception& e ) {
        reply << "!" << args[0] << "= 8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << "!" << args[0] << "= 8 : Caught unknown exception! ;";
    }

    // no reply yet indicates "ok"
    if( reply.str().empty() )
        reply << "!" << args[0] << "= 0 ;";
    return reply.str();
}

// Specialization for Mark5B/DOM. Currently it can only
// globally set the mode properties; no hardware
// settings are done. 
// This is here such that a Mark5B/DOM can do net2file
// correctly. (the sender and receiver of data have to
// have their modes set equally, for the constraint-solving
// to come up with the same values at either end of the
// transmission).
string mk5bdom_mode_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream          reply;
    mk5bdom_inputmode_type ipm( mk5bdom_inputmode_type::empty );

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // query can always be done
    if( qry ) {
        reply << "0 : " << rte.trackformat() << " : " << rte.ntrack() << " ;";
        return reply.str();
    }

    // Command only allowed if doing nothing
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }

    // check if there is at least one argument
    if( args.size()<=1 ) {
        reply << "3 : Empty command (no arguments given, really) ;";
        return reply.str();
    }

    // See what we got
    ipm.mode   = OPTARG(1, args);
    ipm.ntrack = OPTARG(2, args);

    try {
        // set mode to h/w
        rte.set_input( ipm );
        reply << "0 ;";
    }
    catch( const exception& e ) {
        reply << "8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << "8 : Caught unknown exception! ;";
    }
    return reply.str();
}

// Mark5A(+) playrate function
string playrate_fn(bool qry, const vector<string>& args, runtime& rte) {
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";
    if( qry ) {
        double          clkfreq;
        outputmode_type opm;

        rte.get_output( opm );

        clkfreq  = opm.freq;
        clkfreq *= 9.0/8.0;

        // need implementation of table
        // listed in Mark5ACommands.pdf under "playrate" command
        reply << "0 : " << opm.freq << " : " << clkfreq << " : " << clkfreq << " ;";
        return reply.str();
    }

    // if command, we require 'n argument
    // for now, we discard the first argument but just look at the frequency
    if( args.size()<3 ) {
        reply << "3 : not enough arguments to command ;";
        return reply.str();
    }
    // If there is a frequency given, program it
    if( args[2].size() ) {
        outputmode_type   opm( outputmode_type::empty );

        opm.freq = ::strtod(args[2].c_str(), 0);
        DEBUG(2, "Setting clockfreq to " << opm.freq << endl);
        rte.set_output( opm );
    }
    // indicate success
    reply << " 0 ;";
    return reply.str();
}

// Mark5BDIM clock_set (replaces 'play_rate')
string clock_set_fn(bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream       reply;
    mk5b_inputmode_type curipm;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    // Get current inputmode
    rte.get_input( curipm );

    if( qry ) {
        double              clkfreq;
        
        // Get the 'K' registervalue: f = 2^(k+1)
        // Go from e^(k+1) => 2^(k+1)
        clkfreq = ::exp( ((double)(curipm.k+1))*M_LN2 );

        reply << "0 : " << clkfreq 
              << " : " << ((curipm.selcgclk)?("int"):("ext"))
              << " : " << curipm.clockfreq << " ;";
        return reply.str();
    }

    // if command, we require two non-empty arguments.
    // clock_set = <clock freq> : <clock source> [: <clock-generator-frequency>]
    if( args.size()<3 ||
        args[1].empty() || args[2].empty() ) {
        reply << "3 : must have at least two non-empty arguments ; ";
        return reply.str();
    }

    // Verify we recognize the clock-source
    ASSERT2_COND( args[2]=="int"||args[2]=="ext",
                  SCINFO(" clock-source " << args[2] << " unknown, use int or ext") );

    // We already got the current input mode.
    // Modify it such that it reflects the new clock settings.

    // If there is a frequency given, inspect it and transform it
    // to a 'k' value [and see if that _can_ be done!]
    int      k;
    string   warning;
    double   f_req, f_closest;

    f_req     = ::strtod(args[1].c_str(), 0);
    ASSERT_COND( (f_req>=0.0) );

    // can only do 2,4,8,16,32,64 MHz
    // cf IOBoard.c:
    // (0.5 - 1.0 = -0.5; the 0.5 gives roundoff)
    //k         = (int)(::log(f_req)/M_LN2 - 0.5);
    // HV's own rendition:
    k         = (int)::round( ::log(f_req)/M_LN2 ) - 1;
    f_closest = ::exp((k + 1) * M_LN2);
    // Check if in range [0<= k <= 5] AND
    // requested f close to what we can support
    ASSERT2_COND( (k>=0 && k<=5),
            SCINFO(" Requested frequency " << f_req << " <2 or >64 is not allowed") );
    ASSERT2_COND( (::fabs(f_closest - f_req)<0.01),
            SCINFO(" Requested frequency " << f_req << " is not a power of 2") );

    curipm.k         = k;

    // We do not alter the programmed clockfrequency, unless the
    // usr requests we do (if there is a 3rd argument,
    // it's the clock-generator's clockfrequency)
    curipm.clockfreq = 0;
    if( args.size()>=4 && !args[3].empty() )
        curipm.clockfreq = ::strtod( args[3].c_str(), 0 );

    // We already verified that the clocksource is 'int' or 'ext'
    // 64MHz *implies* using the external VSI clock; the on-board
    // clockgenerator can only do 40MHz
    // If the user says '64MHz' with 'internal' clock we just warn
    // him/her ...
    curipm.selcgclk = (args[2]=="int");
    if( k==5 && curipm.selcgclk )
        warning = "64MHz with internal clock will not fail but timecodes will be bogus";

    // Depending on internal or external clock, select the PCI interrupt source
    // (maybe it's valid to set both but I don't know)
    curipm.seldim = !curipm.selcgclk;
    curipm.seldot = curipm.selcgclk;

    // Send to hardware
    rte.set_input( curipm );
    reply << " 0";
    if( !warning.empty() )
        reply << " : " << warning;
    reply << " ;";
    return reply.str();
}


// Expect:
// net_protcol=<protocol>[:<socbufsize>[:<blocksize>[:<nblock>]]
// 
// Note: existing uses of eVLBI protocolvalues mean that when "they" say
//       'netprotcol=udp' they *actually* mean 'netprotocol=udps'
//       (see netparms.h for details). We will transform this silently and
//       add another value, "pudp" which will get translated into plain udp.
// Note: socbufsize will set BOTH send and RECV bufsize
string net_protocol_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream  reply;
    netparms_type& np( rte.netparms );

    if( qry ) {
        reply << "!" << args[0] << "? 0 : "
              << np.get_protocol() << " : " ;
        if( np.rcvbufsize==np.sndbufsize )
            reply << np.rcvbufsize;
        else
            reply << "Rx " << np.rcvbufsize << ", Tx " << np.sndbufsize;
        reply << " : " << np.get_blocksize()
              << " : " << np.nblock 
              << " ;";
        return reply.str();
    }
    // do not allow to change during transfers
    if( rte.transfermode!=no_transfer ) {
        reply << "!" << args[0] << "= 6 : Cannot change during transfers ;";
        return reply.str();
    }
    // Not query. Pick up all the values that are given
    // If len(args)<=1 *and* it's not a query, that's a syntax error!
    if( args.size()<=1 )
        return string("!net_protocol = 3 : Empty command (no arguments given, really) ;");

    // Make sure the reply is RLY empty [see before "return" below why]
    reply.str( string() );

    // See which arguments we got
    // #1 : <protocol>
    if( args.size()>=2 && !args[1].empty() ) {
        string  proto( args[1] );
        // do silent transformations
        if( proto=="udp" )
            proto="udps";
        if( proto=="pudp" )
            proto="udp";

        np.set_protocol( proto );
    }

    // #2 : <socbuf size> [we set both send and receivebufsizes to this value]
    if( args.size()>=3 && !args[2].empty() ) {
        long int   v = ::strtol(args[2].c_str(), 0, 0);

        // Check if it's a sensible "int" value for size, ie >0 and <=INT_MAX
        if( v<=0 || v>INT_MAX ) {
            reply << "!" << args[0] << " = 8 : <socbuf size> out of range <=0 or >= INT_MAX ; ";
        } else {
            np.rcvbufsize = np.sndbufsize = (int)v;
        }
    }

    // #3 : <workbuf> [the size of the blocks used by the threads]
    //      Value will be adjusted to accomodate an integral number of
    //      datagrams.
    if( args.size()>=4 && !args[3].empty() ) {
        unsigned long int   v = ::strtoul(args[3].c_str(), 0, 0);

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v<=UINT_MAX ) {
            np.set_blocksize( (unsigned int)v );
        } else {
            reply << "!" << args[0] << " = 8 : <workbufsize> out of range (too large) ;";
        }
    }

    // #4 : <nbuf>
    if( args.size()>=5 && !args[4].empty() ) {
        unsigned long int   v = ::strtoul(args[4].c_str(), 0, 0);

        // Check if it's a sensible "unsigned int" value for blocksize, ie
        // <=UINT_MAX [we're going to truncate from unsigned long => unsigned
        if( v>0 && v<=UINT_MAX )
            np.nblock = (unsigned int)v;
        else
            reply << "!" << args[0] << " = 8 : <nbuf> out of range - 0 or too large ;";
    }
    if( args.size()>5 )
        DEBUG(1,"Extra arguments (>5) ignored" << endl);

    // If reply is still empty, the command was executed succesfully - indicate so
    if( reply.str().empty() )
        reply << "!" << args[0] << " = 0 ;";
    return reply.str();
}


// status? [only supports query. Can't be bothered to complain
// if someone calls it as a command]
string status_fn(bool, const vector<string>&, runtime& rte) {
    // flag definitions for readability and consistency
    const unsigned int record_flag   = 0x1<<6; 
    const unsigned int playback_flag = 0x1<<8; 
    // automatic variables
    unsigned int       st;
    ostringstream      reply;

    // compile the hex status word
    st = 1; // 0x1 == ready
    switch( rte.transfermode ) {
        case in2disk:
            st |= record_flag;
            break;
        case disk2net:
            st |= playback_flag;
            st |= (0x1<<14); // bit 14: disk2net active
            break;
        case in2net:
            st |= record_flag;
            st |= (0x1<<16); // bit 16: in2net active/waiting
            break;
        case net2out:
            st |= playback_flag;
            st |= (0x1<<17); // bit 17: net2out active
            break;
        default:
            // d'oh
            break;
    }
    reply << "!status? 0 : " << hex_t(st) << " ;";
    return reply.str();
}

string debug_fn( bool , const vector<string>& args, runtime& rte ) {
    rte.ioboard.dbg();
    return string("!")+args[0]+"= 0 ;";
}

// set/qre the debuglevel
string debuglevel_fn(bool qry, const vector<string>& args, runtime&) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";
    if( qry ) {
        reply << "0 : " << dbglev_fn() << " : " <<  fnthres_fn() << " ;";
        return reply.str();
    }
    // if command, we must have an argument
    if( args.size()<2 ) {
        reply << " 3 : Command must have argument ;";
        return reply.str();
    }

    // (attempt to) parse the new debuglevel  
    // from the argument. No checks against the value
    // are done as all values are acceptable (<0, 0, >0)
    try {
        int    lev;
        string s;

        if( (s=OPTARG(1, args)).empty()==false ) {
            ASSERT_COND( (::sscanf(s.c_str(), "%d", &lev)==1) );
            // and install the new value
            dbglev_fn( lev );
        }
        if( (s=OPTARG(2, args)).empty()==false ) {
            ASSERT_COND( (::sscanf(s.c_str(), "%d", &lev)==1) );
            // and install the new value
            fnthres_fn( lev );
        }
        reply << " 0 ;";
    }
    catch( const exception& e ) {
        reply << " 8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 8 : Caught unknown exception ;";
    }
    return reply.str();
}

string interpacketdelay_fn( bool qry, const vector<string>& args, runtime& rte ) {
    // variables
    ostringstream reply;

    reply << "!" << args[0] << (qry?('?'):('=')) << " ";

    if( qry ) {
        reply << " 0 : ";
        if( rte.netparms.interpacketdelay<0 )
            reply << "auto : " << rte.netparms.theoretical_ipd << " usec";
        else 
            reply << rte.netparms.interpacketdelay << " usec";
        reply << " ;";
        return reply.str();
    }

    // if command, we must have an argument
    if( args.size()<2 || args[1].empty() ) {
        reply << " 3 : Command must have argument ;";
        return reply.str();
    }

    // (attempt to) parse the interpacket-delay-value
    // from the argument. No checks against the value
    // are done as all values are acceptable (<0, 0, >0)
    try {
        int   ipd;

        ASSERT_COND( (::sscanf(args[1].c_str(), "%d", &ipd)==1) );

        // great. install new value
        // Before we do that, grab the mutex, as other threads may be
        // using this value ...
        RTEEXEC(rte, rte.netparms.interpacketdelay=ipd);

        reply << " 0 ;";
    }
    catch( const exception& e ) {
        reply << " 8 : " << e.what() << " ;";
    }
    catch( ... ) {
        reply << " 8 : Caught unknown exception ;";
    }
    return reply.str();
}


typedef std::map<runtime*, int64_t> per_rte_skip_type;

string skip_fn( bool q, const vector<string>& args, runtime& rte ) {
    static per_rte_skip_type skips;
    // local variables
	int64_t        nskip;
	ostringstream  reply;
	
	reply << "!" << args[0] << (q?('?'):('='));

	if( q ) {
		reply << " 0 : " << skips[&rte] << " ;";
		return reply.str();
	}

    // Not a query. Only allow skip if doing a 
    // transfer to which it sensibly applies:
    if( rte.transfermode!=net2out ) {
        reply << " 6 : it does not apply to " << rte.transfermode << " ;";
        return reply.str();
    }

    // We rilly need an argument
	if( args.size()<2 || args[1].empty() ) {
		reply << " 3 : Command needs argument! ;";
		return reply.str();
	}

    // Now see how much to skip
	nskip    = ::strtol(args[1].c_str(), 0, 0);

    // Attempt to do the skip. Return value is always
    // positive so must remember to get the sign right
    // before testing if the skip was achieved
    // Must serialize access to the StreamStor, therefore
    // use do_xlr_lock/do_xlr_unlock
    do_xlr_lock();
    skips[&rte] = ::XLRSkip( rte.xlrdev.sshandle(),
                             ::abs(nskip), (nskip>=0) );
    do_xlr_unlock();
    if( nskip<0 )
        skips[&rte] = -skips[&rte];

    // If the achieved skip is not the expected skip ...
    reply << " 0";
    if( skips[&rte]!=nskip )
        reply << " : Requested skip was not achieved";
    reply << " ;";
    return reply.str();
}

// This one works both on Mk5B/DIM and Mk5B/DOM
// (because it looks at the h/w and does "The Right Thing (tm)"
string led_fn(bool q, const vector<string>& args, runtime& rte) {
	ostringstream                reply;
    ioboard_type::iobflags_type  hw = rte.ioboard.hardware();
    ioboard_type::mk5bregpointer led0;
    ioboard_type::mk5bregpointer led1;
	
	reply << "!" << args[0] << (q?('?'):('='));

    // only check mk5b flag. it *could* be possible that
    // only the mk5b flag is set and neither of dim/dom ...
    // the ioboard.cc code should make sure that this
    // does NOT occur for best operation
    if( !(hw&ioboard_type::mk5b_flag) ) {
        reply << " 8 : This is not a Mk5B ;";
        return reply.str();
    }
    // Ok, depending on dim or dom, let the registers for led0/1
    // point at the correct location
    if( hw&ioboard_type::dim_flag ) {
        led0 = rte.ioboard[mk5breg::DIM_LED0];
        led1 = rte.ioboard[mk5breg::DIM_LED1];
    } else {
        led0 = rte.ioboard[mk5breg::DOM_LED0];
        led1 = rte.ioboard[mk5breg::DOM_LED1];
    }

	if( q ) {
        mk5breg::led_colour            l0, l1;

        l0 = (mk5breg::led_colour)*led0;
        l1 = (mk5breg::led_colour)*led1;
		reply << " 0 : " << l0 << " : " << l1 << " ;";
		return reply.str();
	}

    // for DOM we must first enable the leds?
    if( hw&ioboard_type::dom_flag )
        rte.ioboard[mk5breg::DOM_LEDENABLE] = 1;

    if( args.size()>=2 && args[1].size() ) {
        led0 = text2colour(args[1]);
    }
    if( args.size()>=3 && args[2].size() ) {
        led1 = text2colour(args[2]);
    }
    reply << " 0 ; ";
    return reply.str();
}

string dtsid_fn(bool , const vector<string>& args, runtime& rte) {
    int                         ndim = 0, ndom = 0;
	ostringstream               reply;
    const transfer_type         tm( rte.transfermode );
    ioboard_type::iobflags_type hw = rte.ioboard.hardware();

	reply << "!" << args[0] << "? 0 : ";

    // <system type>
	if( hw&ioboard_type::mk5a_flag ) {
		reply << "mark5A";
        ndim = ndom = 1;
    } else if( hw&ioboard_type::mk5b_flag ) {
		reply << "mark5b";
        if( hw&ioboard_type::dim_flag )
            ndim = 1;
        else
            ndom = 1;
    } else
		reply << "-";
    // <software revision date> (timestamp of this SW version)
    reply << " : - ";
    // <media type>
    // 0 - magnetic tape, 1 - magnetic disk, 2 - realtime/nonrecording
    //  assume that if the transfermode == '*2net' or 'net2out' that we are
    //  NOT recording
    const bool realtime = (tm==in2net || tm==disk2net || tm==net2out);
    reply << " : " << ((realtime==true)?(2):(1));
    // <serial number>
    char   name[128];
    int    fd = ::open("/etc/hardware_id", O_RDONLY);
    string serial;

    if( fd>0 ) {
        int rr;
        if( (rr=::read(fd, name, sizeof(name)))>0 ) {
            vector<string> parts;
            // Use only the first line of that file; use everything up to 
            // the first newline.
            parts  = split(string(name), '\n');
            serial = parts[0];
        } else {
            serial = ::strerror(rr);
        }
        ::close(fd);
    } else {
        vector<string> parts;
        ::gethostname(name, sizeof(name));
        // split at "."'s and keep only first part
        parts = split(string(name), '.');
        serial = parts[0];
        DEBUG(3, "[gethostname]serial = '" << serial << "'" << endl);
    }
    reply << " : " << serial;
    // <#DIM ports>, <#DOM ports>
    reply << " : " << ndim << " : " << ndom;
    // <command set revision>
    reply << " : 2.7x";
    if( hw.empty() ) 
        // No Input/Output designrevisions 'cuz there ain't any
        reply << " : - : - ";
    else
        // <Input design revision> & <Output design revision> (in hex)
        reply << " : " << hex_t(rte.ioboard.idr())
              << " : " << hex_t(rte.ioboard.odr());

    reply << " ;";
	return reply.str();
}

// Display all version info we know about "SS_rev?"
// Only do it as query
string ssrev_fn(bool, const vector<string>& args, runtime& rte) {
	ostringstream       reply;
    const S_DEVINFO&    devInfo( rte.xlrdev.devInfo() );
    const S_XLRSWREV&   swRev( rte.xlrdev.swRev() );

    reply << "!" << args[0] << "? ";

    // Active transfer? Don't allow it then! (technically, I think
    // it *could* be done - just to be compatible with Mark5A/John Ball)
    if( rte.transfermode!=no_transfer ) {
        reply << "6 : Not whilst doing " << rte.transfermode << ";";
        return reply.str();
    }

    // Get all the versions!
    reply << " 0 : "
          << "BoardType " << devInfo.BoardType << " : "
          << "SerialNum " << devInfo.SerialNum << " : "
          << "ApiVersion " << swRev.ApiVersion << " : "
          << "ApiDateCode " << swRev.ApiDateCode << " : "
          << "FirmwareVersion " << swRev.FirmwareVersion << " : "
          << "FirmDateCode " << swRev.FirmDateCode << " : "
          << "MonitorVersion " << swRev.MonitorVersion << " : "
          << "XbarVersion " << swRev.XbarVersion << " : " 
          << "AtaVersion " << swRev.AtaVersion << " : "
          << "UAtaVersion " << swRev.UAtaVersion << " : "
          << "DriverVersion " << swRev.DriverVersion;
    if( rte.xlrdev.isAmazon() ) {
        const S_DBINFO& dbInfo( rte.xlrdev.dbInfo() );

        reply << " : "
              << "AMAZON : "
              << "SerialNum " << dbInfo.SerialNum << " : "
              << "PCBVersion " << dbInfo.PCBVersion << " : "
              << "PCBType " << dbInfo.PCBType << " : "
              << "PCBSubType " << dbInfo.PCBSubType << " : "
              << "FPGAConfig " << dbInfo.FPGAConfig << " : "
              << "FPGAConfigVersion " << dbInfo.FPGAConfigVersion << " : "
              << "NumChannels " << dbInfo.NumChannels;
    }
    reply << " ;";
    return reply.str();
}


string scandir_fn(bool, const vector<string>& args, runtime& rte ) {
    ostringstream   reply;
    UserDirectory   ud( rte.xlrdev );

    reply << "!" << args[0] << " = 0 : " << ud.getLayout();
    if( ud.getLayout()!=UserDirectory::UnknownLayout ) {
        unsigned int   scannum( 0 );
        const string   scan( OPTARG(1, args) );
        const ScanDir& sd( ud.scanDir() );

        reply << " : " << sd.nScans();
        if( !scan.empty() ) {
            unsigned long int    v = ::strtoul(scan.c_str(), 0, 0);

            if( ((v==ULONG_MAX) && errno==ERANGE) || v>=UINT_MAX )
                throw cmdexception("value for scannum is out-of-range");
            scannum = (unsigned int)v; 
        }
        if( scannum<sd.nScans() ) {
            ROScanPointer  rosp( sd[scannum] );

            reply << " : " << rosp.name() << " : " << rosp.start() << " : " << rosp.length();
        } else {
            reply << " : <scan # " << scannum << "> out of range";
        }
    } else {
        reply << " : 0";
    }
    reply << " ;";
    return reply.str();
}

// wait for 1PPS-sync to appear. This is highly Mk5B
// spezifik so if you call this onna Mk5A itz gonna FAIL!
// Muhahahahaa!
//  pps=* [force resync]  (actual argument ignored)
//  pps?  [report 1PPS status]
string pps_fn(bool q, const vector<string>& args, runtime& rte) {
    double              dt;
    ostringstream       reply;
    const double        syncwait( 3.0 ); // max time to wait for PPS, in seconds
    struct ::timeval    start, end;
    const unsigned int  selpp( *rte.ioboard[mk5breg::DIM_SELPP] );

	reply << "!" << args[0] << (q?('?'):('='));

    // if there's no 1PPS signal set, we do nothing
    if( selpp==0 ) {
        reply << " 6 : No 1PPS signal set (use 1pps_source command) ;";
        return reply.str();
    }

    // good, check if query
    if( q ) {
        const bool  sunk( *rte.ioboard[mk5breg::DIM_SUNKPPS] );
        const bool  e_sync( *rte.ioboard[mk5breg::DIM_EXACT_SYNC] );
        const bool  a_sync( *rte.ioboard[mk5breg::DIM_APERTURE_SYNC] );

        // check consistency: if not sunk, then neither of exact/aperture
        // should be set (i guess), nor, if sunk, may both be set
        // (the pps is either exact or outside the window but not both)
        reply << " 0 : " << (!sunk?"NOT ":"") << " synced ";
        if( e_sync )
            reply << " [not incident with DOT1PPS]";
        if( a_sync )
            reply << " [> 3 clocks off]";
        reply << " ;";
#if 0
        if((!sunk && (e_sync || a_sync)) || (sunk && e_sync && a_sync)) {
            reply << " 6 : ARG - (!sunk && (e||a)) || (sunk && e && a) ["
                  << sunk << ", " << e_sync << ", " << a_sync << "] ;";
        } else {
            reply << " 0 : " << (!sunk?"NOT ":"") << " synced ";
            if( e_sync )
                reply << " [not incident with DOT1PPS]";
            if( a_sync )
                reply << " [> 3 clocks off]";
            reply << " ;";
        }
#endif
        return reply.str();
    }

    // ok, it was command.
    // trigger a sync attempt, wait for some time [3 seconds?]
    // at maximum for the PPS to occur, clear the PPSFLAGS and
    // then display the systemtime at which the sync occurred

    // Note: the poll-loop below might be implementen rather 
    // awkward but I've tried to determine the time-of-sync
    // as accurate as I could; therefore I really tried to 
    // remove as much "unknown time consuming" systemcalls
    // as possible.
    register bool      sunk = false;
    const unsigned int wait_per_iter = 2; // 2 microseconds/iteration
    unsigned long int  max_loops = ((unsigned long int)(syncwait*1.0e6)/wait_per_iter);

    // Pulse SYNCPPS to trigger zynchronization attempt!
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 1;
    rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 0;

    // now wait [for some maximum amount of time]
    // for SUNKPPS to transition to '1'
    ::gettimeofday(&start, 0);
    while( max_loops-- ) {
        if( (sunk=*rte.ioboard[mk5breg::DIM_SUNKPPS])==true )
            break;
        // Ok, SUNKPPS not 1 yet.
        // sleep a bit and retry
        busywait( wait_per_iter );
    };
    ::gettimeofday(&end, 0);
    dt = ((double)end.tv_sec + (double)end.tv_usec/1.0e6) -
        ((double)start.tv_sec + (double)start.tv_usec/1.0e6);

    if( !sunk ) {
        reply << " 4 : Failed to sync to 1PPS within " << dt << "seconds ;";
    } else {
        char      tbuf[128];
        double    frac_sec;
        struct tm gmt;

        // As per Mark5B-DIM-Registers.pdf Sec. "Typical sequence of operations":
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 1;
        rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 0;

        // convert 'timeofday' at sync to gmt
        ASSERT_NZERO( ::gmtime_r(&end.tv_sec, &gmt) );
        frac_sec = end.tv_usec/1.0e6;
        ::strftime(tbuf, sizeof(tbuf), "%a %b %d %H:%M:", &gmt);
        reply << " 0 : sync @ " << tbuf
              << format("%0.8lfs", ((double)gmt.tm_sec+frac_sec)) << " [GMT]" << " ;";
    }
    return reply.str();
}


// report time of last generated disk-frame
// DOES NO CHECK AT ALL if a recording is running!
string dot_fn(bool q, const vector<string>& args, runtime& rte) {
    ioboard_type& iob( rte.ioboard );
    ostringstream reply;

	reply << "!" << args[0] << (q?('?'):('='));
    if( !q ) {
        reply << " 4 : Only available as query ;";
        return reply.str();
    }

    const bool          fhg = *iob[mk5breg::DIM_STARTSTOP];
    pcint::timediff     delta; // 0 by default, filled in when necessary
    pcint::timeval_type os_now  = pcint::timeval_type::now();

    // Time fields that need filling in
    double       s, frac = 0.0; // seconds + fractional seconds
    unsigned int y, doy, h, m;

    // Depending on wether FHG running or not, take time
    // from h/w or from the pseudo-dot
    if( fhg ) {
        time_t         time_now;
        struct tm      tm_dot;
        unsigned int   tmjd;
        struct timeval tv;

        // Good, fetch the hdrwords from the last generated DISK-FRAME
        // and decode the hdr.
        // HDR2:   JJJSSSSS   [day-of-year + seconds within day]
        // HDR3:   SSSS****   [fractional seconds]
        //    **** = 16bit CRC
        unsigned int hdr2 = ((*iob[mk5breg::DIM_HDR2_H]<<16)|(*iob[mk5breg::DIM_HDR2_L]));
        unsigned int hdr3 = ((*iob[mk5breg::DIM_HDR3_H]<<16)|(*iob[mk5breg::DIM_HDR3_L]));

        // hdr2>>(5*4) == right-shift hdr2 by 5BCD digits @ 4bits/BCD digit
        // NOTE: doy processing is a two-step process. The 3 BCD 'day' digits in
        // the Mark5B timecode == basically a VLBA timecode == Truncated MJD
        // daynumber. We'll get the tmjd first. Actual DOY will be computed
        // later on.
        tmjd = unbcd((hdr2>>(5*4)));
        s    = (double)unbcd(hdr2&0x000fffff) + ((double)unbcd(hdr3>>(4*4)) * 1.0e-4);
        h    = (unsigned int)(s/3600.0);
        s   -= (h*3600);
        m    = (unsigned int)(s/60.0);
        s   -= (m*60);
        // break up seconds into integral seconds + fractional part
        frac = ::modf(s, &s);

        // Get current GMT
        time_now = time(0);
        ::gmtime_r(&time_now, &tm_dot);
        y    = tm_dot.tm_year + 1900;

        // as eBob pointed out: doy starts at 1 rather than 0?
        // ah crap
        // The day-of-year = the actual daynumber - MJD at begin of the
        // current year
        doy = tmjd - jdboy(tm_dot.tm_year+1900);

        // Overwrite values read from the FHG - 
        // eg. year is not kept in the FHG, we take it from the OS
        tm_dot.tm_yday = doy;
        tm_dot.tm_hour = h;
        tm_dot.tm_min  = m;
        tm_dot.tm_sec  = (unsigned int)s;

        // Transform back into a time
        tv.tv_usec     = 0;
        tv.tv_sec      = mktime(&tm_dot);

        // Now we can finally compute delta(DOT, OS time)
        delta =  (pcint::timeval_type(tv)+frac) - os_now;
    } else {
        struct tm           tm_dot;
        pcint::timeval_type dot_now = local2dot(os_now);

        // Go from time_t (member of timeValue) to
        // struct tm. Struct tm has fields month and monthday
        // which we use for getting DoY
        ::gmtime_r(&dot_now.timeValue.tv_sec, &tm_dot);
        y     = tm_dot.tm_year + 1900;
        doy   = tm_dot.tm_yday + 1;
        h     = tm_dot.tm_hour;
        m     = tm_dot.tm_min;
        s     = tm_dot.tm_sec;
        frac  = (dot_now.timeValue.tv_usec * 1.0e-6);
        delta = dot_now - os_now;
    }

    // Now form the whole reply
    const bool   pps = *iob[mk5breg::DIM_SUNKPPS];
    unsigned int syncstat;
    const string stattxt[] = {"not_synced",
                        "syncerr_eq_0",
                        "syncerr_le_3",
                        "syncerr_gt_3" };
    // start with not-synced status
    // only if sync'ed, we check status of the sync.
    // I've noticed that most of the times >1 of these bits
    // will be set. however, i think there's a "most significant"
    // bit; it is the bit with the highest "deviation" from exact sync
    // that's been set that determines the actual sync state
    syncstat = 0;
    // if we have a pps, we assume (for a start) it's exactly synced)
    if( pps )
        syncstat = 1;
    // only if we have a pps + exact_sync set we move on to
    // next syncstatus [sync <=2 clock cycles]
    if( pps && *iob[mk5breg::DIM_EXACT_SYNC] )
        syncstat = 2;
    // finally, if we have a PPS and aperture sync is set,
    // were at >3 cycles orf!
    if( pps && *iob[mk5breg::DIM_APERTURE_SYNC] )
        syncstat = 3;

    // prepare the reply:
    reply << " = 0 : "
          // time
          << y << "y" << doy << "d" << h << "h" << m << "m" << format("%07.4lf", s+frac) << "s : " 
          // current sync status
          << stattxt[syncstat] << " : "
          // FHG status? taken  from the "START_STOP" bit ...
          << ((fhg)?("FHG_on"):("FHG_off")) << " : "
          << os_now << " : "
          // delta( DOT, system-time )
          <<  delta << " "
          << ";";
    return reply.str();
}

// struct to communicate between the trackmask_fn & the trackmask computing
// thread
struct computeargs_type {
    data_type     trackmask;
    // write solution in here
    runtime*      rteptr;

    computeargs_type() :
        trackmask( trackmask_empty ), rteptr( 0 )
    {}
};
void* computefun(void* p) {
    computeargs_type*  computeargs = (computeargs_type*)p;

    DEBUG(0, "computefun: start computing solution for " << hex_t(computeargs->trackmask) << endl);
    computeargs->rteptr->solution = solve(computeargs->trackmask);
    DEBUG(0, "computefun: done computing solution for " << hex_t(computeargs->trackmask) << endl);
    DEBUG(0, computeargs->rteptr->solution << endl);
    return (void*)0;
}

string trackmask_fn(bool q, const vector<string>& args, runtime& rte) {
    // computing the trackmask may take a considerable amount of time
    // so we do it in a thread. As long as the thread is computing we
    // report our status as "1" ("action initiated or enabled but not
    // completed" as per Mark5 A/B Commandset v 1.12)
    static pthread_t*       computer = 0;
    static computeargs_type computeargs;

    // automatic variables
    char*           eocptr;
    const bool      busy( computer!=0 && ::pthread_kill(*computer, 0)==0 );
    ostringstream   reply;

    // before we do anything, update our bookkeeping.
    // if we're not busy (anymore) we should update ourselves to accept
    // further incoming commands.
    if( !busy ) {
        delete computer;
        computer    = 0;
    }

    // now start forming the reply
	reply << "!" << args[0] << (q?('?'):('='));

    // irrespective of command or query: if we're busy we return the same
    // returnvalue
    if( busy ) {
        reply << " 1 : still computing compressionsteps ;";
        return reply.str();
    }

    // good, check if query
    if( q ) {
        reply << " 0 : " << hex_t(computeargs.trackmask) << " : " << rte.signmagdistance << " ;";
        return reply.str();
    }
    // must be command then. we do not allow the command when doing a
    // transfer
    if( rte.transfermode!=no_transfer ) {
        reply << " 6 : cannot set trackmask whilst transfer in progress ;";
        return reply.str();
    }
    // we require at least the trackmask
	if( args.size()<2 || args[1].empty() ) {
		reply << " 3 : Command needs argument! ;";
		return reply.str();
	}
    //ASSERT2_COND( ::sscanf(args[1].c_str(), "%llx", &computeargs.trackmask)==1,
    //                  SCINFO("Failed to parse trackmask") );
    computeargs.trackmask = ::strtoull(args[1].c_str(), &eocptr, 0);
    // !(A || B) => !A && !B
    ASSERT2_COND( !(computeargs.trackmask==0 && eocptr==args[1].c_str()) && !(computeargs.trackmask==~((uint64_t)0) && errno==ERANGE),
                  SCINFO("Failed to parse trackmask") );
                 
    // The sign magnitude distance is optional, default value 0
    // which means no magnitude restoration effort is made
    rte.signmagdistance = 0;
    if( args.size()>2 ) {
        ASSERT2_COND( ::sscanf(args[2].c_str(), "%d", &rte.signmagdistance) == 1,
                      SCINFO("Failed to parse sign-magnitude distance") );
    }

    // no tracks are dropped
    if( computeargs.trackmask==((uint64_t)0xffffffff << 32) + 0xffffffff ) 
        computeargs.trackmask=0;

    // Right - if no trackmask, clear it also from the runtime environment.
    // If yes trackmask, start a thread to compute the solution
    if( computeargs.trackmask ) {
        computer           = new pthread_t;
        computeargs.rteptr = &rte;

        // attempt to start the thread. if #fail then clean up
        PTHREAD2_CALL( ::pthread_create(computer, 0, computefun, &computeargs),
                       delete computer; computer = 0 );
        reply << " 1 : start computing compression steps ;";
    } else {
        rte.solution = solution_type();
        reply << " 0 : " << hex_t(computeargs.trackmask) << " : " << rte.signmagdistance << " ;";
    }
    return reply.str();
}


string version_fn(bool q, const vector<string>& args, runtime& ) {
    ostringstream   reply;

    reply << "!" << args[0]  << (q?"?":"=") << " ";

    // this is query only
    if( q ) 
	    reply << " 0 : " << buildinfo() << " ;";
    else
        reply << " 3 : query only ;";
    return reply.str();
}



string bufsize_fn(bool, const vector<string>& args, runtime& rte) {
    ostringstream   reply;

    // this is query only
    if( q ) 
	    reply << " 0 : " << rte.get_buffersize() << " ;";
    else
        reply << " 3 : query only ;";
    return reply.str();
}

template <typename T>
const char* format_s(T*) {
    ASSERT2_COND( false,
                  SCINFO("Attempt to use an undefined formatting generator") );
    return 0;
}
template <>
const char* format_s(unsigned int*) {
    return "%u%c";
}
template <>
const char* format_s(double*) {
    return "%lf%c";
}

// Function template for a function returning the location
// of a temporary variable of type T
template <typename T>
void* temporary(T*) {
    static T d;
    return &d;
}

struct fld_type {
    // fmt: pointer to format, two conversions of which last one MUST be %c
    // sep: separator character to be expected after scan into %c
    // vptr: pointer to the value where the scanned value will be stored 
    //       (if successfull)
    // tptr: pointer where the value will initially be scanned into, will
    //       only be transferred to vptr iff the scan was completely
    //       successfull
    // sz:    size of the value that is scanned
	const char*   fmt;
	const char    sep;
	void*         vptr;
    void*         tptr;
    unsigned int  sz;

	fld_type():
		fmt( 0 ), sep( '\0' ), vptr(0), tptr(0), sz(0)
	{}

    // templated constructor, at least gives _some_ degree of
    // typesafety (yeah, very shallow, I knows0rz)
    //
    // Only pass it the character you expect after the value
    // and a pointer to the location where to store the
    // (only if succesfully!) decoded value
    template <typename T>
    fld_type(char _sep, T* valueptr) :
        fmt( format_s(valueptr) ), // get the formatting string for T
        sep( _sep ),
        vptr( valueptr ),
        tptr( temporary(valueptr) ), // get a pointer-to-temp-T
        sz( sizeof(T) )
    {}
    // Returns true if the format scan returns 2 AND
    // the scanned character matches 'sep'
    // Only overwrites the value pointed at by vptr iff returns true.
    bool operator()( const char* s ) const {
        char c;
        bool rv;

        // Scan value into "tmpptr" and character into "c",
        // iff everything matches up, transfer scanned value
        // from tmpptr to valueptr
        if( (rv = (s && ::sscanf(s, fmt, tptr, &c)==2 && c==sep))==true )
            ::memcpy(vptr, tptr, sz);
        return rv;
    }
};

// set the DOT at the next 1PPS [if one is set, that is]
// this function also performs the dot_inc command
string dot_set_fn(bool q, const vector<string>& args, runtime& rte) {
    // default DOT to set is "now()"!
    static int                 dot_inc;
    static float               delta_cmd_pps = 0.0f;
    static pcint::timeval_type dot_set;
    ostringstream              reply;
    // we must get the current OS time and the dot to set is
    // (defaults to) *now*
    pcint::timeval_type        now = pcint::timeval_type::now();
    pcint::timeval_type        dot = now;

	reply << "!" << args[0] << (q?('?'):('='));

    // Handle dot_inc command/query
    if( args[0]=="dot_inc" ) {
        string    incstr( OPTARG(1, args) );

        if( q ) {
            reply << " 0 : " << dot_inc << " ;";
            return reply.str();
        }
        // it's a command so it *must* have an argument
        if( incstr.empty() ) {
            reply << " 3 : command MUST have an argument" << endl;
        }
        dot_inc = (int)::strtol(incstr.c_str(), (char **)NULL, 10);
        inc_dot( dot_inc );
        reply << " 0 ;";
        return reply.str();
    }

    if( q ) {
        reply << " 0 : " << dot_set << " : * : " << format("%07.4lf", delta_cmd_pps) << " ;";
        return reply.str();
    }
    // Ok must have been a command, then!
    bool                         force = false;
    string                       req_dot( OPTARG(1, args) );
    string                       force_opt( OPTARG(2, args) );
    ioboard_type&                iob( rte.ioboard );
    ioboard_type::mk5bregpointer sunkpps( iob[mk5breg::DIM_SUNKPPS] );

    // if force_opt is non-empty and not equal to "force", that is an 
    // error
    if( !force_opt.empty() ) {
        if( force_opt!="force" ) {
            reply << " 8 : invalid force-value ;";
            return reply.str();
        }
        force = true;
    }

    // this whole charade only makes sense if there is a 1PPS 
    // source selected
    if( (*iob[mk5breg::DIM_SELPP])==0 ) {
        reply << " 6 : cannot set DOT if no 1PPS source selected ;";
        return reply.str();
    }

    // if usr. passed a time, pick it up.
	// Supported format: VEX-like timestring
	//       0000y000d00h00m00.0000s
	//  with basically all fields being optional
	//  but with implicit order. Omitted fields are
	//  taken from the current systemtime.
    if( req_dot.size() ) {
        // translate to pcint::timeval_type ...
		const unsigned int not_given( (unsigned int)-1 );
		time_t             tt;
		struct ::tm        tms;
		// reserve space for the parts the user _might_ give
		unsigned int       year( not_given );
		unsigned int       doy( not_given );
		unsigned int       hh( not_given );
		unsigned int       mm( not_given );
		unsigned int       ss( not_given );

        // the timefields we recognize.
		// Note: this order is important (it defines the
		//  order in which the field(s) may appear)
		// Note: leave the empty fld_type() as last entry -
		//  it signals the end of the list.
		// Note: a field can _only_ read a single value.
		const fld_type     fields[] = {
                                fld_type('y', &year),
        					    fld_type('d', &doy),
        						fld_type('h', &hh),
        						fld_type('m', &mm),
        						fld_type('s', &ss),
        						fld_type()
	        				};
		// as per documentation: any timevalues not given
		// [note: the doc sais explicitly 'higher order time'
		//  like year, doy] should be taken from the current time
		// so we might as well get those right away.
		::time( &tt );
		::gmtime_r( &tt, &tms );

		// now go on and see what we can dig up
		{
			const char*     ptr;
			const char*     cpy = ::strdup( req_dot.c_str() );
            const fld_type* cur, *nxt;

			ASSERT2_NZERO( cpy, SCINFO("Failed to duplicate string") );

            for( cur=fields, ptr=cpy, nxt=0; cur->fmt!=0; cur++ ) {
				// attempt to convert the current field at the current
				// position in the string. Also check the separating
                // character
				if( ptr && (*cur)(ptr) ) {
                    // This is never an error, as long as decoding goes
                    // succesfully.
					if( (ptr=::strchr(ptr, cur->sep))!=0 )
                        ptr++;
                    nxt = cur+1;
                } else {
                    // nope, this field did not decode
                    // This is only not an error if we haven't started
                    // decoding _yet_. We can detect if we have started
                    // decoding by inspecting nxt. If it is nonzero,
                    // decoding has started
                    ASSERT2_COND( nxt==0,
                                  SCINFO("Timeformat fields not in strict sequence");
                                  ::free((void*)cpy) );
                }
            }
			::free( (void*)cpy );
		}
		// done parsing user input
		// Now take over the values that were specified
		if( year!=not_given )
			tms.tm_year = (year-1900);
		// translate doy [day-of-year] to month/day-in-month
		if( doy!=not_given ) {
			bool         doy_cvt;
			unsigned int month, daymonth;

			doy_cvt = DayConversion::dayNrToMonthDay(month, daymonth,
											         doy, tms.tm_year+1900);
			ASSERT2_COND( doy_cvt==true, SCINFO("Failed to convert Day-Of-Year " << doy));
			tms.tm_mon  = month;
			tms.tm_mday = daymonth;
		}

		if( hh!=not_given ) {
			ASSERT2_COND( hh<=23, SCINFO("Hourvalue " << hh << " out of range") );
			tms.tm_hour = hh;
		}
		if( mm!=not_given ) {
			ASSERT2_COND( mm<=59, SCINFO("Minutevalue " << mm << " out of range") );
			tms.tm_min = mm;
		}
        if( ss!=not_given ) {
            ASSERT2_COND( ss<=59, SCINFO("Secondsvalue " << ss << " out of range") );
            tms.tm_sec = ss;
        }

		// now create the actual timevalue
		struct ::timeval   requested;

		// we only do integral seconds
		requested.tv_sec  = ::my_timegm( &tms );
		requested.tv_usec = 0;

		dot = pcint::timeval_type( requested );
		DEBUG(2, "dot_set: requested DOT at next 1PPS is-at " << dot << endl);
    }

    // force==false && PPS already SUNK? 
    //  Do not resync the hardware but reset the binding of
    //  current OS time <=> DOT
    // Since 'dot' contains, by now, the actual DOT that we *want* to set
    // we can immediately bind local to DOT using the current time
    // (the time of entry into this routine):
    if( !force && *iob[mk5breg::DIM_SUNKPPS] ) {
        bind_dot_to_local(dot, now);
        dot_set       = dot;
        delta_cmd_pps = 0;
        reply << " 0 ;";
        return reply.str();
    }
    // So, we end up here because either force==true OR the card is not
    // synced yet. For the commandsequence that does not matter.

    // Now wait for 1PPS to happen [SUNKPPS becoming 1 after being reset].
    // If "force" we tell it to sync, otherwise we just clear the SUNKPPS.
    bool                 synced( false );
    pcint::timediff      dt;
    pcint::timeval_type  start;
    pcint::timeval_type  systime_at_1pps;

    // Pulse the "Reset PPS" bit
    iob[mk5breg::DIM_RESETPPS] = 1;
    iob[mk5breg::DIM_RESETPPS] = 0;

    // Id. for the syncpps bit - make sure it goes through a zero -> one
    // transition
    iob[mk5breg::DIM_SYNCPPS] = 0;
    iob[mk5breg::DIM_SYNCPPS] = 1;

    // wait at most 3 seconds for SUNKPPS to transition to '1'
    start = pcint::timeval_type::now();
    do {
        if( *sunkpps ) {
            // this is the earliest moment at which we know
            // the 1PPS happened. Do our time-kritikal stuff
            // NOW! [like mapping DOT <-> system time!
            systime_at_1pps = pcint::timeval_type::now();
            // depending on wether user specified a time or not
            // we bind the requested time. no time given (ie empty
            // requested dot) means "use current systemtime"
            if( req_dot.empty() )
                bind_dot_to_local(systime_at_1pps, systime_at_1pps);
            else
                bind_dot_to_local(dot, systime_at_1pps);
            synced = true;
            break;
        }
        // not sunk yet - busywait a bit
        dt = pcint::timeval_type::now() - start;
    } while( dt<3.0 );
    // now we can resume checking the flags
    iob[mk5breg::DIM_CLRPPSFLAGS] = 1;
    iob[mk5breg::DIM_CLRPPSFLAGS] = 0;

    // well ... ehm .. that's it then? we're sunked and
    // the systemtime <-> dot mapping's been set up.
    if( !synced ) {
        reply << " 4 : Failed to sync to selected 1PPS signal ;";
    } else {
        delta_cmd_pps = (systime_at_1pps - now);
        reply << " 0 ;";
    }
    return reply.str();
}

// Set up the Mark5B/DIM input section to:
//   * sync to 1PPS (if a 1PPS source is set)
//   * set the time at the next 1PPS
//   * start generating on the next 1PPS
// Note: so *if* this function executes completely,
// it will start generating diskframes.
//
// This is done to ascertain the correct relation
// between DOT & data.
//
// dfhg = disk-frame-header-generator
//
// 'maxsyncwait' is the amount of time in seconds the system
// should at maximum wait for a 1PPS to appear.
// Note: if you said "1pps_source=none" then this method
// doesn't even try to wait for a 1pps, ok?
void start_mk5b_dfhg( runtime& rte, double maxsyncwait ) {
    const double    syncwait( maxsyncwait ); // Max. time to wait for 1PPS
    const double    minttns( 0.7 ); // minimum time to next second (in seconds)
    // (best be kept >0.0 and <1.0 ... )

    // Okie. Now it's time to start prgrm'ing the darn Mk5B/DIM
    // This is a shortcut: we rely on the Mk5's clock to be _quite_
    // accurate. We have to set the DataObservingTime at the next 1PPS
    // before we kick off the data-frame-header-generator.
    // Make sure we are not too close to the next integral second:
    // we need some processing time (computing JD, transcode to BCD
    // write into registers etc).
    time_t                      tmpt;
    double                      ttns; // time-to-next-second, delta-t
    double                      mjd;
    struct tm                   gmtnow;
    unsigned int                tmjdnum; // truncated MJD number
    unsigned int                nsssomjd;// number of seconds since start of mjd
    struct timeval              localnow;
	pcint::timeval_type         dot;
    mk5b_inputmode_type         curipm;
    mk5breg::regtype::base_type time_h, time_l;

    // Ere we start - see if the 1PPS is actwerly zynched!
    // That is to say: we get the current inputmode and see
    // if there is a 1PPS source selected. If the PPS source is 'None',
    // obviously, there's little point in trying to zynkronize!
    rte.get_input( curipm );

    // Trigger reset of all DIM statemachines. As per
    // the docs, this 'does not influence any settable
    // DIM parameter' (we hope)
    rte.ioboard[ mk5breg::DIM_RESET ] = 1;
    rte.ioboard[ mk5breg::DIM_RESET ] = 0;
    // selpps=0 => No PPS source
    if( curipm.selpps ) {
        double         dt;
        struct timeval start;
        struct timeval end;

        // Pulse SYNCPPS to trigger zynchronization attempt!
        rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 1;
        rte.ioboard[ mk5breg::DIM_SYNCPPS ] = 0;

        // now wait [for some maximum amount of time]
        // for SUNKPPS to transition to '1'
        dt = 0.0;
        ::gettimeofday(&start, 0);
        do {
            if( *rte.ioboard[mk5breg::DIM_SUNKPPS] )
                break;
            // Ok, SUNKPPS not 1 yet.
            // sleep a bit and retry
            usleep(10);
            ::gettimeofday(&end, 0);
            dt = ((double)end.tv_sec + (double)end.tv_usec/1.0e6) -
                ((double)start.tv_sec + (double)start.tv_usec/1.0e6);
        } while( dt<syncwait );

        // If dt>=syncwait, this indicates we don't have a synched 1PPS signal?!
        ASSERT2_COND( dt<syncwait, SCINFO(" - 1PPS failed to sync"));
    }

    // As per Mark5B-DIM-Registers.pdf Sec. "Typical sequence of operations":
    rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 1;
    rte.ioboard[ mk5breg::DIM_CLRPPSFLAGS ] = 0;

    // Great. Now wait until we reach a time which is sufficiently before 
    // the next integral second
    do {
        ::gettimeofday(&localnow, 0);
        // compute time-to-next-(integral)second
        ttns = 1.0 - (double)(localnow.tv_usec/1.0e6);
    } while( ttns<minttns );

    // Good. Now be quick about it.
    // We know what the DOT will be (...) at the next 1PPS.
    // Transform localtime into GMT, get the MJD of that,
    // transform that to "VLBA-JD" (MJD % 1000) and finally
    // transform *that* into B(inary)C(oded)D(ecimal) and
    // write it into the DIM
    // Note: do NOT forget to increment the tv_sec value 
    // because we need the next second, not the one we're in ;)
    // and set the tv_usec value to '0' since ... well .. it
    // will be the time at the next 1PPS ...
	dot  = local2dot( pcint::timeval_type(localnow) );
    tmpt = (time_t)(dot.timeValue.tv_sec + 1);
    ::gmtime_r( &tmpt, &gmtnow );

    // Get the MJD daynumber
    //mjd = tm2mjd( gmtnow );
    mjd = jdboy( gmtnow.tm_year+1900 ) + gmtnow.tm_yday;
    DEBUG(2, "Got mjd for next 1PPS: " << mjd << endl);
    tmjdnum  = (((unsigned int)::floor(mjd)) % 1000);
    nsssomjd = gmtnow.tm_hour * 3600 + gmtnow.tm_min*60 + gmtnow.tm_sec;

    // Now we must go to binary coded decimal
    unsigned int t1, t2;
    t1 = bcd( tmjdnum );
    // if we multiply nseconds-since-start-etc by 1000
    // we fill the 8 bcd-digits nicely
    // [there's ~10^5 seconds in a day]
    // (and we could, if nsssomjd were 'double', move to millisecond
    // accuracy)
    t2 = bcd( nsssomjd * 1000 );
    // Transfer to the correct place in the start_time
    // JJJS   SSSS
    // time_h time_l
    time_h  = ((mk5breg::regtype::base_type)(t1 & 0xfff)) << 4;

    // Get the highest bcd digit from the 'seconds-since-start-of-mjd'
    // and move it into the lowest bcd of the high-word of START_TIME
    time_h |= (mk5breg::regtype::base_type)(t2 >> ((2*sizeof(t2)-1)*4));

    // the four lesser most-significant bcd digits of the 
    // 'seconds-since-start etc' go into the lo-word of START_TIME
    // This discards the lowest three bcd-digits.
    time_l  = (mk5breg::regtype::base_type)(t2 >> ((2*sizeof(t2)-5)*4));

    DEBUG(2, "Writing BCD StartTime H:" << hex_t(time_h) << " L:" << hex_t(time_l) << endl);

    // Fine. Bung it into the DIM
    rte.ioboard[ mk5breg::DIM_STARTTIME_H ] = time_h;
    rte.ioboard[ mk5breg::DIM_STARTTIME_L ] = time_l;

    // Now we issue a SETUP, wait for at least '135 data-clock-cycles'
    // before releasing it. We'll approximate this by just sleeping
    // 10ms.
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 1;
    ::usleep( 10000 );
    rte.ioboard[ mk5breg::DIM_SETUP ]     = 0;

    // Weehee! Start the darn thing on the next PPS!
    rte.ioboard[ mk5breg::DIM_STARTSTOP ] = 1;

    return;
}








//
//
//    HERE we build the actual command-maps
//
//
const mk5commandmap_type& make_mk5a_commandmap( void ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );

    // in2net + in2fork [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("in2net",  &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2fork", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2file", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("record", in2disk_fn)).second );

    // net2out + net2disk [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2net", spill2net_fn)).second );


    ASSERT_COND( mk5.insert(make_pair("play_rate", playrate_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5a_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("skip", skip_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );

#if 0
    // Not official mk5 commands but handy sometimes anyway :)
    insres = mk5commands.insert( make_pair("dbg", debug_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dbg into commandmap");

    insres = mk5commands.insert( make_pair("reset", reset_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command reset into commandmap");

    insres = mk5commands.insert( make_pair("netstat", netstat_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command netstat into commandmap");

    insres = mk5commands.insert( make_pair("evlbi", evlbi_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command evlbi into commandmap");
#endif
#if 0
    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
#endif
    ASSERT_COND( mk5.insert(make_pair("clock", mk5a_clock_fn)).second );
    return mk5;
}

// Build the Mk5B DIM commandmap
const mk5commandmap_type& make_dim_commandmap( void ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );

    // in2net + in2fork [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("in2net",  &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2fork", &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2file", &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("record", in2disk_fn)).second );

    // sekrit functions ;) Mk5B/DIM is not supposed to be able to record to
    // disk/output ... but the h/w can do it all the same :)
    // net2out + net2disk [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2net", spill2net_fn)).second );


    ASSERT_COND( mk5.insert(make_pair("clock_set", clock_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("1pps_source", pps_source_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pps", pps_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot", dot_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot_set", dot_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot_inc", dot_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdim_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("skip", skip_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );

#if 0
    insres = mk5commands.insert( make_pair("netstat", netstat_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command netstat into DIMcommandmap");

    insres = mk5commands.insert( make_pair("evlbi", evlbi_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command evlbi into DIMcommandmap");
#endif

#if 0
    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
#endif
    return mk5;
}

const mk5commandmap_type& make_dom_commandmap( void ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdom_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );

    // net2*
    //ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("spill2net", spill2net_fn)).second );
    return mk5;
}

const mk5commandmap_type& make_generic_commandmap( void ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    //ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    //ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdom_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );

    // net2*
    //ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    //ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2net", spill2net_fn)).second );

    return mk5;
}


