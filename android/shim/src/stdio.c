/*
 * newlib's stdout/stderr devices on bionic (see sys/iosupport.h).
 *
 * At load time stdout and stderr are replaced with fopencookie streams whose
 * writes go to devoptab_list[STD_OUT] and devoptab_list[STD_ERR], read at
 * every write. So every stdio path (printf, puts, fwrite(stdout), perror,
 * fprintf(stderr), the compiler's printf-to-puts rewrite) reaches whatever
 * device 3DS code installed - 3ds_log.c's log device, or the console after
 * consoleInit - exactly as with newlib. Until something is installed, both
 * go to logcat a line at a time (newlib would discard them).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctrshim_newlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>

#include "shim_internal.h"
#include "stdio_internal.h"

static ssize_t LogWrite(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r;
    (void)fd;
    ShimLogText(ptr, len);
    return (ssize_t)len;
}

static const devoptab_t sDefaultDevice = {.name = "logcat", .write_r = LogWrite};

const devoptab_t *devoptab_list[STD_MAX] = {
    [STD_OUT] = &sDefaultDevice,
    [STD_ERR] = &sDefaultDevice,
};

/*
 * Console text to logcat: lines are collected without escape sequences and
 * written on newline. One collector for all writers; the lock is only held
 * while a chunk is scanned.
 */
static pthread_mutex_t sLineLock = PTHREAD_MUTEX_INITIALIZER;
static char sLine[256];
static size_t sLength;
static int sEscape; /* 0 text, 1 after ESC, 2 inside CSI */

static void FlushLine(void)
{
    if (sLength == 0)
        return;
    sLine[sLength] = '\0';
    sLength = 0;
    __android_log_write(ANDROID_LOG_INFO, SHIM_LOG_TAG, sLine);
}

void ShimLogText(const char *text, size_t length)
{
    pthread_mutex_lock(&sLineLock);
    for (size_t i = 0; i < length; ++i)
    {
        char c = text[i];

        if (sEscape == 1)
        {
            sEscape = c == '[' ? 2 : 0;
            continue;
        }
        if (sEscape == 2)
        {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                sEscape = 0;
            continue;
        }
        if (c == 0x1b)
            sEscape = 1;
        else if (c == '\n' || c == '\r')
            FlushLine();
        else if (c != '\0')
        {
            sLine[sLength++] = c;
            if (sLength == sizeof(sLine) - 1)
                FlushLine();
        }
    }
    pthread_mutex_unlock(&sLineLock);
}

static ssize_t CookieWrite(void *cookie, const char *buffer, size_t size)
{
    const devoptab_t *device = devoptab_list[(int)(intptr_t)cookie];
    ssize_t written;

    if (device == NULL || device->write_r == NULL)
        return (ssize_t)size;
    written = device->write_r(NULL, NULL, buffer, size);
    /* stdio treats a short or failed write as an error on the stream. */
    return written > 0 ? written : (ssize_t)size;
}

static FILE *OpenDevice(int fd, int mode)
{
    cookie_io_functions_t io = {.read = NULL, .write = CookieWrite, .seek = NULL, .close = NULL};
    FILE *file = fopencookie((void *)(intptr_t)fd, "w", io);

    if (file != NULL)
        setvbuf(file, NULL, mode, 0);
    return file;
}

__attribute__((constructor)) static void InstallStdio(void)
{
    FILE *out = OpenDevice(STD_OUT, _IOLBF);
    FILE *err = OpenDevice(STD_ERR, _IONBF);

    if (out != NULL)
    {
        fflush(stdout);
        stdout = out;
    }
    if (err != NULL)
    {
        fflush(stderr);
        stderr = err;
    }
}
