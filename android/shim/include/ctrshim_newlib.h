/*
 * newlib libc extensions 3DS code may use that bionic does not have. Pulled
 * in by <3ds.h>, the way devkitARM's newlib declares them in <stdio.h>.
 * On other C libraries (host tests on glibc) the libc's own are used.
 */
#pragma once

#include <stdio.h>
#include <sys/types.h>

#ifdef __BIONIC__

#ifdef __cplusplus
extern "C" {
#endif

typedef ssize_t cookie_read_function_t(void *cookie, char *buffer, size_t size);
typedef ssize_t cookie_write_function_t(void *cookie, const char *buffer, size_t size);
typedef int cookie_seek_function_t(void *cookie, off_t *offset, int whence);
typedef int cookie_close_function_t(void *cookie);

typedef struct
{
    cookie_read_function_t *read;
    cookie_write_function_t *write;
    cookie_seek_function_t *seek;
    cookie_close_function_t *close;
} cookie_io_functions_t;

FILE *fopencookie(void *cookie, const char *mode, cookie_io_functions_t functions);

#ifdef __cplusplus
}
#endif

#endif
