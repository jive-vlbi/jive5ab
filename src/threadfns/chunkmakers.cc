// implementations
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
//
#include <threadfns/chunkmakers.h>

chunkmakerargs_type::chunkmakerargs_type(runtime* rte, std::string const& rec):
    rteptr( rte ), recording_name( rec )
{
    EZASSERT2( rteptr && !recording_name.empty(), std::runtime_error,
               EZINFO("Do not pass NULL runtime pointer (" << (void*)rte << ") " <<
                      "or empty recording name ('" << recording_name << "') ") );
}

bsn_entry::bsn_entry(std::string const& basename, uint32_t n):
    blockSeqNr( n ), baseName( basename )
{}

////////////////////////////////////////////////
/// The Suffix Sources
////////////////////////////////////////////////


// No suffix defined
NoSuffix::NoSuffix( chunkmakerargs_type const& )
{}

std::string NoSuffix::operator()( unsigned int ) const {
    static std::string nothing;
    return nothing;
}


// Get the suffix from the defined data streams
DataStreamSuffix::DataStreamSuffix( chunkmakerargs_type const& cma ):
    __m_datastreams( cma.rteptr->mk6info.datastreams )
{}

std::string DataStreamSuffix::operator()( unsigned int t) const {
    std::string suffix = __m_datastreams.streamid2name( t );
    EZASSERT2( !suffix.empty(), std::runtime_error, 
            EZINFO("No suffix found for data stream#" << t << std::endl) );
    return "_ds"+suffix;
}

// Get the suffix from the net_port definitions
NetPortSuffix::NetPortSuffix( chunkmakerargs_type const& cma ):
    __m_netparms( cma.rteptr->netparms )
{}

std::string NetPortSuffix::operator()( unsigned int t) const {
    return "_ds"+__m_netparms.stream2suffix( t );
}
