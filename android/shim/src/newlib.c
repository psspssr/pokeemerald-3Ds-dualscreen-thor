/*
 * fopencookie (see ctrshim_newlib.h) on bionic's funopen64. The stream's
 * read/write access follows the mode string, as with newlib; a missing
 * callback makes that operation fail (read: EOF) instead of the stream.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctrshim_newlib.h>

#ifdef __BIONIC__

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    void *cookie;
    cookie_io_functions_t io;
} Cookie;

static int CookieRead(void *data, char *buffer, int size)
{
    Cookie *c = data;
    return c->io.read != NULL ? (int)c->io.read(c->cookie, buffer, (size_t)size) : 0;
}

static int CookieWrite(void *data, const char *buffer, int size)
{
    Cookie *c = data;

    if (c->io.write == NULL)
    {
        errno = EBADF;
        return -1;
    }
    return (int)c->io.write(c->cookie, buffer, (size_t)size);
}

static fpos64_t CookieSeek(void *data, fpos64_t offset, int whence)
{
    Cookie *c = data;
    off_t position = (off_t)offset;

    if (c->io.seek == NULL)
    {
        errno = ESPIPE;
        return -1;
    }
    if ((fpos64_t)position != offset)
    {
        errno = EOVERFLOW;
        return -1;
    }
    if (c->io.seek(c->cookie, &position, whence) != 0)
        return -1;
    return position;
}

static int CookieClose(void *data)
{
    Cookie *c = data;
    int result = c->io.close != NULL ? c->io.close(c->cookie) : 0;

    free(c);
    return result;
}

FILE *fopencookie(void *cookie, const char *mode, cookie_io_functions_t functions)
{
    bool readable, writable;
    Cookie *c;
    FILE *file;

    if (mode == NULL || strchr("rwa", mode[0]) == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    readable = mode[0] == 'r' || strchr(mode, '+') != NULL;
    writable = mode[0] != 'r' || strchr(mode, '+') != NULL;
    c = malloc(sizeof(*c));
    if (c == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    c->cookie = cookie;
    c->io = functions;
    file = funopen64(c, readable ? CookieRead : NULL, writable ? CookieWrite : NULL, CookieSeek, CookieClose);
    if (file == NULL)
        free(c);
    return file;
}

#endif
