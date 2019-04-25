// group together Mark6 information [note: will also be used on FlexBuff]
#ifndef EVLBI5A_MK6INFO_H
#define EVLBI5A_MK6INFO_H

#include <mountpoint.h>
#include <ezexcept.h>
#include <map>
#include <list>
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

typedef int (*fchown_fn_t)(int, uid_t, gid_t);
typedef int (*chown_fn_t)(char const*, uid_t, gid_t);

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
struct vdif_key {
    union {
        uint16_t    station_id;
        uint8_t     station_code[2];
    };
    uint16_t    thread_id;

    vdif_key(std::string const& code, uint16_t t);
    vdif_key(char const* code, uint16_t t);
    vdif_key(uint16_t s, uint16_t t);

    bool printable_station( void ) const;

    private:
        // no default-initialized vdif_keys
        vdif_key();
};

std::ostream& operator<<(std::ostream& os, vdif_key const& vk);

// define the sort operation based on vdif_key
inline bool operator<(vdif_key const& l, vdif_key const& r) {
    if( l.station_id == r.station_id )
        return l.thread_id < r.thread_id;
    return l.station_id < r.station_id;
}


struct datastream_type {
    const std::string   match_criteria;

    datastream_type(std::string const& mc);

    bool match(vdif_key const& key) const;
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

        void add_datastream(std::string const& name, std::string const& mc);
        void delete_datastream(std::string const& nm);

        // Should be called before a new recording, to clear the current
        // name-to-datastream mappings
        void clear_runtime( void );

        // clear everything
        void clear_all( void );

        // given a vdif frame this returns the datastream_id it is
        // associated with
        datastream_id vdif2stream_id(uint16_t station_id, uint16_t thread_id);
        datastream_id vdif2stream_id(uint16_t station_id, uint16_t thread_id, struct sockaddr_in const& sender);

    private:
        // Why do have several data members/mappings?
        datastreamlist_type  defined_datastreams;
        vdif2tagmap_type     vdif2tag;
        tag2namemap_type     tag2name;
        name2tagmap_type     name2tag;
};


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
