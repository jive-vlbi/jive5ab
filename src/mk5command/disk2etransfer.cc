// Copyright (C) 2007-2018 Harro Verkouter
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
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo

///////////////////////////////////////////////////////////////////////////////
//       Only compiled in if we *have* an etransfer configured
//              https://github.com/jive-vlbi/etransfer
///////////////////////////////////////////////////////////////////////////////
#ifdef ETRANSFER

#include <etransfer.h>
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <threadfns.h>    // for all the processing steps + argument structs
#include <tthreadfns.h>
#include <inttypes.h>     // For SCNu64 and friends
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

using namespace std;

///////////////////////////////////////////////////////////////////////////////////////
//
//   We have an e-transfer!
//
///////////////////////////////////////////////////////////////////////////////////////


// We only do disk2etransfer
string disk2etransfer_fn( bool qry, const vector<string>& args, runtime& rte) {
    ostringstream               reply;
    const transfer_type         ctm( rte.transfermode ); // current transfer mode
    static etransfer_state_type etransfer_map;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query may execute always, command only if nothing else happening
    INPROGRESS(rte, reply, !(qry || ctm==no_transfer))

    if( qry ) {
        etransfer_state_type::const_iterator p = etransfer_map.find( &rte );

        reply << " 0 : " << (p==etransfer_map.end() || !p->second?"in":"") << "active";

        if( p!=etransfer_map.end() && p->second ) {
            auto const pp = p->second;
            reply << " : " << pp->scanName << " : " << pp->fpStart << " : " << pp->fpCur << " : " << pp->fpEnd << " ;";
        }
        return reply.str();
    }

    // disk2etransfer = <host|ip>[@<port>] : <destination path> [ : <mode> ]
    // 0                1                    2                      3
    //
    // <port> = defaults to 4004; the compiled in etransfer daemon default
    // <mode> = New, OverWrite, Resume, SkipExisting
    //
    string const serverAddress( OPTARG(1, args) );
    string const outputPath( OPTARG(2, args) );
    string const modeString( OPTARG(3, args) );

    EZASSERT2(!serverAddress.empty() && !outputPath.empty(), cmdexception, EZINFO("Either the destination host, path or both are not given"));

    // Make etransfer follow same debuglevel as jive5ab
    auto oldLevel = etdc::dbglev_fn( dbglev_fn() );

    // Host should be host@port; the "port()" function will do the
    // string conversion and check for valid port number. We default
    // to the etransfer compiled in default
    string::size_type   at( serverAddress.find('@') );
    etdc::host_type     host_s(  host(at==string::npos ? serverAddress : serverAddress.substr(0, at)) );
    etdc::port_type     port_nr( at==string::npos ? port(4004) :  port(serverAddress.substr(at+1)) );
    etdc::openmode_type mode{ etdc::openmode_type::New };

    // Attempt to transform a given mode string to an official open mode
    if( !modeString.empty() ) {
        std::istringstream  iss(modeString);
        iss.exceptions( std::istringstream::failbit | std::istringstream::badbit );
        iss >> mode;
    }

    // Let's start a new etransfer state
    etransfer_state_ptr state{ std::make_shared<etransfer_state>(&rte) };

    // Connect to remote etdserver
    //state->remote_proxy = mk_proxy("tcp", host_s, port_nr);
    state->remote_proxy = std::make_shared<etdc::ETDProxy>( mk_client("tcp", host_s, port_nr) );
    DEBUG(2, "disk2etransfer_vbs: connected COMMAND to " << host_s << ":" << port_nr << std::endl);

    // Set a receive timeout (otherwise waits forever) second and get the data channel addresses
    std::dynamic_pointer_cast<etdc::ETDProxy>(state->remote_proxy)->setsockopt( etdc::so_rcvtimeo{{4,0}} );

    etdc::dataaddrlist_type dataChannels( state->remote_proxy->dataChannelAddr() );
    EZASSERT2(!dataChannels.empty(), cmdexception,
              EZINFO("The etransfer daemon at " << serverAddress << " claims to have no data ports?"));

    // In the data channels, we must replace any of the wildcard IPs with a real host name - 
    // use the one that was given under argument #2
    std::regex  rxWildCard("^(::|0.0.0.0)$");
    for(auto ptr=dataChannels.begin(); ptr!=dataChannels.end(); ptr++)
        *ptr = mk_sockname(get_protocol(*ptr), etdc::host_type(std::regex_replace(get_host(*ptr), rxWildCard, host_s)), get_port(*ptr));

    std::exception_ptr eptr;

    try {
        ROScanPointer current_scan = rte.xlrdev.getScan(rte.current_scan);

        // Start by attempting to open the recording
        state->src_fd      = std::make_shared<etd_streamstor_fd>(rte.xlrdev.sshandle(), rte.pp_current.Addr, rte.pp_end.Addr);

        // OK, that one exists (otherwise an exception would've happened).
        // Now it's time to see if we have permission to write to the destination
        // and also we'll find out how many bytes already there
        state->dstResult   = unique_result(new etdc::result_type(state->remote_proxy->requestFileWrite(outputPath, mode)));
        const auto nByte{ etdc::get_filepos(*(state->dstResult)) };
        
        // Transfer some scan_set= parameters to state to save them
        state->scanName = current_scan.name();
        state->fpStart  = state->fpCur = rte.pp_current.Addr;
        state->fpEnd    = rte.pp_end.Addr;

        // Do we actually have to do anything?
        if( mode!=etdc::openmode_type::SkipExisting || nByte==0 ) {
            // Now we *potentially* have to do something - let's advance
            // the current pointer by the number of bytes the destination already has
            // and see if there's still anything left to do
            state->fpCur += nByte;

            if( state->fpCur<state->fpEnd ) {
                // Assert that we can seek to the requested position
                ETDCASSERT(state->src_fd->lseek(state->src_fd->__m_fd, state->fpCur, SEEK_SET)!=static_cast<off_t>(-1),
                           "Cannot seek to position " << state->fpCur << " in recording "
                            << state->scanName << " - " << etdc::strerror(errno));
                // It's about time we tried to connect to a data channel!
                const size_t        bufSz( 32*1024*1024 );
                std::ostringstream  tried;

                for(auto addr: dataChannels) {
                    try {
                        // This is 'sendFile' so our data channel will have to have a big send buffer

                        // Pass all possible receive buf sizes - the mk_client
                        // will make sure only the right ones will be used
                        state->dst_fd = mk_client(get_protocol(addr), get_host(addr), get_port(addr),
                                                  etdc::so_rcvbuf{bufSz}, etdc::so_sndbuf{bufSz}, etdc::udt_sndbuf{bufSz});
                        DEBUG(2, "disk2etransfer_vbs: connected DATA to " << addr << std::endl);
                        break;
                    }
                    catch( std::exception const& e ) {
                        tried << addr << ": " << e.what() << ", ";
                    }
                    catch( ... ) {
                        tried << addr << ": unknown exception" << ", ";
                    }
                }
                // And make sure that we *did* connect somewhere
                ETDCASSERT(state->dst_fd, "Failed to connect to any of the data servers: " << tried.str());

                // Weehee! we're connected!
                // At this point it makes sense to store the current
                // etransfer_state into our runtime->etransfer_state map;
                // all connections have been made, the data source is open and has been
                // proven to be able to be sought to the beginning of data to transfer.
                // All that's left is to build the processing chain so, before we actually
                // run the threads, all info they'll be needing needs to be made available
                // in a shared place
                // But before we update that entry, make sure the current runtime does not
                // have a transfer that could refer to this shared storage
                // (if you execute this transfer twice in a row in the same runtime, e.g.)
                rte.processingchain = chain();

                auto insres    = etransfer_map.insert( std::make_pair(&rte, state) );
                if( !insres.second ) {
                    // there was already an entry, assert it is nullptr
                    ETDCASSERT(!insres.first->second, "The previous etransfer state was not cleaned up/still in use");
                    // now we can overwrite it with our state
                    insres.first->second = state;
                }

                // This should be done in a processing chain
                chain        c;
                
                c.register_cancel(c.add(&etransfer_fd_read, 10, state), &cancel_etransfer);
                c.add(&etransfer_fd_write, state);
                c.register_final(&etransfer_cleanup, insres.first);

                rte.statistics.clear();
                rte.processingchain = c;
                rte.processingchain.run();
                rte.transfersubmode.clr_all().set( run_flag );
                rte.transfermode = disk2etransfer;

                reply << " 1;";
            } else {
                reply << " 0 : Destination is complete or larger than source file ;";
            }
        } else {
            reply << " 0 : remote file already exists and SkipExisting == true;";
        }
    }
    catch( ... ) {
        eptr = std::current_exception();
    }
    etdc::dbglev_fn( oldLevel ); 
    if( eptr )
        std::rethrow_exception(eptr);
    return reply.str();
}

#endif // ETRANSFER
