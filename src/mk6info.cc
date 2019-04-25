#include <mk6info.h>
#include <mountpoint.h>
#include <evlbidebug.h>
#include <stringutil.h>
#include <regular_expression.h>

#include <string>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>

#include <unistd.h>
#include <cstring>   // for strlen()
#include <ctype.h>   // for isprint()

using namespace std;

DEFINE_EZEXCEPT(mk6exception_type)
DEFINE_EZEXCEPT(datastreamexception_type)

// prototype of function that returns the set of built-in groupdefs
groupdef_type mk_builtins( void );
groupdef_type findMountedModules( void );

// Some module static data
static const groupdef_type      builtin_groupdefs = mk_builtins();
static const Regular_Expression rxMk6group( "^[1-4]+$" );


// jive5ab defaults are flexbuff mountpoints and flexbuff recording
bool mk6info_type::defaultMk6Disks  = false;
bool mk6info_type::defaultMk6Format = false;

// default do-nothing (f)chown functionality
static int no_fchown(int, uid_t, gid_t) { 
    // does nothing, succesfully
    return 0;
}
static int no_chown(char const*, uid_t, gid_t) { 
    // does nothing, succesfully
    return 0;
}

uid_t       mk6info_type::real_user_id = (uid_t)-1;
fchown_fn_t mk6info_type::fchown_fn    = no_fchown;
chown_fn_t  mk6info_type::chown_fn     = no_chown;


// Replace {thread} by key.thread_id
//  {station} by key.station_code or key.station_id
std::string replace_fields(std::string const& input, vdif_key const& key) {
    std::string::size_type  threadptr  = input.find("{thread}");
    std::string::size_type  stationptr = input.find("{station}");

    if( threadptr==std::string::npos && stationptr==std::string::npos )
        return input;
    // Ok now we *know* we need to modify the string. Make a copy for that.
    std::string  inputcp( input );

    // Replace all occurrences
    std::ostringstream  oss;

    // Any "{thread}"'s to replace?
    if( threadptr!=std::string::npos ) {
        // compute the replacement
        oss.str( std::string() );
        oss << key.thread_id;
        const std::string thread = oss.str();
        // and do the replacements
        do {
            inputcp   = inputcp.replace(threadptr, 8, thread);
            threadptr = inputcp.find("{thread}");
        } while( threadptr!=std::string::npos );
    }
    // "{station}"s maybe?
    if( stationptr!=std::string::npos ) {
        // compute the replacement
        oss.str( std::string() );
        if( key.printable_station() )
            oss << (char)key.station_code[1] << (char)key.station_code[0];
        else
            oss << std::setw(4) << std::hex << key.station_id;
        const std::string station = oss.str();
        // and do the replacements
        do {
            inputcp   = inputcp.replace(stationptr, 9, station);
            stationptr = inputcp.find("{station}");
        } while( stationptr!=std::string::npos );
    }
    return inputcp;
}

// Keeping track of datastreams
datastream_type::datastream_type(std::string const& mc) :
    match_criteria(mc)
{}

bool datastream_type::match(vdif_key const& /*key*/) const {
    // returns wether the vdif key actually matches anything in this data stream
    return true;
}

//datastream_type::~datastream_type()
//{}


vdif_key::vdif_key(std::string const& code, uint16_t t):
    thread_id( t )
{
    EZASSERT2( code.size() <= 2 && !code.empty(), datastreamexception_type,
               EZINFO("VDIF Station code must be 1 or 2 characters, not '" << code << "'") );
    station_code[1] = code[0];
    station_code[0] = code.size()>1 ? code[1] : ' '; // std::string is not necessarily NUL terminated!
}

vdif_key::vdif_key(char const* code, uint16_t t):
    thread_id( t )
{
    EZASSERT2( ::strlen(code) <= 2 && station_code[0]!='\0', datastreamexception_type,
               EZINFO("VDIF Station code must be 1 or 2 characters, not '" << code << "'") );
    station_code[1] = code[0];
    station_code[0] = code[1] ? code[1] : ' '; // No NULs in our "string"
}
vdif_key::vdif_key(uint16_t s, uint16_t t):
    station_id(s), thread_id(t)
{}

bool vdif_key::printable_station( void ) const {
    return ::isprint(station_code[1]) && (station_code[0] ? ::isprint(station_code[0]) : true);
}

std::ostream& operator<<(std::ostream& os, vdif_key const& vk) {
    os << "VDIF<";
    if( vk.printable_station() )
        os << "station=" << (char)vk.station_code[1] << (char)vk.station_code[0];
    else
        os << "station_id=" << std::setw(4) << std::hex << vk.station_id;
    os << ", thread_id=" << vk.thread_id << ">";
    return os;
}

// datastream management is encapsulated in one class
void datastream_mgmt_type::add_datastream(std::string const& nm, std::string const& mc) {
    // check if not already defined
    if( defined_datastreams.find(nm)!=defined_datastreams.end() )
        THROW_EZEXCEPT(datastreamexception_type, "The data stream '" << nm << "' already has a definition");
    // attempt to insert into the map
    std::pair<datastreamlist_type::iterator, bool> insres = defined_datastreams.insert( std::make_pair(nm, datastream_type(mc)) );
    if( !insres.second )
        THROW_EZEXCEPT(datastreamexception_type, "Failed to insert he data stream '" << nm << "' ??? (internal error in std::map?)");
}


void datastream_mgmt_type::delete_datastream(std::string const& nm) {
    datastreamlist_type::iterator ptr = defined_datastreams.find(nm);

    if( ptr==defined_datastreams.end() )
        THROW_EZEXCEPT(datastreamexception_type, "The data stream '" << nm << "' was not defined so cannot remove it");
    defined_datastreams.erase( ptr );
}

void datastream_mgmt_type::clear_runtime() {
    vdif2tag.clear();
    tag2name.clear();
    name2tag.clear();
}

void datastream_mgmt_type::clear_all() {
    this->clear_runtime();
    defined_datastreams.clear();
}


// 1) a. we manage the list of currently defined datastreams.
//    each of these may match specific vdif atrributes (station,
//    thread)
//    b. the actual _name_ of the data stream could be a pattern,
//    such that the name of the stream depends on what is actually 
//    received
// 2) so we keep the list of original definitions (match criteria +
//    name pattern)
// 3) if a user requests the data stream id for a specific vdif
//    frame, we check if we already have an entry for those
//    If we don't: find a match in the defined data streams and
//    generate the actual name for the data stream.
//    Check if that stream already has an id and otherwise generate
//    one.
datastream_id datastream_mgmt_type::vdif2stream_id(uint16_t station_id, uint16_t thread_id) {
    static const struct sockaddr_in noSender = { .sin_family = AF_INET, .sin_port = 0, .sin_addr = { .s_addr = INADDR_NONE } };
    return this->vdif2stream_id(station_id, thread_id, noSender);
#if 0
    vdif_key const             key(station_id, thread_id);
    vdif2tagmap_type::iterator ptr = vdif2tag.find( key );

    if( ptr!=vdif2tag.end() )
        return ptr->second;

    // Crap, haven't seen this one before. Go find which datastream this'un
    // belongs to
    datastreamlist_type::iterator  dsptr;
    for( dsptr=defined_datastreams.begin(); dsptr!=defined_datastreams.end(); dsptr++ )
        if( dsptr->second.match(key) )
            break;

    if( dsptr==defined_datastreams.end() )
        THROW_EZEXCEPT(datastreamexception_type, "No defined datastream for VDIF key " << key);

    // Translate the datastream's key to an actual name (in case it is a
    // pattern)
    std::string const    datastream_name( ::replace_fields(dsptr->first, key) );

    // Check if this specific data stream name already has an ID assigned
    name2tagmap_type::iterator p = name2tag.find( datastream_name );

    if( p==name2tag.end() ) {
        // Bugger. Need to allocate a new entry
        std::pair<name2tagmap_type::iterator, bool> insres = name2tag.insert( std::make_pair(datastream_name, name2tag.size()) );
        if( !insres.second )
            THROW_EZEXCEPT(datastreamexception_type, "Failed to insert name for data stream '" << datastream_name << "'");
        p = insres.first;
        // Also put it in the reverse mapping
        std::pair<tag2namemap_type::iterator, bool> sersni = tag2name.insert( std::make_pair(p->second, p->first) );
        if( !sersni.second ) {
            name2tag.erase( p );
            THROW_EZEXCEPT(datastreamexception_type, "Failed to insert reverse mapping name for data stream '" << datastream_name << "'");
        }
    }

    // all that's left is to add an entry in the  vdif->data_stream map
    if( vdif2tag.insert(std::make_pair(key, p->second)).second==false )
        THROW_EZEXCEPT(datastreamexception_type, "Failed to insert mapping for " << key << " to data stream #" << p->second);
    return p->second;
#endif
}

datastream_id datastream_mgmt_type::vdif2stream_id(uint16_t station_id, uint16_t thread_id, struct sockaddr_in const& sender) {
    vdif_key const             key(station_id, thread_id);
    vdif2tagmap_type::iterator ptr = vdif2tag.find( key );

    if( ptr!=vdif2tag.end() )
        return ptr->second;

    // Crap, haven't seen this one before. Go find which datastream this'un
    // belongs to
    datastreamlist_type::iterator  dsptr;
    for( dsptr=defined_datastreams.begin(); dsptr!=defined_datastreams.end(); dsptr++ )
        if( dsptr->second.match(key) )
            break;

    if( dsptr==defined_datastreams.end() )
        THROW_EZEXCEPT(datastreamexception_type, "No defined datastream for VDIF key " << key);

    // Translate the datastream's key to an actual name (in case it is a
    // pattern)
    std::string const    datastream_name( ::replace_fields(dsptr->first, key) );

    // Check if this specific data stream name already has an ID assigned
    name2tagmap_type::iterator p = name2tag.find( datastream_name );

    if( p==name2tag.end() ) {
        // Bugger. Need to allocate a new entry
        std::pair<name2tagmap_type::iterator, bool> insres = name2tag.insert( std::make_pair(datastream_name, name2tag.size()) );
        if( !insres.second )
            THROW_EZEXCEPT(datastreamexception_type, "Failed to insert name for data stream '" << datastream_name << "'");
        p = insres.first;
        // Also put it in the reverse mapping
        std::pair<tag2namemap_type::iterator, bool> sersni = tag2name.insert( std::make_pair(p->second, p->first) );
        if( !sersni.second ) {
            name2tag.erase( p );
            THROW_EZEXCEPT(datastreamexception_type, "Failed to insert reverse mapping name for data stream '" << datastream_name << "'");
        }
    }

    // all that's left is to add an entry in the  vdif->data_stream map
    if( vdif2tag.insert(std::make_pair(key, p->second)).second==false )
        THROW_EZEXCEPT(datastreamexception_type, "Failed to insert mapping for " << key << " to data stream #" << p->second);
    return p->second;
}


// Keep track of Mark6/FlexBuff properties
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

// Update: 04 Nov 2015: cplane/dplane (Mark6 software) writes data
//                      one level deeper than just in the root
//                      of the disk, in a directory called "data"
groupdef_type mk_builtins( void ) {
    const string    groups[] = {
        "^/mnt/disks/1/[0-7]/data$",
        "^/mnt/disks/2/[0-7]/data$",
        "^/mnt/disks/3/[0-7]/data$",
        "^/mnt/disks/4/[0-7]/data$",
        "^/mnt/disk[0-9]+$",
        "^/mnt/disks/[1234]/[0-7]/data$"
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


// Update: 04 Nov 2015: data is located at one level deeper, in
//                      a directory called "/data"
// Need a function which transforms 
//   /mnt/disks/.meta/[1234]/[0-7]/eMSN contents [if exist] into 
//       (MSN, "/mnt/disks/[123]/[0-7]/data")  [see update above]


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

        // HV: 04 Nov 2015  Helge R. and Walter A. find that
        //                  cplane/dplane actually append
        //                  an extra level of ".../data/" to 
        //                  the mount points such that data files
        //                  end up in:
        //                  /mnt/disks/<module>/<disk>/data/<recording>
        mpstrm  << "/mnt/disks/" << module << "/" << disk << "/data";

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
