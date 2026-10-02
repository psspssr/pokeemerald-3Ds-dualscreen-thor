/**
 * @file types.h
 * @brief libctru's basic types, for the Android shim. Same names and widths as
 * libctru (zlib licence, devkitPro), so origin's native units compile as is.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define U64_MAX UINT64_MAX

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;

typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef u32 Handle;
typedef s32 Result;
typedef void (*ThreadFunc)(void *);
typedef void (*voidfn)(void);

#define BIT(n) (1U << (n))

#define ALIGN(m) __attribute__((aligned(m)))
#define PACKED __attribute__((packed))

#ifndef LIBCTRU_NO_DEPRECATION
#define DEPRECATED __attribute__((deprecated))
#else
#define DEPRECATED
#endif

#define CTR_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define CTR_ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
