// supporting FlexBuff/Mark5 mountpoint discovery and inspection
#ifndef EVLBI5A_MOUNTPOINT_H
#define EVLBI5A_MOUNTPOINT_H

#include <set>
#include <list>
#include <string>
#include <ezexcept.h>

typedef std::set<std::string>     mountpointlist_type;
typedef std::list<std::string>    patternlist_type;
typedef std::list<std::string>    filelist_type;

DECLARE_EZEXCEPT(mountpoint_exception)

// Transform a list of strings, interpreted as shell filename expansion patterns, into a
// list of matching directory names.
//
// Note that this function implicitly ONLY looks for directories matching
// the pattern(s). The pattern MUST address an absolute path. [Exception
// thrown if this is not the case]
//
// We support two styles of pattern:
//  1.) shell globbing patterns:
//      /mnt/disk?, /mnt/disk/*/*, /mnt/disk{0,3,8}
//  2.) full regex(3) support:
//      ^/mnt/disk[0-9]+$ , ^/dev/sd[a-z]/.+$, ^/mnt/(aap|noot)/[0-9]{1,3}$
//
// The regex interpretation is signalled by starting the pattern with
// "^" and finished with "$", otherwise it is interpreted as shell globbing.
bool                 isValidPattern(const std::string& pattern);
mountpointlist_type  find_mountpoints(const std::string& pattern);       // convenience function
mountpointlist_type  find_mountpoints(const patternlist_type& patterns);


// Find all chunks of a FlexBuff recording named 'scan' stored on the indicated mountpoints 
filelist_type        find_recordingchunks(const std::string& scan, const mountpointlist_type& mountpoints);

#endif
