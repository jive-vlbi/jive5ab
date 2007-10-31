// implementation
#include <runtime.h>
#include <dosyscall.h>
#include <pthreadcall.h>
#include <evlbidebug.h>
#include <hex.h>
#include <bin.h>
#include <streamutil.h>

// C and system includes
#include <signal.h>

using namespace std;

// some constant string-valued defaults for netparm
const std::string defProtocol = std::string("tcp");
const std::string defUDPHelper = std::string("smart");

// construct a default network parameter setting thingy
netparms_type::netparms_type():
    rcvbufsize( netparms_type::defSockbuf ), sndbufsize( netparms_type::defSockbuf ),
    udphelper( defUDPHelper ),
    interpacketdelay( netparms_type::defIPD ), nblock( netparms_type::defNBlock ),
    protocol( defProtocol ), mtu( netparms_type::defMTU ), datagramsize( 0 ),
    blocksize( netparms_type::defBlockSize ), nmtu( netparms_type::nMTU )
{
    constrain();
}

void netparms_type::set_protocol( const std::string& p ) {
    protocol = p;
    if( protocol.empty() )
        protocol = defProtocol;
    // update constraints
    constrain();
}

void netparms_type::set_mtu( unsigned int m ) {
    mtu = m;
    if( !mtu )
        mtu = netparms_type::defMTU;
    constrain();
}

void netparms_type::set_blocksize( unsigned int bs ) {
    blocksize = bs;
    if( blocksize==0 )
        blocksize = netparms_type::defBlockSize;
    constrain();
}

#if 0
void netparms_type::set_nmtu( unsigned int n ) {
    nmtu = n;
    if( !nmtu )
        nmtu = netparms_type::nMTU;
    constrain();
}
#endif

// implement the constraints
void netparms_type::constrain( void ) {
    // step one:
    //   from mtu + protcol, derive datagramsize
    compute_datagramsize();

    // Ok. datagramsize is constrained now.
    // [compute datagramsize also asserts it is non-zero]

    // Make sure the blocksize is an integral multiple
    // of datagramsize
    blocksize = (blocksize/datagramsize) * datagramsize;

    return;
}

void netparms_type::compute_datagramsize( void ) {
    // Compute the datagramsize based on MTU, protocol in use and
    // number-of-MTU's per datagram
    // consty stuff
    const unsigned int  iphdrlen = 20;
    // variables
    unsigned int        hdrlen;

    // Start of with a datagramsize of nMTU * MTUsize
    // Note: before calling this, member functions should
    //       make sure that none of these are '0' for best
    //       results
    datagramsize = nmtu * mtu; 

    // minimum TCP hdr is 6 4-byte words
    // Let's just hope there are no "OPTION"s set->
    // there may be a variable number of options present which
    // make the header bigger... let's just pretend we know nothing!
    if( protocol=="tcp" )
        hdrlen = 6 * 4; 
    else if( protocol=="udp" )
        // UDP header is 2 2-byte words
        // AAAIIIIEEEEE!! Why can't I (HV) read RFC's correctly?!
        // in the UDP header there's *four* (4) 2-byte words:
        // "source-port dst-port length checksum" !!
        hdrlen = 4 * 2;
    else
        ASSERT2_NZERO(0, SCINFO("Unrecognized netprotocol '" << protocol << "'!"));
    // Account for internal protocol header. As it is, just as with (tcp|udp)
    // protocol header, sent only once per datagram (namely, immediately after
    // said protocol header), we integrate it into the total 'hdrlen'
    hdrlen += sizeof(unsigned long long);

    // Now subtract the total headerlen from the datagramsize
    // Note: the IP header is sent in each fragment (MTU) but
    // the TCP/UDP and application header are only sent once

    // It makes sense to assert this condition before actually
    // doing the (unsigned!) subtraction
    ASSERT_COND( (datagramsize>(hdrlen+nmtu*iphdrlen)) );

    datagramsize -= hdrlen;
    datagramsize -= (nmtu * iphdrlen);

    // and truncate it to be an integral multiple of 8
    // [so datagram loss, even @64tracks, does not change the
    //  tracklayout; each bit in a 32 or 64bit word is one bit
    //  of one track => it's a longitudinal format: the same bit
    //  in subsequent words are the bits of a single track]
    // Also: reads/writes to the StreamStor must be multiples of 8
    datagramsize &= ~0x7;

    // Assert that something's left at all
    ASSERT2_NZERO( datagramsize, SCINFO(" After truncating to multiple-of-eight no size left") );
}



// evlbi stats counters
evlbi_stats_type::evlbi_stats_type():
    pkt_total( 0ULL ), pkt_lost( 0ULL ), pkt_ooo( 0ULL ), pkt_rpt( 0ULL )
{}

ostream& operator<<(ostream& os, const evlbi_stats_type& es) {
    double    pct_lst( -1.0 );
    double    pct_ooo( -1.0 );

    if( es.pkt_total ) {
        pct_lst = ((double)es.pkt_lost/(double)es.pkt_total) * 100.0;
        pct_ooo  = ((double)es.pkt_ooo/(double)es.pkt_total) * 100.0;
    }
    os << format("%10llu", es.pkt_total) << " TOT, "
       << format("%10llu", es.pkt_lost) << " LST (" << format("%5.2lf%%", pct_lst) << "), "
       << format("%10llu", es.pkt_ooo) << " OoO (" << format("%5.2lf%%", pct_ooo) << ")";
    if( es.pkt_rpt )
        os << " " << es.pkt_rpt << " RPT!!";
    return os;
}




//
// inputboard mode status
//
inputmode_type::inputmode_type( inputmode_type::setup_type setup ):
    mode( (setup==mark5adefault)?("st"):("") ),
    ntracks( (setup==mark5adefault)?(32):(-1) ),
    notclock( true ),
    errorbits( 0 )
{}
ostream& operator<<(ostream& os, const inputmode_type& ipm) {
    os << ipm.ntracks << "trk " << ipm.mode << " [R:" << !ipm.notclock
       << " E:" << bin_t(ipm.errorbits) << "]";
    return os;
}

//
// Outputboard mode status
//
outputmode_type::outputmode_type( outputmode_type::setup_type setup ):
    mode( (setup==mark5adefault)?("st"):("") ),
    freq( (setup==mark5adefault)?(8.0):(-1.0) ),
    active( false ), synced( false ),
    tracka( (setup==mark5adefault)?(2):(-1) ),
    trackb( (setup==mark5adefault)?(2):(-1) ),
    ntracks( (setup==mark5adefault)?(32):(0) ),
    numresyncs( -1 ), throttle( false ),
    format( (setup==mark5adefault)?("mark4"):("") )
{}

ostream& operator<<(ostream& os, const outputmode_type& opm ) {
    os << opm.ntracks << "trk " << opm.mode << "(" << opm.format << ") "
       << "@" << format("%7.5lfMs/s/trk", opm.freq) << " "
       << "<A:" << opm.tracka << " B:" << opm.trackb << "> " 
       << " S: "
       << (opm.active?(""):("!")) << "Act "
       << (opm.synced?(""):("!")) << "Sync "
       << (opm.throttle?(""):("!")) << "Susp "
       ;
    return os;
}


// how to show 'devices' on a stream
ostream& operator<<(ostream& os, devtype dt) {
    char   c( '!' );
    switch( dt ) {
        case dev_none:
            c = '*';
            break;
        case dev_network:
            c = 'N';
            break;
        case dev_disk:
            c = 'D';
            break;
        case dev_fifo:
            c = 'F';
            break;
        default:
            break;
    }
    return os << c;
}




//
//
//    The actual runtime 
//
//
runtime::runtime():
    transfermode( no_transfer ), transfersubmode( transfer_submode() ),
    condition( 0 ), mutex( 0 ), fd( -1 ), acceptfd( -1 ), repeat( false ), 
    lastskip( 0LL ), packet_drop_rate( 0ULL ),
    lasthost( "localhost" ), run( false ), stop( false ),
    tomem_dev( dev_none ), frommem_dev( dev_none ), nbyte_to_mem( 0ULL ), nbyte_from_mem( 0ULL ),
    rdid( 0 ), wrid( 0 ),
    inputmode( inputmode_type::empty ), outputmode( outputmode_type::empty )
{
    // already set up the mutex and the condition variable

    // Work with a default mutex, should be fine
    mutex = new pthread_mutex_t;
    PTHREAD_CALL( ::pthread_mutex_init(mutex, 0) );

    // And the conditionvariable, also with default attributes
    condition = new pthread_cond_t;
    PTHREAD_CALL( ::pthread_cond_init(condition, 0) );

    // Set a default inputboardmode
    this->inputMode( inputmode_type(inputmode_type::mark5adefault) );

    // Set the outputboardmode
    this->outputMode( outputmode_type(outputmode_type::mark5adefault) );
}

void runtime::start_threads( void* (*rdfn)(void*), void* (*wrfn)(void*), bool initstartval ) {
    int            pte;
    sigset_t       oldset, newset;
    ostringstream  oss;
    pthread_attr_t attribs;

    // only allow if no threads are running
    ASSERT_COND( (rdid==0 && wrid==0) );

    // prepare the errormessage - in case of failure to start
    // messages will be added
    oss << "runtime::start_threads()/";

    // Before creating the threads, set the sigmask to empty (saving the
    // current set) and restore the mask afterwards. Threads that are
    // being created inherit the mask of the thread starting them so this
    // is the most convenient way to do it [otherwise we'd have to put
    // this code in each thread-function]
    ASSERT_ZERO( ::sigfillset(&newset) );
    PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &newset, &oldset) );

    // Fill in the attributes we want to start the threads with
    PTHREAD_CALL( ::pthread_attr_init(&attribs) );
    // make sure we create them joinable so we can wait on them when
    // cancelled!
    PTHREAD2_CALL( ::pthread_attr_setdetachstate(&attribs, PTHREAD_CREATE_JOINABLE),
                   ::pthread_attr_destroy(&attribs); ::pthread_sigmask(SIG_SETMASK, &oldset, 0); );

    // Before we actually start the threads, fill in some of the parameters
    run            = initstartval;
    stop           = false;
    tomem_dev      = dev_none;
    frommem_dev    = dev_none;
    nbyte_to_mem   = 0ULL;
    nbyte_from_mem = 0ULL;

    // Attempt to start the read-thread. Give 'm 'this' as functionargument
    rdid = new pthread_t;

    // do not throw (yet) upon failure to create, we may have a wee bit of cleaning-up
    // to do fi the threadfn fails to start!
    if( (pte=::pthread_create(rdid, &attribs, rdfn, (void*)this))!=0 ) {
        oss << "failed to start readthread - " << ::strerror(pte) << ", ";
        delete rdid;
        rdid = 0;
    }
    // try to start the write thread
    wrid = new pthread_t;
    if( (pte=::pthread_create(wrid, &attribs, wrfn, (void*)this))!=0 ) {
        oss << "failed to start writethread - " << ::strerror(pte) << ", ";
        delete wrid;
        wrid = 0;
    }
    // If either failed to start, do cleanup
    if( rdid==0 || wrid==0 ) {
        this->stop_threads();
    }
    // cleanup stuff that must happen always
    PTHREAD_CALL( ::pthread_attr_destroy(&attribs) );
    PTHREAD_CALL( ::pthread_sigmask(SIG_SETMASK, &oldset, 0) );

    // do not forget to throw up in case of error!
    if( rdid==0 || wrid==0 )
        throw pthreadexception(oss.str());
    return;
}

void runtime::stop_threads( void ) {
    DEBUG(3,"Stopping threads" << endl);
    DEBUG(3,"Disabling interthread queue" << endl);
    queue.disable();
    DEBUG(3,"Good. Now signal threads to stop. Acquiring mutex...");
    // Signal, in the appropriate way any started thread to stop
    PTHREAD_CALL( ::pthread_mutex_lock(mutex) );
    DEBUG(3, "got it" << endl);
    run  = false;
    stop = true;
    PTHREAD_CALL( ::pthread_cond_broadcast(condition) );
    PTHREAD_CALL( ::pthread_mutex_unlock(mutex) );
    DEBUG(3, "signalled...");
    // And wait for it/them to finish
    if( wrid ) {
        PTHREAD_CALL( ::pthread_join(*wrid, 0) );
        DEBUG(1, "writethread joined.." << endl);
    }
    if( rdid ) {
        PTHREAD_CALL( ::pthread_join(*rdid, 0) );
        DEBUG(1, "readthread joined.." << endl);
    }
    DEBUG(3,"done!" << endl);
    // Great! *now* we can clean up!
    delete rdid;
    delete wrid;
    rdid = 0;
    wrid = 0;
    stop = false;
    return;
}

const inputmode_type& runtime::inputMode( void ) const {
    unsigned short               vlba;
    unsigned short               mode;
    codemap_type::const_iterator cme;

    mode = *(ioboard[mk5areg::mode]);
    vlba = *(ioboard[mk5areg::vlba]);
    // decode it. Start off with default of 'unknown'
    inputmode.mode = "?";
    if( mode>7 )
        inputmode.mode = "tvg";
    else if( mode==4 )
        inputmode.mode = "st";
    else if( mode<4 )
        inputmode.mode = (vlba?("vlba"):("vlbi"));
    // Uses same code -> ntrack mapping as outputboard
    // start off with 'unknown' (ie '0' (zero))
    inputmode.ntracks = 0;
    cme  = code2ntrack(codemap, mode);
    // Note: as we may be doing TVG, we should not throw
    // upon not finding the mode!
    if( cme!=codemap.end() )
        inputmode.ntracks = cme->numtracks;

    // get the notclock
    inputmode.notclock = *(ioboard[mk5areg::notClock]);

    // and the errorbits
    inputmode.errorbits = *(ioboard[mk5areg::errorbits]);
    return inputmode;
}

void runtime::inputMode( const inputmode_type& ipm ) {
    bool                          is_vlba;
    inputmode_type                curmode( inputmode );
    ioboard_type::mk5aregpointer  mode = ioboard[ mk5areg::mode ];
    ioboard_type::mk5aregpointer  vlba = ioboard[ mk5areg::vlba ];

    // transfer parameters from argument to desired new mode
    // but only those that are set
    if( !ipm.mode.empty() )
        curmode.mode    = ipm.mode;
    if( ipm.ntracks>0 )
        curmode.ntracks = ipm.ntracks;
    curmode.notclock    = ipm.notclock;

    // go from mode(text) -> mode(encoded value)
    is_vlba = (curmode.mode=="vlba");
    if( curmode.mode=="st" ) {
        mode = 4;
    }
    else if( curmode.mode=="tvg" || curmode.mode=="test" ) {
        mode = 8;
    } else if( curmode.mode=="vlbi" || is_vlba || curmode.mode=="mark4" ) {
        vlba = (is_vlba?1:0);

        // read back from h/w, now bung in ntrack code
        switch( curmode.ntracks ) {
            case 32:
                mode = 0;
                break;
            case 64:
                mode = 1;
                break;
            case 16:
                mode = 2;
                break;
            case 8:
                mode = 3;
                break;
            default:
                ASSERT2_NZERO(0, SCINFO("Unsupported nr-of-tracks " << ipm.ntracks));
                break;
        }
    } else 
        ASSERT2_NZERO(0, SCINFO("Unsupported inputboard mode " << ipm.mode));

    inputmode = curmode;
    return;
}


void runtime::reset_ioboard( void ) const {
    ASSERT_COND( ioboard.boardType()==ioboard_type::mk5a );

    // pulse the 'R'eset register
    DEBUG(1,"Resetting IOBoard" << endl);
    ioboard_type::mk5aregpointer  w0 = ioboard[ mk5areg::ip_word0 ];

    w0 = 0;
    usleep( 1 );
    w0 = 0x200;
    usleep( 1 );
    w0 = 0;
    usleep( 1 );
#if 0
    ioboard[ mk5areg::R ] = 0;
    usleep( 1 );
    ioboard[ mk5areg::R ] = 1;
    usleep( 1 );
    ioboard[ mk5areg::R ] = 0;
#endif
    DEBUG(1,"IOBoard reset" << endl);
}

const outputmode_type& runtime::outputMode( void ) const {
    unsigned short               trka, trkb;
    unsigned short               code;
    codemap_type::const_iterator cme;

    outputmode.active     = *(ioboard[ mk5areg::Q ]);
    outputmode.synced     = *(ioboard[ mk5areg::S ]);
    outputmode.numresyncs = *(ioboard[ mk5areg::NumberOfReSyncs ]);
    outputmode.throttle   = *(ioboard[ mk5areg::SF ]);
    // reset the SuspendFlag [cf IOBoard.c ...]
    ioboard[ mk5areg::SF ] = 0;

    trka                   = *(ioboard[ mk5areg::ChASelect ]);
    trkb                   = *(ioboard[ mk5areg::ChBSelect ]);
    /// and frob the tracknrs..
    outputmode.tracka      = trka + (trka>31?70:2);
    outputmode.trackb      = trkb + (trkb>31?70:2);

    // And decode 'code' into mode/format?
    code = *(ioboard[ mk5areg::CODE ]);

    DEBUG(2,"Read back code " << hex_t(code) << endl);
    cme  = code2ntrack(codemap, code);
    ASSERT2_COND( (cme!=codemap.end()),
                  SCINFO("Failed to find entry for CODE#" << code) );
    outputmode.ntracks = cme->numtracks;

    return outputmode;
}

void runtime::outputMode( const outputmode_type& opm ) {
    bool            is_vlba;
    unsigned short  code;
    outputmode_type curmode( outputmode );

    // 'curmode' holds the current outputmode
    // Now transfer values from the argument 'opm'
    // and overwrite the values in 'curmode' that are set
    // in 'opm' [it it possible to just change the 
    // playrate w/o changing the mode!]
    if( !opm.mode.empty() )
        curmode.mode    = opm.mode;
    if( !opm.format.empty() )
        curmode.format  = opm.format;
    if( opm.freq>=0.0 )
        curmode.freq    = opm.freq;
    if( opm.ntracks>0 )
        curmode.ntracks = opm.ntracks;
    if( opm.tracka>0 )
        curmode.tracka  = opm.tracka;
    if( opm.trackb>0 )
        curmode.trackb  = opm.trackb;

    // Good. 'curmode' now holds the values that we 
    // would *like* to set.
    // Go and do it!
    is_vlba = (curmode.format=="vlba");

    // Always program a frequency. Do not support setting a negative
    // frequency. freq<0.001 (really, we want to test ==0.0 but
    // on floats/doubles that's not wise to do...
    if( curmode.freq<0.0 )
        curmode.freq = 8.0;

    if( ::fabs(curmode.freq)<0.001 ) {
        ioboard[ mk5areg::I ] = 0;
        DEBUG(2, "Setting external clock on output board" << endl);
    } else {
        // do program the frequency
        double                        freq = curmode.freq;
        unsigned int                  dphase;// was unsigned long but on LP64, UL is 64bits iso 32!
        unsigned char*                dp( (unsigned char*)&dphase );
        ioboard_type::mk5aregpointer  w0    = ioboard[ mk5areg::ip_word0 ];
        ioboard_type::mk5aregpointer  w2    = ioboard[ mk5areg::ip_word2 ];
#if 0
        ioboard_type::mk5aregpointer  w     = ioboard[ mk5areg::W ];
        ioboard_type::mk5aregpointer  wclk  = ioboard[ mk5areg::W_CLK ];
        ioboard_type::mk5aregpointer  fqud  = ioboard[ mk5areg::FQ_UD ];
#endif
        // From comment in IOBoard.c
        // " Yes, W0 = phase, cf. AD9850 writeup, p. 10 "
        // and a bit'o googling and finding/reading the h/w documentation
        // of this chip and looking at the registernaming in IOBoard.c and
        // the PDF, it would seem that the clockchip
        // on the board is an "Analog Devices #9850" chip.
        //
        // From the mentioned writeup [I've added the pdf into 
        // the CVS] of how to make the device read a new clockfreq..
        // i think it should be done differently...
        // [I've included the PDF into CVS so you can check for
        //  yourself]
        // take care of parity
        if( is_vlba || curmode.format=="mark4" )
            freq *= 1.125;
        // correct for VLBA non-data-replacement headers
        if( is_vlba )
            freq *= 1.008;
        // if 64 tracks, double the freq
        if( curmode.ntracks==64 )
            freq *= 2.0;

        //dphase = (unsigned long)(freq*42949672.96+0.5);
        // According to the AD9850 manual, p.8:
        // f_out = (dphase*input_clk)/ 2^32  (1)
        // judging from the code in IOBoard.c it follows that
        // the AD9850 on board the I/O board is fed with a 100MHz
        // clock - so we just reverse (1)
        dphase = (unsigned int)(((freq*4294967296.0)/100.0)+0.5);
        DEBUG(2,"dphase = " << hex_t(dphase) << " (" << dphase << ")" << endl);
        // According to the AD9850 doc:
        //   rising edge of FQ_UD resets the 'address' pointer to
        //   zero, after that, five rising edges of w_clk are used to
        //   transfer 5 * 8bits into the device. After 5 w_clk's
        //   any more w_clks are ingnored until the next 
        //   fq_ud rising edge or a reset...

        // 1) trigger a rising edge on fq_ud [force it to go -> 0 -> 1
        //    to make sure there *is* a rising edge!]
#if 0
        cout << "(1) FQ_UD=" << (unsigned short)fqud << ", WCLK=" << (unsigned short)wclk << endl;
        wclk = 0;
        fqud = 0;
        cout << "(2) FQ_UD=" << (unsigned short)fqud << ", WCLK=" << (unsigned short)wclk << endl;
        ASSERT_COND( (((unsigned short)fqud==0) && ((unsigned short)wclk==0)) );
        //fqud = 1;
        //fqud = 0;
#endif
        // 2) clock the thingy into the registers with
        //    five w_clks
        for( unsigned int i=0; i<5; ++i ) {
            w0 = ((i==0)?0:dp[4-i]);
            w2 = 1;
            usleep(1);
            w2 = 0;
#if 0
            // stick a byte into word0
            w = (unsigned short)((i==0)?(0):(dp[4-i]));
            // and pulse the 'w_clk' bit to get it read
            // make *sure* it's a rising edge!
            wclk = 1;
            wclk = 0;
#endif
        }
#if 0
        // pulse fqud to read the value
        fqud = 1;
        fqud = 0;
#endif
        w0 = 0x100;
        usleep(1);
        w0 = 0;
        // done!
        ioboard[ mk5areg::I ] = 1;
        DEBUG(2, "Set internal clock on output board @" << freq << "MHz [" << curmode.freq << "MHz entered]" << endl);
    }

    // Ok, clock has been programmed and (hopefully) has become stable...
    // (See ProgrammingOutputBoard.pdf)
    int                          v;
    int                          aplus;
    ioboard_type::mk5aregpointer Q = ioboard[ mk5areg::Q ];
    ioboard_type::mk5aregpointer C = ioboard[ mk5areg::C ];

    // start re-initialization sequence
    // as per the pdf we must do this everytime
    // the clock has been tampered with
    Q = 0;
    usleep( 1 );

    // enable fill detection
    ioboard[ mk5areg::F ]   = 1;

    // only enable mk5+ for now, none of the fancy blah
    v     = 0;
    aplus = 0;
    if( curmode.mode=="mark5a+" || curmode.mode=="mark5b" )
        aplus = 1;
    // the vlba bit must be set if either doing VLBA
    // or the aplus bit is set
    if( is_vlba || aplus )
        v = 1;
    DEBUG(2, "Setting AP" << aplus << ", V" << v << endl);
    ioboard[ mk5areg::AP ]  = aplus;
    ioboard[ mk5areg::AP1 ] = 0;
    ioboard[ mk5areg::AP2 ] = 0;
    ioboard[ mk5areg::V ]   = v;

    // decide on the code
    code = 0;
    if( curmode.mode=="st" )
        code = 4;
    else if( curmode.mode=="tvg" ) {
        // doc sais: "Not implemented" ... let's find out!
        code = 8;
    } else {
        codemap_type::const_iterator cme = ntrack2code(codemap, curmode.ntracks);
        
        ASSERT2_COND( cme!=codemap.end(), SCINFO("Unsupported number of tracks: " << curmode.ntracks) );
        code = cme->code;
        DEBUG(2,"Found codemapentry code/ntrk: " << cme->code << "/" << cme->numtracks);
    }
    // write the code to the H/W
    ioboard[ mk5areg::CODE ] = code;

    DEBUG(2,"write code " << hex_t(code) << " to output-board" << endl);

    // Pulse the 'C' register
    C = 1;
    usleep( 10 );
    C = 0;
    usleep( 10 );

    // and switch 'Q' back on
    Q = 1;
    usleep( 10 );

    // And store current mode
    outputmode = curmode;

    return;
}


runtime::~runtime() {
    DEBUG(1, "Cleaning up runtime" << endl);
    // if threadz running, kill'm!
    this->stop_threads();

    // release the resources
    PTHREAD_CALL( ::pthread_mutex_destroy(mutex) );
    PTHREAD_CALL( ::pthread_cond_destroy(condition) );
    delete mutex;
    delete condition;

    // if filedescriptor is open, close it
    if( fd>=0 )
        ::close( fd );

    if( acceptfd>=0 )
        ::close( acceptfd );
}

