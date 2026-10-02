#ifndef STDIO_INTERNAL_H
#define STDIO_INTERNAL_H

#include <stddef.h>

/* Text for logcat, a line at a time, without ANSI escape sequences. */
void ShimLogText(const char *text, size_t length);

#endif
