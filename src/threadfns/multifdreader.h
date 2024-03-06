// producer fn used to read from multiple file descriptors
// each thread "pops" an entry from "net_port", opens it and start 
// reading from it (supports tcp and udt too ...)
// "pop" = rotate the net_port entries, such that after n_port()
// threads we should be back where we started
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
#ifndef JIVE5A_THREADFNS_MULTIFDREADER_H
#define JIVE5A_THREADFNS_MULTIFDREADER_H

#include <chain.h>
#include <block.h>
#include <threadfns.h>
#include <evlbidebug.h>
//#include <threadfns/per_sender.h>
//#include <threadfns/do_push_block.h>
#include <threadfns/netreader.h>

//#include <list>
//#include <string>
//#include <sstream>
//#include <iostream>
#include <pthread.h>

// Can produce tagged or untagged blocks
template <typename Item>
void multifdreader(outq_type<Item>* oq, sync_type<multifdrdargs>* args) {
    DEBUG(1, "multifdreader[" << ::pthread_self() << "]: starting" << std::endl);
    bool                    stop;
    // Step one: check if there is another port for us to open
    unsigned int            myTag( 0 );
    fdreaderargs*           myFD( 0 );
    multifdrdargs*          mfd( args->userdata );

    SYNCEXEC(args, stop = args->cancelled;
                   if( !stop && mfd->curHPS<mfd->netparms.n_port() ) {
                        myFD  = net_server(networkargs(mfd->rteptr, mfd->netparms, true));
                        myTag = mfd->curHPS++;
                        mfd->netparms.rotate();
                        mfd->fdreaders.push_back( myFD );
                    });
    if( stop || myFD==0 ) {
        DEBUG(1, "multifdreader[" << ::pthread_self() << "]: terminating before begin - "
                 << (stop ? "cancelled" : (myFD==0 ? "no more filedescriptors" : "WUT?")));
        return;
    }

    // This is one reader thread
    // so we make a fake sync_type so we can reuse net_reader(...)
    pthread_cond_t          lclCondition = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t         lclMutex     = PTHREAD_MUTEX_INITIALIZER;
    sync_type<fdreaderargs> lclST(&lclCondition, &lclMutex);

    // Fill in all kinds of local data
    myFD->tag = myTag;
    lclST.setqdepth( args->qdepth );
    lclST.setstepid( args->stepid );
    lclST.setuserdata( myFD );
    try {
        DEBUG(1, "multifdreader[" << ::pthread_self() << "]: fd=" << myFD->fd << " streamID=" << myTag << std::endl);
        ::netreader<Item>(oq, &lclST);
    }
    catch( std::exception const& e ) {
        DEBUG(-1, "multifdreader[" << ::pthread_self() << "]: error - " << e.what() << std::endl);
    }
    catch( ... ) {
        DEBUG(-1, "multifdreader[" << ::pthread_self() << "]: caught unknown exception" << std::endl);
    }
    // Signal that we stopped
    SYNCEXEC(args, args->userdata->nFinished++);
    DEBUG(1, "multifdreader[" << ::pthread_self() << "]: terminating" << std::endl);
}

#endif
