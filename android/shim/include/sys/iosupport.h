/*
 * newlib's (devkitARM) device table, as far as 3DS code uses it: replacing
 * the devices behind stdout and stderr. bionic has no such layer, so the
 * shim (src/stdio.c) makes stdout and stderr streams that pass every write
 * to devoptab_list[STD_OUT] / devoptab_list[STD_ERR]->write_r, looked up at
 * write time, so assigning an entry takes effect immediately, as in newlib.
 * The other operations are never called.
 */
#ifndef CTRSHIM_SYS_IOSUPPORT_H
#define CTRSHIM_SYS_IOSUPPORT_H

#include <stddef.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _reent;
struct statvfs;

enum
{
    STD_IN,
    STD_OUT,
    STD_ERR,
    STD_MAX = 35
};

typedef struct
{
    void *device;
    void *dirStruct;
} DIR_ITER;

typedef struct
{
    const char *name;
    size_t structSize;
    int (*open_r)(struct _reent *r, void *fileStruct, const char *path, int flags, int mode);
    int (*close_r)(struct _reent *r, void *fd);
    ssize_t (*write_r)(struct _reent *r, void *fd, const char *ptr, size_t len);
    ssize_t (*read_r)(struct _reent *r, void *fd, char *ptr, size_t len);
    off_t (*seek_r)(struct _reent *r, void *fd, off_t pos, int dir);
    int (*fstat_r)(struct _reent *r, void *fd, struct stat *st);
    int (*stat_r)(struct _reent *r, const char *file, struct stat *st);
    int (*link_r)(struct _reent *r, const char *existing, const char *newLink);
    int (*unlink_r)(struct _reent *r, const char *name);
    int (*chdir_r)(struct _reent *r, const char *name);
    int (*rename_r)(struct _reent *r, const char *oldName, const char *newName);
    int (*mkdir_r)(struct _reent *r, const char *path, int mode);
    size_t dirStateSize;
    DIR_ITER *(*diropen_r)(struct _reent *r, DIR_ITER *dirState, const char *path);
    int (*dirreset_r)(struct _reent *r, DIR_ITER *dirState);
    int (*dirnext_r)(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
    int (*dirclose_r)(struct _reent *r, DIR_ITER *dirState);
    int (*statvfs_r)(struct _reent *r, const char *path, struct statvfs *buf);
    int (*ftruncate_r)(struct _reent *r, void *fd, off_t len);
    int (*fsync_r)(struct _reent *r, void *fd);
    void *deviceData;
    int (*chmod_r)(struct _reent *r, const char *path, mode_t mode);
    int (*fchmod_r)(struct _reent *r, void *fd, mode_t mode);
    int (*rmdir_r)(struct _reent *r, const char *name);
    int (*lstat_r)(struct _reent *r, const char *file, struct stat *st);
    int (*utimes_r)(struct _reent *r, const char *filename, const struct timeval times[2]);
} devoptab_t;

extern const devoptab_t *devoptab_list[];

#ifdef __cplusplus
}
#endif

#endif
