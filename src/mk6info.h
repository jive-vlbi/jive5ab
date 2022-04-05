// group together Mark6 information [note: will also be used on FlexBuff]
#ifndef EVLBI5A_MK6INFO_H
#define EVLBI5A_MK6INFO_H

#include <mountpoint.h>
#include <ezexcept.h>
#include <map>
#include <list>
#include <vector>
#include <string>

#include <inttypes.h>
#include <sys/types.h>  // for off_t
#include <netinet/in.h> // struct sockaddr_in


DECLARE_EZEXCEPT(mk6exception_type)
DECLARE_EZEXCEPT(datastreamexception_type)

// Groups can be defined for a (list of) pattern(s), say an alias (see
// "group_def=" command).
// The alias will not be evaluated until it is used in a "set_disks="
// command (imminent) or "input_stream=" command (future).
//
//  pattern: /path/to/disk*/*                (shell globbing)
//           ^/path/to/(foo|bar)[0-3]/.+$    (regex style)
//           [1-4]+                          (Mark6 module shorthands)
typedef std::map<std::string, patternlist_type> groupdef_type;

typedef std::list<std::string>                  scanlist_type;
typedef std::list<std::string>                  filterlist_type;

typedef int (*fchown_fn_t)(int, uid_t, gid_t);
typedef int (*chown_fn_t)(char const*, uid_t, gid_t);

// We need to deal a bit with IPv4 addresses
extern const struct sockaddr_in                 noSender;
// equality based on IPv4 addr and port and sort by IPv4 address, then by port
inline bool operator==(struct sockaddr_in const& l, struct sockaddr_in const& r) {
    return (l.sin_addr.s_addr == r.sin_addr.s_addr) && (l.sin_port == r.sin_port);
}
inline bool operator<(struct sockaddr_in const& l, struct sockaddr_in const& r) {
    if( l.sin_addr.s_addr == r.sin_addr.s_addr )
        return l.sin_port < r.sin_port;
    return l.sin_addr.s_addr < r.sin_addr.s_addr;
}


// Handy functor
struct isEmptyString {
    inline bool operator()(std::string const& s) const {
        return s.empty();
    }
};


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
//
struct vdif_station {
    union {
        uint16_t    station_id;
        uint8_t     station_code[2];
    };

    enum type_type {
        id_numeric, id_one_char, id_two_char, id_invalid
    } type;

    
    vdif_station();
    vdif_station(uint8_t c0);
    vdif_station(uint8_t c0, uint8_t c1);
    vdif_station(uint16_t id);
};

std::ostream& operator<<(std::ostream& os, vdif_station::type_type const& vst);

struct vdif_key {
    union {
        uint16_t       station_id;
        uint8_t        station_code[2];
    };
    uint16_t           thread_id;
    struct sockaddr_in origin;

    vdif_key(std::string const& code, uint16_t t, struct sockaddr_in const& sender = noSender);
    vdif_key(char const* code, uint16_t t, struct sockaddr_in const& sender = noSender);
    vdif_key(uint16_t s, uint16_t t, struct sockaddr_in const& sender = noSender);

    bool printable_station( void ) const;

    private:
        // no default-initialized vdif_keys
        vdif_key();
};

std::ostream& operator<<(std::ostream& os, vdif_key const& vk);

// In order for these to be inlined, the compiler must see them in the
// header files
inline bool operator<(vdif_key const& l, vdif_key const& r) {
    if( l.origin == r.origin ) {
        if( l.station_id == r.station_id )
            return l.thread_id < r.thread_id;
        return l.station_id < r.station_id;
    }
    return l.origin < r.origin;
}


// Each data stream may have to match itself against an incoming vdif frame
// so this better be done fast.
// We can only match on four parameters:
//    SenderIP       0.0.0.0 = don't match
//    SenderPort     0       = don't match
//    VDIF Station   
//    VDIF Thread
//
// So we keep a list of vdif_key(s) that we accept
// together with a pointer-to-function that does the actual matching
struct vdif_key_matcher;
struct thread_matcher_type;
//struct station_matcher_type;

typedef bool (*thread_matcher_fn)(vdif_key const& l, thread_matcher_type const& r);

// can support single thread match or range lower-upper (inclusive)
struct range_type {
    uint16_t   id_low;
    uint16_t   id_high;
};
struct thread_matcher_type {
    union thread_type {
        uint16_t    thread_id;
        range_type  thread_range;

        thread_type(uint16_t t);
        thread_type(uint16_t l, uint16_t h);
    } thread;

    // The actual thread-matching fn will be set based on the c'tor
    thread_matcher_type(uint16_t tid);
    thread_matcher_type(uint16_t lo, uint16_t hi);

    thread_matcher_fn   thread_matcher;

    private:
        thread_matcher_type();
};
#if 0
// vdif station must be matchable by string or by id
// For this one there should be a default c'tor
typedef bool (*station_matcher_fn)(vdif_key const& l, station_matcher_type const& r);
struct station_matcher_type {

    station_matcher_type();
    station_matcher_type( uint16_t    sid );
    station_matcher_type( uint8_t[]  name );

    const vdif_station       station;
    const station_matcher_fn station_matcher;
};
#endif

typedef std::vector<thread_matcher_type> threadmatchers_type; 
typedef bool (*key_matching_fn)(vdif_key const& l, vdif_key_matcher const& r);

struct vdif_key_matcher {
    vdif_key_matcher(key_matching_fn mf, struct sockaddr_in const& src, vdif_station const& s, threadmatchers_type const& t);

    // Pointer to the /actual/ matching function
    // c'tor asserts that it isn't null
    size_t              nThread;
    vdif_station        station;
    key_matching_fn     match_fn;
    struct sockaddr_in  origin;
    threadmatchers_type threads;

    private:
        vdif_key_matcher();
};

typedef std::vector<vdif_key_matcher> match_criteria_type;

struct datastream_type {
    filterlist_type     match_criteria_txt; // Store original text for display
    match_criteria_type match_criteria;
    size_t              nMatch;

    datastream_type(filterlist_type const& mc);

#if 0
    bool match(vdif_key const& key) const;
#endif
    //~datastream_type();

    private:
        // no nameless datastreams please
        datastream_type();
};

typedef std::map<std::string,datastream_type> datastreamlist_type;
typedef unsigned int                          datastream_id;

typedef std::map<vdif_key, datastream_id>    vdif2tagmap_type;
typedef std::map<datastream_id, std::string> tag2namemap_type;
typedef std::map<std::string, datastream_id> name2tagmap_type;

class datastream_mgmt_type {

    public:

        void    add(std::string const& name, filterlist_type const& mc);
        void    remove(std::string const& nm);

        // Should be called before a new recording, to clear the current
        // name-to-datastream mappings
        void    reset( void );

        // clear everything
        void    clear( void );
        bool    empty( void ) const;
        size_t  size( void ) const;

        // Allow (read-only) iteration over the defined data streams
        inline datastreamlist_type::const_iterator begin( void ) const {
            return defined_datastreams.begin();
        }
        inline datastreamlist_type::const_iterator end( void ) const {
            return defined_datastreams.end();
        }
        inline datastreamlist_type::const_iterator find( datastreamlist_type::key_type const& key ) const {
            return defined_datastreams.find( key );
        }

        // given a vdif frame this returns the datastream_id it is
        // associated with
        datastream_id vdif2stream_id(uint16_t station_id, uint16_t thread_id);
        datastream_id vdif2stream_id(uint16_t station_id, uint16_t thread_id, struct sockaddr_in const& sender);

        // given a tag (datastream_id) return its name.
        // empty string implies no datastream associated with the tag
        std::string const& streamid2name( datastream_id dsid ) const;

    private:
        // Why do have several data members/mappings?
        datastreamlist_type  defined_datastreams;
        vdif2tagmap_type     vdif2tag;
        tag2namemap_type     tag2name;
        name2tagmap_type     name2tag;
};


// HV: May 2019 Currently we have the minimum block size for Mk6/FlexBuff
//              recording formats hardcoded. Let's build in a backdoor such
//              that we can change those defaults from the commandline
typedef std::map<bool, unsigned int> size_map_type;

struct mk6info_type {
    // We should discriminate between default disk location and
    // default recording format. This allows the user to fine tune
    // their setup; wether you want to record in mk6 mode on flexbuff system
    // or in flexbuff mode on mark6 disk(s)
    static bool             defaultMk6Disks;  // used in runtime c'tor to pre-find mountpoints.
                                              // effects can be undone by an explicit "set_disks="
    static bool             defaultMk6Format; // used to set the runtime's 'mk6' flag;
                                              // wether to record in mk6 format or not.
                                              // can be altered at runtime using
                                              //  "record=mk6:[1|0]"
    static size_map_type    minBlockSizeMap;  // Minimum block size for <true> (==Mk6) and <false> (==vbs)
    static bool             defaultUniqueRecordingNames; // Wether to scan for duplicate vbs scan names (global default)
                                                         // set at runtime
                                                         // "record=unique_recording_names:[0|1]"

    // If jive5ab is run suid root without dropping its privileges,we should
    // change the ownership of files or else only root can delete the files,
    // which is bad.
    // So, in main() we detect if we're running with suid root and set up
    // the function in here to do the right thing. Code can 
    // blindly call:
    //      mk6info::fchown(<filedescriptor>, mk6info::real_user_id, -1)
    // (see fchown(2))
    static uid_t            real_user_id;
    static fchown_fn_t      fchown_fn;
    static chown_fn_t       chown_fn;

    // Indicate wether we're running in Mark6 compatibility mode
    // default: of course not, d'oh!
    bool                    mk6;
    bool                    unique_recording_names;

    // Keep a list of mountpoints that we can record onto
    // this is the global list, modified by "set_disks=".
    // Initialized with all directories matching the following pattern:
    //
    //      ^/mnt/disk[0-9]+$
    //
    // i.e. the FlexBuf default
    mountpointlist_type     mountpoints;

    // Keep a mapping of group-id to list-of-patterns
    groupdef_type           groupdefs;

    // Last recording or value(s) from "scan_set=..."
    std::string             scanName;
    off_t                   fpStart, fpEnd;

    // We should keep a list of recordings made in this session,
    // a sort of in-memory DirList
    scanlist_type           dirList;

    // And which datastreams are defined
    datastream_mgmt_type    datastreams;

    // Default constructor will implement FlexBuff defaults
    mk6info_type();

    ~mk6info_type();
};

// We have a couple of built-in aliases ('groupids') that map to either
// standard Mark6 module mountpoints or FlexBuf standard mount points.
// It is impossible to re-define these group-ids
//
// The following API function will return a non-empty patternlist in case
// the indicated groupid is a built-in.
//
// We recognize:
//
//   'flexbuf'  => ^/mnt/disk[0-9]+$     (all flexbuf mountpoints)
//   '1'        => ^/mnt/disk/1/[0-7]$   (Mark6 group '1')
//   '2'        => ^/mnt/disk/2/[0-7]$   (Mark6 group '2')
//   '3'        => ^/mnt/disk/3/[0-7]$   (Mark6 group '3')
//   '4'        => ^/mnt/disk/4/[0-7]$   (Mark6 group '4')
//
// Note that, according to Mark6 manual, groups of '143' '24' &cet may also
// be specified - i.e. "[1-4]+" in (simple) regex terms. 
// Thus if the input pattern matches that, we'll accept it as 'builtin' as
// well.

bool                 isBuiltin(const std::string& groupid);
patternlist_type     patternOf(const std::string& groupid);
groupdef_type const& builtinGroupDefs( void );

// Resolve a list of (GRP|pattern) mountpoint specifications into a list of
// patterns. Implicitly uses the built-in groupdefinitions.
//
// May throw mk6info_exception if something fails to resolve (e.g. an
// undefined group definition)
//
// Note: the resolving returns a list of _unique_ patterns!
//
// Note: if the list only contains disk patterns then it basically
// returns a copy of the input.
patternlist_type     resolvePatterns(patternlist_type const& pl, groupdef_type const& userGrps);

///////////////////////////////////////////////////////////////////////////////
//    Below code copied from Jan Wagner's mark6sg FUSE file system
//    which is copied from Roger Cappallo's d-plane software ...
//
//    [HV: Why are block numbers & sizes ints and not unsigned ints?
//    We will have to follow the convention or else we may construct invalid
//    files. But why????
//    Using unsigned:
//                - gives you another factor of two in block numbers ....
//                - how can block numbers go *negative* in the first place?!?!!
//                - likewise for packet sizes: how could they ever be
//                  _negative_???
//
//           Also made the ints of actual 32-bit size; you can't really
//           leave it up to the compiler to decide how long your int really
//           is if you want portable, 64-bit clean code]


// Internal structs
//
// There is apparently no documentation on the Mark6 non-RAID recording format(?).
// From Mark6 source code one can see that the several files associated with a
// single scan all look like this:
//
//  [file header]
//  [block a header] [block a data (~10MB)]
//  [block b header] [block b data (~10MB)]
//  [block c header] [block c data (~10MB)]
//  ...
//
// Block numbers are increasing. They will have gaps (e.g, a=0, b=16, c=35, ...).
// The 'missing' blocks (1,2,3..,15,17,18,..,34,...) of one file are found in one of
// the other files. Blocks can have different data sizes.
//
// Because 10GbE recording in Mark6 software is not done Round-Robin across files,
// the order of blocks between files is somewhat random.
//
// The below structures are adopted from the source code of the
// Mark6 program 'd-plane' version 1.12:


#define MARK6_SG_SYNC_WORD          0xfeed6666


struct mk6_file_header {             // file header - one per file
    // HV: Stick the enum in the mk6_file_header struct such that
    //     the global namespace doesn't get clobbered.
    enum packet_formats {
        VDIF,
        MK5B,
        UNKNOWN_FORMAT
    };
    uint32_t  sync_word;              // MARK6_SG_SYNC_WORD
    int32_t   version;                // defines format of file
    int32_t   block_size;             // length of blocks including header (bytes)
    int32_t   packet_format;          // format of data packets, enumerated below
    int32_t   packet_size;            // length of packets (bytes)

    // We only do Version 2 so the only bits to fill in are packet format &
    // size. Block size will be taken from the first block, when the file is
    // created. Will NOT throw if any of these <0, even though I would like
    // them to
    mk6_file_header(int32_t bs, int32_t pf, int32_t ps);
};

struct mk6_wb_header_v2 {           // write block header - version 2
    int32_t blocknum;               // block number, starting at 0
    int32_t wb_size;                // same as block_size, or occasionally shorter

    mk6_wb_header_v2(int32_t bn, int32_t ws);
};



#endif
