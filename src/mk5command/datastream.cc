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
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <iostream>
#include <mk6info.h>

using namespace std;


// We want to be able to record data streams. Each data stream can contain
// VDIF frames matching special constraints - e.g.
//    stream-0:  threadids [0,2,4,6]
//    stream-1:  threadids [1,3,5,7]
// Or, if we're feeling fancy, add the vdif_station as well:
//    stream-Xx: vdif_station [Xx]
//    stream-Yy: vdif_station [Yy]
// Or:
//    stream-foo: vdif_station[Xx].threadids[0,1] vdif_station[Yy].[0,1]
//    stream-bar: vdif_station[Xx].threadids[3] vdif_station[Yy].[3] vdif_station[Zz].[3]
string datastream_fn( bool qry, const vector<string>& args, runtime& rte ) {
    ostringstream              reply;

    // we can already form *this* part of the reply
    reply << "!" << args[0] << ((qry)?('?'):('=')) << " ";

    // Query is always possible, command only if idle
    INPROGRESS(rte, reply, !(qry || rte.transfermode==no_transfer));

    if( qry ) {
        const string                stream_id( OPTARG(1, args) );
        datastream_mgmt_type const& datastreams( rte.mk6info.datastreams );

        // If no specific stream requested, return the name(s) of all
        // defined data streams
        if( stream_id.empty() ) {
            reply << "0 : " << datastreams.size() << " ";
            for( datastreamlist_type::const_iterator p = datastreams.begin(); p != datastreams.end(); p++ )
                reply << ": " << p->first << " ";
        } else {
            // attempt to find the requested data stream
            datastreamlist_type::const_iterator stream( datastreams.find(stream_id) );

            if( stream==datastreams.end() ) 
                reply << "8 : The streeam '" << stream_id << "' is not defined";
            else {
                filterlist_type::const_iterator p = stream->second.match_criteria_txt.begin();

                reply << "0 : " << stream->first;
                while( p!=stream->second.match_criteria_txt.end() ) {
                    reply << " : " << *p; 
                    p++;
                }
            }
        }
        reply << ";";
        return reply.str();
    }

    // Ok it was a command
    bool         recognized( false );    
    const string subCommand( OPTARG(1, args) );

    EZASSERT2( !subCommand.empty(), Error_Code_8_Exception, EZINFO("subcommand (add, delete, clear) is required"));

    // 0            1        2              3
    // datastream = add    : <datastream> : <extra stuff>
    // datastream = remove : <datastream> : <extra stuff>
    // datastream = reset
    // datastream = clear
    if( subCommand=="add" || subCommand=="remove" ) {
        const string    dsname( OPTARG(2, args) );

        recognized = true;
        if( dsname.empty() ) {
            reply << "8 : Missing data stream name ;";
            return reply.str();
        }

        if( subCommand=="add" ) {
            // Extract all non-empty match criteria
            filterlist_type                filterlist;
            vector<string>::const_iterator argptr = args.begin();

            // move the args pointer to the first filter spec (position 3)
            // and keep only non-empty elements
            advance(argptr, 3);
            remove_copy_if(argptr, args.end(), back_inserter(filterlist), isEmptyString());

            // It is an error to have no match specificiations
            if( filterlist.empty() ) {
                reply << "8 : Missing data stream match specification(s) ;";
                return reply.str();
            }
            rte.mk6info.datastreams.add(dsname, filterlist);
        } else {
            rte.mk6info.datastreams.remove(dsname);
        }
        reply << "0 ;";
    } else if( subCommand=="clear" ) {
        recognized = true;
        rte.mk6info.datastreams.clear();
        reply << "0 ;";
    } else if( subCommand=="reset" ) {
        recognized = true;
        rte.mk6info.datastreams.reset();
        reply << "0 ;";
    }
    if( !recognized )
        reply << " 2 : " << args[1] << " does not apply to " << args[0] << " ;";
    return reply.str();
}
