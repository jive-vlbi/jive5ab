#ifndef LIBVBS_H
#define LIBVBS_H

#include <sys/types.h>

/* API for FlexBuff recordings */
#ifdef __cplusplus
extern "C" {
#endif

/* Unfortunately, it IS necessary to initialize the library -
 * mostly to verify the root dir where all the data disks are mounted
 * does exist and we can read it etc.
 * It is possible to re-initialize the library as often as desired,
 * as long as no files are open at the time of vbs_init()
 *
 * The path is scanned for entries of the name "disk<number>" and will
 * assume those to be flexbuff mount points.
 *
 * On error it will return -1 and set errno, otherwise 0.
 */
int     vbs_init( char const* const rootdir );

/* This does not look for entries called "disk<number>" in each root
 * directory but assumes that each path potentially contains VBS style
 * recording(s). Terms and conditions about not being able to call it when
 * files are opened (see "vbs_init()" do apply.
 *
 * rootdirs should be a NULL-terminated array of pointers to C-'strings'
 * (NTBS)
 *
 * On error it will return -1 and set errno, otherwise 0.
 */
int     vbs_init2( char const* const * rootdirs );

/* Normal Unix-style file API */
int     vbs_open(char const* recname);
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
