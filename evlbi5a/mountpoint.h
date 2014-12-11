// supporting FlexBuff/Mark5 mountpoint discovery and inspection
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


// Convenience wrapper around pthread_create(3) - creates a joinable thread
// with ALL SIGNALS BLOCKED. Return value is the value returned from
// any of the pthread_*(3) calls made [pthread_attr_init(3),
// pthread_attr_set_detachstate(3), pthread_sigmask(3), pthread_create(3)].
int mp_pthread_create(pthread_t* thread, void *(*start_routine)(void*), void *arg);


// The operating system keeps a list of mounted file systems.
// We need to access this list too. This interface is a wrapper around O/S
// specific method(s) to retrieve this list.
//
// We're mostly interested in finding out on which device the root file
// system is such that we can filter the user selected directories by the
// ones that do not live on that file system.
//
// This is needed to prevent the flexbuf recording filling up the root file
// system.
struct sysmountpoint_type {
    // Construct a mounted path and which physical device it maps to
    sysmountpoint_type(std::string const& p, std::string const& dev);

    const std::string   path;
    const std::string   device;
};


typedef std::list<sysmountpoint_type>   sysmountpointlist_type;

// Retrieve list of currently mounted devices and whence they're mounted
sysmountpointlist_type find_sysmountpoints( void );


#endif
