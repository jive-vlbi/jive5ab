#include <mk6info.h>
#include <mountpoint.h>
#include <evlbidebug.h>
#include <stringutil.h>
#include <regular_expression.h>

#include <string>
#include <fstream>
#include <iostream>
#include <algorithm>

#include <unistd.h>

using namespace std;

DEFINE_EZEXCEPT(mk6exception_type)

// prototype of function that returns the set of built-in groupdefs
groupdef_type mk_builtins( void );
groupdef_type findMountedModules( void );

// Some module static data
static const groupdef_type      builtin_groupdefs = mk_builtins();
static const Regular_Expression rxMk6group( "^[1-4]+$" );


// jive5ab defaults are flexbuff mountpoints and flexbuff recording
bool mk6info_type::defaultMk6Disks  = false;
bool mk6info_type::defaultMk6Format = false;


mk6info_type::mk6info_type():
    mk6( mk6info_type::defaultMk6Format ), fpStart( 0 ), fpEnd( 0 )
{
    const string                  mpString      = (mk6info_type::defaultMk6Disks ? "mk6" : "flexbuf");
    groupdef_type::const_iterator fbMountPoints = builtin_groupdefs.find(mpString);

    EZASSERT2(fbMountPoints!=builtin_groupdefs.end(), mk6exception_type,
              EZINFO(" - no builtin pattern for '" << mpString << "' found?!!"));

    mountpoints = find_mountpoints(fbMountPoints->second);
    DEBUG(-1, "mk6info - found " << mountpoints.size() << " " << (mk6info_type::defaultMk6Disks ? "Mark6" : "FlexBuff") <<
              " mountpoints" << endl);

    string  lst;
    copy(mountpoints.begin(), mountpoints.end(), ostringiterator(lst, ", "));
    DEBUG(4, "mk6info - " << lst << endl);
}

mk6info_type::~mk6info_type() {}



/////////////////////////////////////////////////////////////////////
//
//                    MIT Haystack d-plane emulation helper structs
//
/////////////////////////////////////////////////////////////////////

// Construct a Mk6 file header. Note that if the packet_size is not set,
// we'll set it to the block size such that #pkts/block will evaluate to 1
mk6_file_header::mk6_file_header(int32_t bs, int32_t pf, int32_t ps):
    sync_word( MARK6_SG_SYNC_WORD ), version( 2 ),
    block_size( bs+sizeof(mk6_wb_header_v2) ), packet_format( pf ), packet_size( ps==0 ? bs : ps )
{
    // At the moment, do not throw upon these failures because
    //  'parallel_writer()' in "threadfns/multisender.cc" 
    //  cannot deal with exceptions being thrown whilst attempting to
    //  write a file/block
    //EZASSERT2(block_size>0,  mk6exception_type, EZINFO("Mark6 can only do blocks of max 2.1 GB"));
    //EZASSERT2(packet_size>0, mk6exception_type, EZINFO("Negative packet size is unreal man!"));
}

mk6_wb_header_v2::mk6_wb_header_v2(int32_t bn, int32_t ws):
    blocknum( bn ), wb_size( ws + sizeof(mk6_wb_header_v2) )  /* We only write v2 files! */
{
    // For reason why not actually throw yet, see above, mk6_file_header
    // constructor.
    //EZASSERT2(blocknum>=0,  mk6exception_type, EZINFO("Block numbers cannot go negative, really"));
    //EZASSERT2(wb_size>0, mk6exception_type, EZINFO("Mark6 can only do blocks of max 2.1 GB"));
}


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
    // Assume [...] dict2 is the dict of MSNs! This is important because
    // (see below)
    patternLookup(groupdef_type const& d1, groupdef_type const& d2):
        dict1ref(d1), dict2ref(d2)
    {}

    // assume grpdef is a key in any of the dicts. If it isn't, we throw an
    // exception.
    patternlist_type operator()(string const& grpdef) {
        patternlist_type                builtin_pattern( ::patternOf(grpdef) );
        groupdef_type::const_iterator   p1 = dict1ref.find(grpdef);
        groupdef_type::const_iterator   p2 = dict2ref.find(::toupper(grpdef)); // Lookup MSNs case insensitive [implicit]

        // Only returns non-empty if it *was* a builtin pattern!
        if( builtin_pattern.size() )
            return builtin_pattern;

        // Resolvable if either p1 or p2 found
        EZASSERT2(p1!=dict1ref.end() || p2!=dict2ref.end(), mk6exception_type,
                EZINFO(" - group definition '" << grpdef << "' not found in dictionaries"));
        return p1!=dict1ref.end() ? p1->second : p2->second;
    }

    private:
        groupdef_type const&    dict1ref;
        groupdef_type const&    dict2ref;

        // these had better not exist
        patternLookup();
        patternLookup(const patternLookup&);
        patternLookup& operator=(const patternLookup&);
};

patternlist_type resolvePatterns(patternlist_type const& pl, groupdef_type const& userGrps) {
    typedef std::set<std::string>  accumulator_type;
    groupdef_type       msns( ::findMountedModules() );
    patternLookup       lookup( userGrps, msns );
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
        "^/mnt/disks/1/[0-7]$",
        "^/mnt/disks/2/[0-7]$",
        "^/mnt/disks/3/[0-7]$",
        "^/mnt/disks/4/[0-7]$",
        "^/mnt/disk[0-9]+$",
        "^/mnt/disks/[1234]/[0-7]$"
    };
    groupdef_type   rv;

    EZASSERT( rv.insert(make_pair("1",       one_elem_list(&groups[0]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("2",       one_elem_list(&groups[1]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("3",       one_elem_list(&groups[2]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("4",       one_elem_list(&groups[3]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("flexbuf", one_elem_list(&groups[4]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("flexbuff",one_elem_list(&groups[4]))).second, mk6exception_type );
    EZASSERT( rv.insert(make_pair("mk6",     one_elem_list(&groups[5]))).second, mk6exception_type );
    return rv;
}


/////////////////////////////////////////////////////////////////////
//
//                    Find all mounted modules (and which mountpoints
//                    they're mounted on)
//
/////////////////////////////////////////////////////////////////////


// Need a function which transforms 
//   /mnt/disks/.meta/[1234]/[0-7]/eMSN contents [if exist] into 
//       (MSN, "/mnt/disks/[123]/[0-7]")


struct msndisk_type {
    const string    MSN;
    const string    mountpoint;

    msndisk_type() { }

    msndisk_type(const string& msn, const string& path):
        MSN(msn), mountpoint(path)
    {}

    // Conversion to bool: 
    //   non-zero length of both MSN and mountpoint => true
    //   false otherwise
    operator bool( void ) const {
        return !(MSN.empty() || mountpoint.empty());
    }
};

// function attempting to read MSN.
// This assumes the eMSN file IS there; will fail
// if we can't read it or the contents seem b0rked
// The "eMSN" file contains one line of text:
//      LABL0000/<capacity>/<digit?>/<number of disks>
string readMSN(const string& fn) {
    string            msn;
    ifstream          f( fn.c_str() );
    string::size_type slash;

    f >> msn;
    f.close();
    EZASSERT2(msn.empty()==false, mk6exception_type, EZINFO("failed to read MSN from " << fn));

    if( (slash=msn.find("/"))!=string::npos )
        msn = msn.substr(0, slash);
    DEBUG(4, "readMSN(" << fn << ") => " << msn << endl);
    return ::toupper(msn); 
}

// For a given module, disk, attempt to read the MSN and if succesful 
// return the msndisk_type() for this disk
msndisk_type checkDisk(const unsigned int module, const unsigned int disk) {
    ostringstream  msnstrm;

    msnstrm << "/mnt/disks/.meta/" << module << "/" << disk << "/eMSN";

    // If the eMSN file exists, we try to read it
    if( ::access(msnstrm.str().c_str(), R_OK)==0 ) {
        ostringstream mpstrm;

        mpstrm  << "/mnt/disks/" << module << "/" << disk;

        return msndisk_type(::readMSN(msnstrm.str()), mpstrm.str());
    } else {
        DEBUG(4, "checkDisk(" << module << ", " << disk << ")/No R access to " << msnstrm.str() << endl);
    }
    return msndisk_type();
}

groupdef_type findMountedModules( void ) {
    groupdef_type   rv;

    // Loop over all modules, disks and collect the results
    for(unsigned int mod=1; mod<5; mod++)
        for(unsigned int disk=0; disk<8; disk++) 
            if(msndisk_type  msndisk = ::checkDisk(mod, disk))
                rv[ msndisk.MSN ].push_back( msndisk.mountpoint );

#if 0
    cout << "findMountedModules:" << endl;
    for(groupdef_type::const_iterator c=rv.begin(); c!=rv.end(); c++)
        for(patternlist_type::const_iterator mp=c->second.begin(); mp!=c->second.end(); mp++)
            cout << ((mp==c->second.begin())?(c->first):("      ")) << *mp << endl;
#endif
    return rv;
}
