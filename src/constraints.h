// "constraintsolvers" for dealing with networktransfer read/write sizes
// Copyright (C) 2007-2010 Harro Verkouter
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
// Depending on how you plan to send data over the network, we may need to
// solve some constraints and adjust sizes so our basic constraints are met.
// Some of the constraints are very much because of the hardware, for
// efficiency or for forcing consistency and alignment (no partial
// blocks/frames).
//
// 1. when using a lossy transfermechanism, loss of one datagram should not
//    throw us out-of-sync - a frame or a block [or both] should be
//    transferred in an integral amount of datagrams
// 2. datagrams [and by extension blocks and/or frames] should be an
//    integral multiple of 8 bytes in size (because of streamstor
//    and or channeldropping requirements)
// 3. when compressing data [ie throwing away selected bitstreams], there
//    is a difference between read-from-device and write-to-network sizes;
//    at the receiving end there is a difference between read-from-the-network
//    size and write-to-the-device size
// 4. if the underlying dataformat is known we may add the constraint that a
//    frame is sent in an integral number of datagrams
// 5. for some formats - notably mark5b - when compressing we should NOT
//    compress the header. because we cannot tell from a datagram wether it
//    contains a header or not, it is simplest to skip compression of the
//    first <compressoffset> bytes in each datagram
// 6. the actual amount of payload available is very much dependant on the
//    actual network protocol in combination with (an optional)
//    application-level protocol [eg udp + 64bit sequencenumber]
#ifndef JIVE5A_CONSTRAINTS_H
#define JIVE5A_CONSTRAINTS_H
#include <map>
#include <sstream>
#include <iostream>
#include <exception>

// we use these objects to draw our information from
#include <netparms.h>      // for the network parameters ... d'oh
#include <trackmask.h>     // for the optional compression
#include <headersearch.h>  // for possible dataformat + size


// Forward declaration of the type of thing you'll get back from the
// "solvers" - a set of constrained values.
struct constraintset_type;

// helpfull macro's for accessing/testing constraints in a
// constraintset_type, "cs"
#define CONSTRAINT(cs, constraint)    ((cs)[constraints::constraint])
#define UNCONSTRAINED(cs,constraint)  CONSTRAINT(cs,constraint)==constraints::unconstrained
#define CONSTRAINED(cs,constraint)    CONSTRAINT(cs,constraint)!=constraints::unconstrained

// This is the API offered. Based on the input parameters it returns a set
// of constrained values for blocksize, read_size (read_size<blocksize),
// write_size (to the network) such that all applicable constraints are met.
// You should be able to use the values without reserve.
// Extract the values from the object using "operator[]" with the particular
// value you want, e.g. in sort of pseudocode:
//
//    unsigned char*      buffer;
//    solution_type       solution( <....> ); // (*)
//    netparms_type       netparms( <....> ); // filled in with current
//    constraintset_type  rv;
//
//    // based on current settings, work out the sizes
//    rv = constrain(netparms, solution);
//
//    // and use the computed sizes as follows:
//    unsigned int    blocksize = CONSTRAINT(rv, blocksize);
//    // or address them like this - the "CONSTRAINT(..)" macro
//    // translates into this:
//    unsigned int    readsize  = rv[constraints::read_size];
//    unsigned int    writesize = rv[constraints::write_size];
//
//    // this generates a compressor to compress 'readsize' words using the
//    // compression solution found in 'solution'
//    compressor_type compressor(solution, readsize); 
//
//    buffer = new unsigned char[ blocksize ];
//
//    XLRRead(buffer, blocksize);
//    for( ptr=buffer; ptr<buffer+blocksize; ptr+=readsize ) {
//          // do compress the data - the constrainers have already taken
//          // into account that this will generate 'writesize' bytes of
//          // output ... !
//          compressor(ptr);   // (*)
//          // compression is done in-place so the first 'writesize' bytes
//          // are the ones we need to send ...
//          ::write(socketfd, ptr, writesize);
//    }
//
//    // (*) this step is optional. in case there is no compression
//    //     the system is smart enough to know that and make sure
//    //     that read_size==write_size ...

// No compression and you don't know/care what data you're transferring
//constraintset_type constrain(const netparms_type& netparms);

// You want to compress (ie throw away bits) but you do not know or care
// about the data you're doing it to [good enough for Mark4/VLBA]
//constraintset_type constrain(const netparms_type& netparms,
//                             const solution_type& solution);

// constraining hints/options
namespace constraints {
    const unsigned int BYFRAMESIZE = 0x1;
}

// By giving us the dataformat & number of tracks you indicate you care
// about whole disk/tape frames and as such the code adheres to that.
// Both with and w/o compression
//constraintset_type constrain(const netparms_type& netparms,
//                             const headersearch_type& hdr);

constraintset_type constrain(const netparms_type& netparms,
                             const headersearch_type& hdr,
                             const solution_type& solution,
                             const unsigned int options = 0x0);


//
//  ############# No need to read further than this ;) #############
//
//  below this the actual cruft used to facilitate the API above
//


namespace constraints {
    const unsigned int unconstrained = (unsigned int)-1;
    
    // the actual constraints
    enum constraints {
        framesize, blocksize, MTU, compress_offset,
        application_overhead, protocol_overhead,
        read_size, write_size, n_mtu
    };

    // if something wrong one of these will be thrown
    struct constraint_error:
        public std::exception
    {
            constraint_error(const std::string& m);
            virtual const char* what( void ) const throw ();
            virtual ~constraint_error() throw ();
            const std::string  _msg;
    };
}

// show the actual defined constraint-variables as readable strings
std::ostream& operator<<(std::ostream& os, constraints::constraints c);

// each constraint-variable has an unsigned int value associated with it
typedef std::map<constraints::constraints, unsigned int> constraint_container;

// this is the thing you'll get back from the API. Set/retrieve the
// contraint-values using operator[]
struct constraintset_type {
    // read/write access. if the constraint "c" is not yet in this
    // constraintset it gets added with a default value of "unconstrained".
    unsigned int&       operator[]( constraints::constraints c );
    // read-only access. if the constraint "c" is not in this set of
    // constraints you'll get back a reference to the "unconstrained" value.
    // as it is the read-only method the constraint is NOT added to the set.
    const unsigned int& operator[]( constraints::constraints c ) const;

    // Calling this'un on a constraintset returned from constrain will give
    // you confidence that whatever was returned contains absolutely usable
    // values. It throws an exception if it finds that a constraint is not
    // met.
    void                validate( void ) const;
    
    constraint_container  constraints;
};
// display the defined constraints in the set "cs" in human-readable format as
// "[" <constraintname>:<value>[, <constraintname>:<value>] "]"
// where <value> can be the text "unconstrained"
std::ostream& operator<<(std::ostream& os, const constraintset_type& cs);


#endif
