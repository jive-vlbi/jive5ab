#include <mk6info.h>
#include <mountpoint.h>
#include <evlbidebug.h>
#include <stringutil.h>
#include <regular_expression.h>

#include <string>
#include <iostream>
#include <algorithm>

using namespace std;

DEFINE_EZEXCEPT(mk6exception_type)

// prototype of function that returns the set of built-in groupdefs
groupdef_type mk_builtins( void );


// Some module static data
static const groupdef_type      builtin_groupdefs = mk_builtins();
static const Regular_Expression rxMk6group( "^[1-4]+$" );



mk6info_type::mk6info_type() {
    groupdef_type::const_iterator fbMountPoints = builtin_groupdefs.find("flexbuf");

    EZASSERT2(fbMountPoints!=builtin_groupdefs.end(), mk6exception_type, EZINFO(" - no pattern for builtin group 'flexbuf' found?!!"));

    mountpoints = find_mountpoints(fbMountPoints->second);
    DEBUG(3, "mk6info - found " << mountpoints.size() << " FlexBuf mountpoints" << endl);

    string  lst;
    copy(mountpoints.begin(), mountpoints.end(), ostringiterator(lst, ", "));
    DEBUG(4, "mk6info - " << lst << endl);
}

mk6info_type::~mk6info_type() {}

/////////////////////////////////////////////////////////////////////
//
//                    User functions / the API
//
/////////////////////////////////////////////////////////////////////
bool isBuiltin(const string& groupid) {
    groupdef_type::const_iterator   ptr = builtin_groupdefs.find(groupid);

    return rxMk6group.matches(groupid) || (ptr!=builtin_groupdefs.end());
}

patternlist_type patternOf(const string& groupid) {
    // If this a Mark6 groupid (may be describing more than one module) we
    // must accumulate all the patterns that are contained in the group:
    // e.g. "groupid=124" should evaluate to:
    //     [^/mnt/disk/1/[0-7]$, ^/mnt/disk/2/[0-7]$, ^/mnt/disk/4/[0-7]$]
    patternlist_type              rv;
    groupdef_type::const_iterator ptr = builtin_groupdefs.find(groupid);

    // Direct match
    if( ptr!=builtin_groupdefs.end() )
        copy(ptr->second.begin(), ptr->second.end(), inserter(rv, rv.begin()));
    else if( rxMk6group.matches(groupid) ) {
        // Loop over all entries in the Mark6 groupid
        for(string::const_iterator p=groupid.begin(); p!=groupid.end(); p++) {
            // Assert that we have a pattern for this element
            groupdef_type::const_iterator curptr = builtin_groupdefs.find(string(1, *p));

            EZASSERT2(curptr!=builtin_groupdefs.end(), mk6exception_type,
                      EZINFO(" - internal inconsistency, rxMk6group matches but no pattern found for group " << *p));
            // Copy all patterns associated with this module into the return
            // value
            copy(curptr->second.begin(), curptr->second.end(), inserter(rv, rv.begin()));
        }
    }
    return rv;
}

groupdef_type const& builtinGroupDefs( void ) {
    return builtin_groupdefs;
}

///////////////////////// resolve a list of patterns ///////////////////////

struct patternLookup {
    patternLookup(groupdef_type const& d1):
        dict1ref(d1)
    {}

    // assume grpdef is a key in any of the dicts. If it isn't, we throw an
    // exception.
    patternlist_type operator()(string const& grpdef) {
        patternlist_type                builtin_pattern( ::patternOf(grpdef) );
        groupdef_type::const_iterator   p = dict1ref.find(grpdef);

        // Only returns non-empty if it *was* a builtin pattern!
        if( builtin_pattern.size() )
            return builtin_pattern;

        EZASSERT2(p!=dict1ref.end(), mk6exception_type,
                EZINFO(" - group definition '" << grpdef << "' not found in dictionaries"));
        return p->second;
    }

    private:
        groupdef_type const&    dict1ref;

        // these had better not exist
        patternLookup();
        patternLookup(const patternLookup&);
        patternLookup& operator=(const patternLookup&);
};

patternlist_type resolvePatterns(patternlist_type const& pl, groupdef_type const& userGrps) {
    typedef std::set<std::string>  accumulator_type;
    patternLookup       lookup( userGrps );
    accumulator_type    accumulator;
    patternlist_type    remaining( pl );

    // Keep on resolving until the patternlist is empty
    while( remaining.size() ) {
        // Partition the current patternlist into two parts:
        // those patterns that are valid patterns and those that are not
        // [remaining.begin(), iter)   = all direct patterns [do not require further resolving]
        // [iter, remaining.end())     = anything still needing to be resolved
        patternlist_type             tmp;
        patternlist_type::iterator   iter = partition(remaining.begin(), remaining.end(), &::isValidPattern);

        // Copy the direct patterns into the accumulator
        copy(remaining.begin(), iter, inserter(accumulator, accumulator.begin()));

        // Loop over the remaining aliases - they should be lookup-able
        for( ; iter!=remaining.end(); iter++ ) {
            patternlist_type    dereferenced( lookup(*iter) );
            copy(dereferenced.begin(), dereferenced.end(), back_inserter(tmp));
        }

        // replace remaining with tmp
        remaining = tmp;
    }

    // Transform the set of unique patterns to a list
    patternlist_type    rv;
    copy(accumulator.begin(), accumulator.end(), back_inserter(rv));
    return rv;
}




/////////////////////////////////////////////////////////////////////
//
//                    The built-in group defs
//
/////////////////////////////////////////////////////////////////////

list<string> one_elem_list(string const* s) {
    return list<string>(s, s+1);
}

groupdef_type mk_builtins( void ) {
    const string    groups[] = {
        "^/mnt/disk/1/[0-7]$",
        "^/mnt/disk/2/[0-7]$",
        "^/mnt/disk/3/[0-7]$",
        "^/mnt/disk/4/[0-7]$",
        "^/mnt/disk[0-9]+$"
    };
    groupdef_type   rv;

    EZASSERT( rv.insert(make_pair("1",       one_elem_list(&groups[0]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("2",       one_elem_list(&groups[1]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("3",       one_elem_list(&groups[2]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("4",       one_elem_list(&groups[3]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("flexbuf", one_elem_list(&groups[4]))).second, mk6exception_type );
    return rv;
}
