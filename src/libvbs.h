#ifndef LIBVBS_H
#define LIBVBS_H

#include <sys/types.h>

/* API for FlexBuff recordings */
#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Attempt to open a recording named 'recname'.
 * The path indicated by 'rootdir' is scanned for
 * entries of the name "disk<number>" and will
 * assume those to be flexbuff mount points.
 *
 * It will search for all chunks for the recording 'recname'
 * inside those mountpoints.
 *
 * On error it will return -1 and set errno, otherwise a file descriptor
 * usable for future calls to vbs_open(), vbs_close(), vbs_lseek() and
 * vbs_read()
int     vbs_open( char const* recname, char const* const rootdir );
 */

/* 
 * Almost equal to vbs_open() with this difference:
 * This does not look for entries called "disk<number>" in each root
 * directory but assumes that each path is already a flexbuf mountpoint,
 * potentially containing VBS style recordings.
 *
 * rootdirs should be a NULL-terminated array of pointers to C-'strings'
 * (NTBS)
 *
 * On error it will return -1 and set errno, otherwise 0.
 */
int     vbs_open( char const* recname, char const* const * rootdirs );

/*
 * Open a Mark6-formatted recording.
 * We do not feature a version with just rootdir; on Mark6 there is 
 * no single rootdir for the disks to reside in.
 *
 * If sucessfully opened returns file descriptor>0 which can be used in
 * subsequent calls to vbs_[read, lseek, close].
 */
int     mk6_open( char const* recname, char const* const* rootdirs );

/* Open a fake recording (no associated bytes-on-disk) of maximum size
 * 'maxsize'
 * vbs_read(), vbs_lseek() and vbs_close() operate as expected; only 
 * vbs_read() does not change the contents of the buffer supplied to it, it
 * only checks if the "read()" can be succesfully honoured and the return
 * value reflects this.
 */
int     null_open( off_t maxsize );

/* Normal Unix-style file API */
ssize_t vbs_read(int fd, void* buf, size_t count);
off_t   vbs_lseek(int fd, off_t offset, int whence);
int     vbs_close(int fd);

#if 0
/* Set library debug level. Higher, positive, numbers produce more output. Returns
 * previous level, default is "0", no output. */
int     vbs_setdbg(int newlevel);
#endif

#ifdef __cplusplus
}
#endif

#endif
