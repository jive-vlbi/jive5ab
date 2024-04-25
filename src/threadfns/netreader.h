// templated threaded read functions
// Copyright (C) 2007-2023 Marjolein Verkouter
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
// Author:  Marjolein Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef JIVE5A_THREADFNS_NETREADER_H
#define JIVE5A_THREADFNS_NETREADER_H

// own code
#include <chain.h>
#include <getsok.h>
#include <runtime.h>
#include <getsok_udt.h>
#include <evlbidebug.h>
#include <udt.h>
#include <threadfns/udpreader.h>
#include <threadfns/udtreader.h>
#include <threadfns/udpsreader.h>
#include <threadfns/socketreader.h>
#include <threadfns/udpsnorreader.h>
#include <threadfns/do_push_block.h>
//#include <threadfns.h>

// std c++
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <typeinfo>

// std C
#include <pthread.h>
#include <signal.h>
#if 0
#include <xlrdevice.h>
#include <ioboard.h>
#include <pthreadcall.h>
#include <playpointer.h>
#include <dosyscall.h>
#include <streamutil.h>
#include <getsok.h>
#include <getsok_udt.h>
#include <headersearch.h>
#include <busywait.h>
#include <timewrap.h>
#include <stringutil.h>
#include <sciprint.h>
#include <boyer_moore.h>
#include <mk6info.h>
#include <sse_dechannelizer.h>
#include <hex.h>
#include <udt.h>
#include <timezooi.h>
#include <threadutil.h> // for install_zig_for_this_thread()
#include <libvbs.h>
#include <carrayutil.h>
#include <auto_array.h>
#include <countedpointer.h>

#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm> // for std::min 
#include <queue>
#include <list>
#include <map>
#include <stdexcept>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>
#include <stdlib.h> // for ::llabs()
#include <limits.h>
#include <stdarg.h>
#include <time.h>   // for ::clock_gettime
#include <unistd.h>
#include <strings.h> // for ffs(3)
#endif



// the udpsreader needs only a bit of templating;
// The heavy lifting is done by an internal minichain
// where the actual socket reading can be separated
// from outputting a tagged or non-tagged block

#if 0
inline bool do_push_block(outq_type<block>* q, unsigned int, block b) {
    return q->push( b );
}
inline bool do_push_block(outq_type< tagged<block> >* q, unsigned int t, block b) {
    return q->push( tagged<block>(t, b) );
}
#endif


// Transparent support for pushing tagged items or not
template <typename Item>
void netreader(outq_type<Item>* outq, sync_type<fdreaderargs>* args) {
    static const std::string myself( std::string("netreader<") + typeid(Item).name() + ">: " );
    // deal with generic networkstuff
    bool                   stop;
    fdreaderargs*          network = args->userdata;
    const std::string      proto = network->netparms.get_protocol();
    scopedfd               acceptedfd( (proto=="udt" ? &UDT::close : &::close) );

    // first things first: register our threadid so we can be cancelled
    // if the network (if 'fd' refers to network that is) is to be closed
    // and we don't know about it because we're in a blocking syscall.
    // (under linux, closing a filedescriptor in one thread does not
    // make another thread, blocking on the same fd, wake up with
    // an error. b*tards).
    // do the malloc/new outside the critical section. operator new()
    // may throw. if that happens whilst we hold the lock we get
    // a deadlock. we no like.
    SYNCEXEC(args, stop = args->cancelled);

    if( stop ) {
        DEBUG(0, myself << "stop signalled before we actually started" << std::endl);
        return;
    }
    // we may have to accept first
    if( network->doaccept ) {
        pthread_t                 my_tid( ::pthread_self() );
        pthread_t*                old_tid  = 0;
        fdprops_type::value_type* incoming = 0;

        // Before entering a potentially blocking accept() set up
        // infrastructure to allow other thread(s) to interrupt us and get
        // us out of catatonic sleep:
        // if the network (if 'fd' refers to network that is) is to be closed
        // and we don't know about it because we're in a blocking syscall.
        // (under linux, closing a filedescriptor in one thread does not
        // make another thread, blocking on the same fd, wake up with
        // an error. b*tards. so we have to manually send a signal to wake a
        // thread up!).
        install_zig_for_this_thread(SIGUSR1);
        SYNCEXEC(args, old_tid = network->threadid; network->threadid = &my_tid);

        // Attempt to accept. "do_accept_incoming" throws on wonky!
        RTEEXEC(*network->rteptr,
                network->rteptr->transfersubmode.set( wait_flag ));
        DEBUG(0, myself << "waiting for incoming connection" << std::endl);

        try {
            // dispatch based on actual protocol
            if( proto=="unix" )
                incoming = new fdprops_type::value_type(do_accept_incoming_ux(network->fd));
            else if( proto=="udt" )
                incoming = new fdprops_type::value_type(do_accept_incoming_udt(network->fd));
            else
                incoming = new fdprops_type::value_type(do_accept_incoming(network->fd));
        }
        catch( ... ) {
            // no need to delete memory - our pthread_t was allocated on the stack
            uninstall_zig_for_this_thread(SIGUSR1);
            SYNCEXEC(args, network->threadid = old_tid);
            throw;
        }
        uninstall_zig_for_this_thread(SIGUSR1);

        // great! we have accepted an incoming connection!
        // check if someone signalled us to stop (cancelled==true).
        // someone may have "pressed cancel" between the actual accept
        // and us getting time to actually process this.
        // if that wasn't the case: close the lissnin' sokkit
        // and install the newly accepted fd as network->fd.
        // Whilst we have the lock we can also put back the old threadid.
        args->lock();
        stop              = args->cancelled;
        network->threadid = old_tid;
        // Only need to save the old server socket in case
        // we're overwriting it. If we don't, then the cleanup function
        // of this step will take care of closing that file descriptor
        if( !stop ) {
            acceptedfd.mFileDescriptor = network->fd;
            network->fd                = incoming->first;
        }
        args->unlock();

        if( stop ) {
            DEBUG(0, myself << "stopsignal before actual start " << std::endl);
            return;
        }
        // as we are not stopping yet, inform user whom we've accepted from
        DEBUG(0, myself << "incoming dataconnection from " << incoming->second << std::endl);

        delete incoming;
    }

    // update submode flags
    RTEEXEC(*network->rteptr, 
            network->rteptr->transfersubmode.clr( wait_flag ).set( connected_flag ));

    // and delegate to appropriate reader
    if( proto=="udps" )
        udpsreader(outq, args);
    else if( proto=="udpsnor" )
        udpsnorreader(outq, args);
    else if( proto=="udp" )
        udpreader(outq, args);
    else if( proto=="udt" )
        udtreader(outq, args);
    else if( proto=="itcp") {
        // read the itcp id from the stream before falling to the normal
        // tcp reader
        char               c;
        pthread_t          my_tid( ::pthread_self() );
        pthread_t*         old_tid  = 0;
        unsigned int       num_zero_bytes = 0;
        std::ostringstream os;

        SYNCEXEC(args, old_tid = network->threadid; network->threadid = &my_tid);
        install_zig_for_this_thread(SIGUSR1);

        while ( num_zero_bytes < 2 ) {
            ASSERT_COND( ::read(network->fd, &c, 1) == 1 );
            if ( c == '\0' ) {
                num_zero_bytes++;
            }
            else {
                num_zero_bytes = 0;
            }
            os << c;
        }
        uninstall_zig_for_this_thread(SIGUSR1);
        SYNCEXEC(args, network->threadid = old_tid);

        std::vector<std::string> identifiers = split( os.str(), '\0', false );
        
        // make key/value pairs from the identifiers
        std::map<std::string, std::string> id_values;
        const std::string separator(": ");
        // the last identifiers 2 will be empty, so don't bother
        for (size_t i = 0; i < identifiers.size() - 2; i++) { 
            size_t separator_index = identifiers[i].find(separator);
            if ( separator_index == std::string::npos ) {
                THROW_EZEXCEPT(itcpexception, "Failed to find separator in itcp stream line: '" << identifiers[i] << "'" << std::endl);
            }
            id_values[ identifiers[i].substr(0, separator_index) ] = 
                identifiers[i].substr(separator_index + separator.size());
        }

        // only start reading if the itcp is as expected or no expectation is set
        if ( network->rteptr->itcp_id.empty() ) {
            socketreader(outq, args);
        }
        else {
            std::map<std::string, std::string>::const_iterator id_iter =  id_values.find("id");
            if ( id_iter == id_values.end() ) {
                DEBUG(-1, "No TCP id received, but expected '" << network->rteptr->itcp_id << "', will NOT continue reading" << std::endl);
            }
            else if ( id_iter->second != network->rteptr->itcp_id ) {
                DEBUG(-1, "Received TCP id '" << id_iter->second << "', but expected '" << network->rteptr->itcp_id << "', will NOT continue reading" << std::endl);
            }
            else {
                socketreader(outq, args);
            }
        }
    }
    else
        socketreader(outq, args);

    // We're definitely not going to block on any fd anymore so make rly
    // sure we're not receiving signals no more
    SYNCEXEC(args, delete network->threadid; network->threadid = 0; network->finished = true;);

    // update submode flags
    RTEEXEC(*network->rteptr, 
            network->rteptr->transfersubmode.clr( connected_flag ) );
}

#endif  // include guard
