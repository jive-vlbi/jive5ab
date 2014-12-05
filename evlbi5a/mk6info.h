// group together Mark6 information [note: will also be used on FlexBuff]
#ifndef EVLBI5A_MK6INFO_H
#define EVLBI5A_MK6INFO_H

#include <mountpoint.h>
#include <map>

#include <ezexcept.h>

DECLARE_EZEXCEPT(mk6exception_type)

// Groups can be defined for a (list of) pattern(s), say an alias (see
// "group_def=" command).
// The alias will not be evaluated until it is used in a "set_disks="
// command (imminent) or "input_stream=" command (future).
//
//  pattern: /path/to/disk*/*                (shell globbing)
//           ^/path/to/(foo|bar)[0-3]/.+$    (regex style)
//           [1-4]+                          (Mark6 module shorthands)
typedef std::map<std::string, patternlist_type> groupdef_type;


struct mk6info_type {

    // Keep a list of mountpoints that we can record onto
    // this is the global list, modified by "set_disks=".
    // Initialized with all directories matching the following pattern:
    //
    //      ^/mnt/disk[0-9]+$
    //
    // i.e. the FlexBuf default
    mountpointlist_type mountpoints;

    // Keep a mapping of group-id to list-of-patterns
    groupdef_type       groupdefs;

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

#endif
