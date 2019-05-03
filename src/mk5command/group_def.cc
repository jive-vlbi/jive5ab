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

template <typename PairType>
struct keyextractor: public ostringiterator {
    keyextractor(string& s, const string& sep, bool startwithsep):
        ostringiterator(s, sep, startwithsep)
    {}

    virtual keyextractor<PairType>& operator*( void ) {
        return *this;
    }
    virtual keyextractor<PairType>& operator=(const PairType& p) {
        this->ostringiterator::operator=(p.first);
        return *this;
    }
};


////////////////////////////////////////////////////////////////////
//
//  group_def = define : GRP : pattern [ : pattern ]*
//            = delete : GRP
//
//  group_def?     => !group_def? 0 [ : GRP ]*     (list all known GRPs)
//  group_def? GRP => !group_def? 0 [ : pattern ]* (list all patterns tied to GRP)
//                                4 : EGROUP       (/no such group ...)
//
//  For definition of supported patterns see "mk6info.h"
//
////////////////////////////////////////////////////////////////////
string group_def_fn(bool q, const vector<string>& args, runtime& rte ) {
    ostringstream           reply;
    struct mk6info_type&    mk6info( rte.mk6info );

    reply << "!" << args[0] << (q?('?'):('=')) << " ";

    // Query is always possible, command only if idle
    INPROGRESS(rte, reply, !(q || rte.transfermode==no_transfer));


    // Deal with query first
    if( q ) {
        // Was a group-id given?
        // group_def? [GRP]
        // 0          1
        string                         txt;
        const string                   grp( OPTARG(1, args) );
        const bool                     builtin_def = ::isBuiltin(grp);
        groupdef_type const&           groups( mk6info.groupdefs );
        groupdef_type::const_iterator  defptr = groups.find(grp);

        EZASSERT2(args.size()==2, Error_Code_6_Exception, EZINFO(" - too many arguments, one group at a time please"));

        EZASSERT2(grp.empty() || defptr!=groups.end() || builtin_def, Error_Code_6_Exception,
                  EZINFO(" - undefined group " << grp));

        // Append the definition of grp or all defined groups
        if( defptr!=groups.end() )
            copy(defptr->second.begin(), defptr->second.end(), ostringiterator(txt, " : ", true));
        else if( builtin_def ) {
            // it was a built-in groupdef, extract the associated pattern
            const patternlist_type  pl = patternOf( grp ); 
            copy(pl.begin(), pl.end(), ostringiterator(txt, " : ", true));
        } else {
            // first user defined groups
            copy(groups.begin(), groups.end(), keyextractor<groupdef_type::value_type>(txt, " : ", true));
            if( groups.size() )
                txt += " : ";

            // and append the built-in group definitions
            groupdef_type const&  builtingroups( builtinGroupDefs() );
            copy(builtingroups.begin(), builtingroups.end(), keyextractor<groupdef_type::value_type>(txt, " : ", true));
        }
        
        reply << " 0 " << txt << " ;";
        return reply.str();
    }

    // Must be a command.
    // There have to be at least 3 more arguments!
    // The group-id and at least one pattern
    //   group_def = <command> : GRP : [arguments]*
    //   0           1           2     3 ...                    (argument index)
    const string    command( ::tolower(OPTARG(1, args)) );
    const string    groupid( OPTARG(2, args) );
    groupdef_type&  groups( mk6info.groupdefs );

    // We must have at least two arguments, of which we must recognize the first
    EZASSERT2(args.size()>2, Error_Code_6_Exception, EZINFO(" - command requires at least 2 arguments"));

    // Depending on which command we have Do Stuff (tm)
    if( command=="define" ) {
        // Verify it has enough arguments
        EZASSERT2(args.size()>3, Error_Code_6_Exception, EZINFO(" - not enough arguments"));

        // Verify we're not trying to redefine a built-in
        EZASSERT2(::isBuiltin(groupid)==false, Error_Code_6_Exception, EZINFO(" - attempt to redefine a built-in group definition"));

        // Now get to business
        groupdef_type::mapped_type     groupdefinition;
        groupdef_type::mapped_type&    plref( groups[groupid] );
        vector<string>::const_iterator argptr = args.begin();

        // skip the argument pointer to the first pattern
        advance(argptr, 3);

        // and copy all non-empty patterns into the patternlist
        remove_copy_if(argptr, args.end(), back_inserter(groupdefinition), isEmptyString());

        // Assert that the group-def actually did define _something_
        EZASSERT2(groupdefinition.size(), Error_Code_6_Exception, EZINFO(" - no actual definition specified for group " << groupid));

        // Now we can overwrite the patternlist in the definition
        plref = groupdefinition;
    } else if( command=="delete" ) {
        // Can only delete one group at a time
        EZASSERT2(args.size()==3, Error_Code_6_Exception, EZINFO(" - too many arguments"));

        // Verify we're not trying to delete a built-in. Note that we
        // weren't going to even *try* to execute this, but at least the
        // user now gets a nice error message that what he/she's trying to
        // do is not legit!

        // The test for 'isBuiltin()' should be balanced here with the one
        // under "group_def = define : ..." because we do show the built-ins
        // if you do "group_def?". This might lead, potentially, to the
        // user assuming that they are normal groups and as such can be
        // (re)defined or deleted.
        EZASSERT2(::isBuiltin(groupid)==false, Error_Code_6_Exception, EZINFO(" - attempt to delete a built-in group definition"));

        // Delete the indicated group
        groupdef_type::iterator ptr = groups.find(groupid);

        EZASSERT2(ptr!=groups.end(), Error_Code_6_Exception, EZINFO(" - unknown group " << groupid));
        groups.erase( ptr );
    } else {
        THROW_EZEXCEPT(Error_Code_6_Exception, " - unknown subcommand " << (command.empty() ? string("(empty)") : command));
    }
    reply << " 0 ;";
    return reply.str();
}
