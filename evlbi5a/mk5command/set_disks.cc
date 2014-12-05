// Copyright (C) 2007-2014 Harro Verkouter
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
#include <mk6info.h>
#include <mk5_exception.h>
#include <mk5command/mk5.h>
#include <iostream>


using namespace std;

struct isEmptyString {
    bool operator()(string const& s) const {
        return s.empty();
    }
};

////////////////////////////////////////////////////////////////////
//
//  set_disks = (GRP | pattern) [ : (GRP | pattern) ]*
//  0           1               (2 ... )
//
//  For definition of supported patterns see "mk6info.h"
//  In stead of patterns may also use group alias GRP, as defined using
//  "group_def = add : GRP : pattern [ : pattern ]*"
//
////////////////////////////////////////////////////////////////////
string set_disks_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream           reply;
    //struct mk6info_type&    mk6info( rte.mk6info );

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    // Query is always possible, command only if idle
    INPROGRESS(rte, reply, !(q || rte.transfermode==no_transfer));

    // Deal with query first
    if( q ) {
        string                      tmp;
        mountpointlist_type const&  mps( rte.mk6info.mountpoints );

        // concatenate the mountpoints into a string
        copy(mps.begin(), mps.end(), ostringiterator(tmp, " : ", true));

        reply << " 0 : " << mps.size() << tmp << " ;";
        return reply.str();
    }

    // Must be command. Better have some arguments then!
    EZASSERT2(args.size()>1, Error_Code_6_Exception, EZINFO(" - requires at least one argument"));

    // Make a patternlist out of all arguments
    mk6info_type&                  mk6info( rte.mk6info );
    patternlist_type               pl;
    vector<string>::const_iterator argptr = args.begin();

    // move the args pointer to the first argument and copy only the
    // non-empty elements
    advance(argptr, 1);
    remove_copy_if(argptr, args.end(), back_inserter(pl), isEmptyString());

    // Ok, let's get down to resolving, using the groups defined in the
    // current runtime and the built-in groupdefs and collect all
    // mountpoints matching the pattern(s)
    mk6info.mountpoints = find_mountpoints( resolvePatterns(pl, mk6info.groupdefs) );

    if( mk6info.mountpoints.empty() )
        reply << " 8 : 0 : no mountpoints matched your selection criteria";
    else
        reply << " 0 : " << mk6info.mountpoints.size();
    reply << " ;";
    return reply.str();
}
