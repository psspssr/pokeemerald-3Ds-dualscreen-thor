/*
 * "romfs:" and "sdmc:" paths. On the console these are newlib devices; here
 * libemerald.so is linked with --wrap=<symbol> for every libc entry point in
 * wrap.txt, so origin's fopen("sdmc:/...") lands in __wrap_fopen, which maps
 * the path (CtrShim_MapPath) and calls the real function. Paths without a
 * device prefix are passed through untouched: the rest of the app uses
 * absolute Android paths.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <3ds/result.h>
#include <3ds/romfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ctr_host.h"
#include "shim_internal.h"

FILE *__real_fopen(const char *path, const char *mode);
FILE *__real_fopen64(const char *path, const char *mode);
FILE *__real_freopen(const char *path, const char *mode, FILE *stream);
DIR *__real_opendir(const char *path);
int __real_stat(const char *path, struct stat *st);
int __real_lstat(const char *path, struct stat *st);
int __real_stat64(const char *path, struct stat64 *st);
int __real_lstat64(const char *path, struct stat64 *st);
int __real_access(const char *path, int mode);
int __real_mkdir(const char *path, mode_t mode);
int __real_rmdir(const char *path);
int __real_remove(const char *path);
int __real_unlink(const char *path);
int __real_rename(const char *from, const char *to);
int __real_truncate(const char *path, off_t length);
int __real_truncate64(const char *path, off64_t length);
int __real_open(const char *path, int flags, ...);
int __real_open64(const char *path, int flags, ...);
int __real___open_2(const char *path, int flags);
int __real_chdir(const char *path);

static volatile bool sRomfsMounted;
static pthread_once_t sSdmcOnce = PTHREAD_ONCE_INIT;

/* The SD card root always exists on a console: make the directory once. */
static void MakeSdmcRoot(void)
{
    char path[PATH_MAX];
    const char *root = CtrHost_SdmcDir();
    size_t length;

    if (root == NULL || *root == '\0')
        return;
    length = (size_t)snprintf(path, sizeof(path), "%s", root);
    if (length >= sizeof(path))
        return;
    for (size_t i = 1; i <= length; ++i)
    {
        if (path[i] == '/' || path[i] == '\0')
        {
            char saved = path[i];

            path[i] = '\0';
            if (__real_mkdir(path, 0777) != 0 && errno != EEXIST)
            {
                ShimLogf(ANDROID_LOG_ERROR, "sdmc: cannot create %s (errno %d)", path, errno);
                return;
            }
            path[i] = saved;
        }
    }
}

const char *CtrShim_MapPath(const char *path, char *buffer, size_t size)
{
    const char *root, *rest;
    int length;

    if (path == NULL)
    {
        errno = EFAULT;
        return NULL;
    }
    if (strncmp(path, "romfs:", 6) == 0)
    {
        if (!sRomfsMounted)
        {
            errno = ENODEV;
            return NULL;
        }
        root = CtrHost_RomfsDir();
        rest = path + 6;
    }
    else if (strncmp(path, "sdmc:", 5) == 0)
    {
        pthread_once(&sSdmcOnce, MakeSdmcRoot);
        root = CtrHost_SdmcDir();
        rest = path + 5;
    }
    else
        return path;
    /* A device path is relative to the device's root ("sdmc:x" too). */
    while (*rest == '/')
        ++rest;
    length = *rest ? snprintf(buffer, size, "%s/%s", root, rest) : snprintf(buffer, size, "%s", root);
    if (length < 0 || (size_t)length >= size)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    return buffer;
}

Result romfsMountSelf(const char *name)
{
    const char *dir = CtrHost_RomfsDir();
    struct stat st;

    if (name == NULL || strcmp(name, "romfs") != 0)
        return MAKERESULT(RL_PERMANENT, RS_NOTSUPPORTED, RM_APPLICATION, RD_NOT_IMPLEMENTED);
    if (dir == NULL || *dir == '\0' || __real_stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        ShimLogf(ANDROID_LOG_ERROR, "romfsInit: no RomFS directory at '%s'", dir ? dir : "");
        return MAKERESULT(RL_PERMANENT, RS_NOTFOUND, RM_APPLICATION, RD_NOT_FOUND);
    }
    sRomfsMounted = true;
    return 0;
}

Result romfsUnmount(const char *name)
{
    if (name == NULL || strcmp(name, "romfs") != 0 || !sRomfsMounted)
        return MAKERESULT(RL_PERMANENT, RS_NOTFOUND, RM_APPLICATION, RD_NOT_FOUND);
    sRomfsMounted = false;
    return 0;
}

#define MAP(path, buffer)                                                  \
    char buffer[PATH_MAX];                                                 \
    const char *mapped_##buffer = CtrShim_MapPath(path, buffer, PATH_MAX)

FILE *__wrap_fopen(const char *path, const char *mode)
{
    MAP(path, p);
    return mapped_p ? __real_fopen(mapped_p, mode) : NULL;
}

FILE *__wrap_fopen64(const char *path, const char *mode)
{
    MAP(path, p);
    return mapped_p ? __real_fopen64(mapped_p, mode) : NULL;
}

FILE *__wrap_freopen(const char *path, const char *mode, FILE *stream)
{
    const char *mapped = NULL;
    char p[PATH_MAX];

    /* A NULL path reopens the same file with another mode. */
    if (path != NULL && (mapped = CtrShim_MapPath(path, p, sizeof(p))) == NULL)
        return NULL;
    return __real_freopen(mapped, mode, stream);
}

DIR *__wrap_opendir(const char *path)
{
    MAP(path, p);
    return mapped_p ? __real_opendir(mapped_p) : NULL;
}

int __wrap_stat(const char *path, struct stat *st)
{
    MAP(path, p);
    return mapped_p ? __real_stat(mapped_p, st) : -1;
}

int __wrap_lstat(const char *path, struct stat *st)
{
    MAP(path, p);
    return mapped_p ? __real_lstat(mapped_p, st) : -1;
}

int __wrap_stat64(const char *path, struct stat64 *st)
{
    MAP(path, p);
    return mapped_p ? __real_stat64(mapped_p, st) : -1;
}

int __wrap_lstat64(const char *path, struct stat64 *st)
{
    MAP(path, p);
    return mapped_p ? __real_lstat64(mapped_p, st) : -1;
}

int __wrap_access(const char *path, int mode)
{
    MAP(path, p);
    return mapped_p ? __real_access(mapped_p, mode) : -1;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    MAP(path, p);
    return mapped_p ? __real_mkdir(mapped_p, mode) : -1;
}

int __wrap_rmdir(const char *path)
{
    MAP(path, p);
    return mapped_p ? __real_rmdir(mapped_p) : -1;
}

int __wrap_remove(const char *path)
{
    MAP(path, p);
    return mapped_p ? __real_remove(mapped_p) : -1;
}

int __wrap_unlink(const char *path)
{
    MAP(path, p);
    return mapped_p ? __real_unlink(mapped_p) : -1;
}

int __wrap_rename(const char *from, const char *to)
{
    MAP(from, f);
    MAP(to, t);
    return mapped_f && mapped_t ? __real_rename(mapped_f, mapped_t) : -1;
}

int __wrap_truncate(const char *path, off_t length)
{
    MAP(path, p);
    return mapped_p ? __real_truncate(mapped_p, length) : -1;
}

int __wrap_truncate64(const char *path, off64_t length)
{
    MAP(path, p);
    return mapped_p ? __real_truncate64(mapped_p, length) : -1;
}

int __wrap_chdir(const char *path)
{
    MAP(path, p);
    return mapped_p ? __real_chdir(mapped_p) : -1;
}

static mode_t OpenMode(int flags, va_list args)
{
#ifdef O_TMPFILE
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE)
#else
    if (flags & O_CREAT)
#endif
        return (mode_t)va_arg(args, int);
    return 0;
}

int __wrap_open(const char *path, int flags, ...)
{
    va_list args;
    mode_t mode;

    va_start(args, flags);
    mode = OpenMode(flags, args);
    va_end(args);
    MAP(path, p);
    return mapped_p ? __real_open(mapped_p, flags, mode) : -1;
}

int __wrap_open64(const char *path, int flags, ...)
{
    va_list args;
    mode_t mode;

    va_start(args, flags);
    mode = OpenMode(flags, args);
    va_end(args);
    MAP(path, p);
    return mapped_p ? __real_open64(mapped_p, flags, mode) : -1;
}

int __wrap___open_2(const char *path, int flags)
{
    MAP(path, p);
    return mapped_p ? __real___open_2(mapped_p, flags) : -1;
}
