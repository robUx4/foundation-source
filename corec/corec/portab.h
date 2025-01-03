/*****************************************************************************
 *
 * Copyright (c) 2008-2010, CoreCodec, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 ****************************************************************************/

#ifndef __PORTAB_H
#define __PORTAB_H

#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <limits.h>

#undef INLINE
#undef NOINLINE
#undef IS_LITTLE_ENDIAN
#undef IS_BIG_ENDIAN

#if defined(_WIN64)

#define TARGET_WIN64
#define TARGET_WIN
#define TARGET_DESKTOP

#elif defined(_WIN32)

#define TARGET_WIN32
#define TARGET_WIN
#define TARGET_DESKTOP

#elif defined(__APPLE__)

#ifndef TARGET_IPHONE
#include <TargetConditionals.h> // iPhone information is defined within the SDK in which Xcode is pointed to
#endif

#define TARGET_OSX

#ifdef TARGET_OS_IPHONE // defined in TargetConditionals.h
#define TARGET_IPHONE

#if !TARGET_IPHONE_SIMULATOR
#define TARGET_IPHONE_DEVICE
#endif


#if defined(__arm__)
#define ARM
#endif

#endif

#ifndef TARGET_IPHONE
#define TARGET_DESKTOP // only for dekstop (not iPhone or Apple TV)
#endif

#elif defined(ANDROID)

#define TARGET_ANDROID

#if defined(__arm__)
#define ARM
#endif

#elif defined(__linux__) || defined(__CYGWIN__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)

#define TARGET_LINUX

#else
#error Platform not supported by Core-C!
#endif

#if defined(__powerpc__) || defined(__PPC__) || defined(__POWERPC__)
#define POWERPC
#define IS_BIG_ENDIAN
#endif

#if defined(__sparc)
#if defined(_LP64)
#define SPARC64
#define CPU_64BITS
#else
#define SPARC32
#endif
#define IS_BIG_ENDIAN
#endif

#if defined(TARGET_LINUX) || defined(TARGET_ANDROID)

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/endian.h>
#else
#include <endian.h>
#endif
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define IS_LITTLE_ENDIAN
#elif __BYTE_ORDER == __BIG_ENDIAN
#define IS_BIG_ENDIAN
#endif
#endif

#if defined(_M_X64) || defined(__amd64__)
#define IX86_64
#define CPU_64BITS
#endif

#if defined(_M_IA64)
#define IA64
#define CPU_64BITS
#endif

#if defined(__mips) && !defined(MIPS)
#define MIPS
#endif

#ifdef MIPS
#if defined(__mips64) || defined(CONFIG_MIPS64)
#define MIPS64
#else
#define MIPS32
#endif
#endif

#if defined(_M_IX86) || defined(__i386) || defined(__i686)
#define IX86
#endif

#if !defined(IS_LITTLE_ENDIAN) && !defined(IS_BIG_ENDIAN)
#define IS_LITTLE_ENDIAN
#endif

#define TICKSPERSEC			16384
#define TIMEPERSEC          (TICKSPERSEC>>4)

#ifndef M_PI
#define M_PI    3.14159265358979323846264338327950288
#endif

#ifdef _MSC_VER

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <malloc.h>
#include <crtdbg.h>

#ifndef strncasecmp
#define strncasecmp(x,y,z) _strnicmp(x,y,z)
#endif
#ifndef strcasecmp
#define strcasecmp(x,y)    _stricmp(x,y)
#endif
#ifndef stricmp
#define stricmp(x,y)       _stricmp(x,y)
#endif
#ifndef strnicmp
#define strnicmp(x,y,z)    _strnicmp(x,y,z)
#endif
#ifndef strdup
#define strdup(x)          _strdup(x)
#endif

#ifndef alloca
#define alloca _alloca
#endif

#ifndef inline
#define inline __inline
#endif

#ifndef _UINTPTR_T_DEFINED
typedef unsigned int uintptr_t;
#define _UINTPTR_T_DEFINED
#endif

#ifndef _INTPTR_T_DEFINED
typedef signed int intptr_t;
#define _INTPTR_T_DEFINED
#endif

#if _MSC_VER >= 1400
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif

#define INLINE __forceinline

#ifndef STDCALL
#define STDCALL __stdcall
#endif

#ifndef CDECL
#define CDECL __cdecl
#endif

#if defined(__cplusplus)
#define SORTPP_PASS // to avoid a problem if winnt.h is included after us
#define NOMINMAX // conflict with <algorithm>
#endif

#else /* _MSC_VER */

#include <stdlib.h>

#if __STDC_VERSION__ >= 199901L

#include <inttypes.h>

#elif !defined(__GLIBC__) && !defined(__MINGW32__) && !defined(TARGET_IPHONE) && !defined(TARGET_ANDROID) && !defined(__FreeBSD__)

#include <inttypes.h>

#ifndef PRIdPTR
#error Not compiled with C99 compatibility!
#endif // PRIdPTR


#ifdef SPARC64
  typedef int64_t int_fast32_t;
  typedef uint64_t uint_fast32_t;
  typedef int64_t int_fast16_t;
  typedef uint64_t uint_fast16_t;
#else // !SPARC64
  typedef int32_t int_fast32_t;
  typedef uint32_t uint_fast32_t;
# ifndef __APPLE__
  typedef int32_t int_fast16_t;
  typedef uint32_t uint_fast16_t;
# endif // !__APPLE__
#endif // !SPARC64

typedef int8_t int_fast8_t;
typedef uint8_t uint_fast8_t;
typedef int64_t int_fast64_t;
typedef uint64_t uint_fast64_t;

#endif

#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0) && !defined(always_inline)
#define INLINE __attribute__((always_inline)) inline
#else
#define INLINE inline
#endif

#if __GNUC__ >= 3
  #define NOINLINE __attribute__((noinline))
#else
  #define NOINLINE
#endif

#if (!defined(IX86) && !defined(_M_X64)) || defined(__CW32__)
#define STDCALL
#define CDECL
#else
#ifndef STDCALL
#define STDCALL __attribute__((stdcall))
#endif
#ifndef CDECL
#define CDECL __attribute__((cdecl))
#endif
#endif

#if !defined(COMPILER_GCC) && defined(__GNUC__)
#define COMPILER_GCC
#endif

#endif /* _MSC_VER */

#include "corec_config.h"

#include <stdint.h>

#ifdef _MSC_VER
#define LL(x)   x##i64
#define ULL(x)  x##ui64
#define PRId64  "I64d"
#define PRIu64  "I64u"
#define PRIx64  "I64x"

#ifndef PRIdPTR
# ifdef _WIN64
#define PRIdPTR  PRId64
# else // !_WIN64
#define PRIdPTR  PRId32
# endif // !_WIN64
#endif // PRIdPTR

#define TPRId64  T("I64d")
#define TPRIu64  T("I64u")
#define TPRIx64  T("I64x")
#else
#define LL(x)   x##ll
#define ULL(x)  x##ull
#ifndef PRId64
#define PRId64  "lld"
#endif
#ifndef PRIu64
#define PRIu64  "llu"
#endif
#ifndef PRIx64
#define PRIx64  "llx"
#endif
#define TPRId64  PRId64
#define TPRIu64  PRIu64
#define TPRIx64  PRIx64
#endif

typedef int_fast32_t err_t;
typedef int_fast32_t bool_t;
typedef int_fast32_t tick_t;
typedef uint8_t boolmem_t; /* unsigned so ":1" bitmode should work */
typedef uint32_t fourcc_t;
typedef uint32_t rgbval_t;

typedef struct cc_guid
{
    uint32_t v1;
    uint16_t v2;
    uint16_t v3;
    uint8_t v4[8];
} cc_guid;

typedef struct cc_fraction
{
	int_fast32_t Num;
	int_fast32_t Den;
} cc_fraction;

typedef struct cc_fraction64
{
	int_fast64_t Num;
	int_fast64_t Den;
} cc_fraction64;

typedef struct cc_point
{
	int x;
	int y;

} cc_point;

typedef struct cc_point16
{
	int16_t x;
	int16_t y;

} cc_point16;

typedef struct cc_rect
{
	int x;
	int y;
	int Width;
	int Height;

} cc_rect;

#ifndef SIZEOF_WCHAR
#if defined(TARGET_OSX) || defined(TARGET_LINUX)
#define SIZEOF_WCHAR    4
#else
#define SIZEOF_WCHAR    2
#endif
#endif

#if SIZEOF_WCHAR==2
typedef wchar_t utf16_t;
#else
typedef uint16_t utf16_t;
#endif

#define tcscpy !UNSAFE!
#define tcscat !UNSAFE!
#define stprintf !UNSAFE!
#define vstprintf !UNSAFE!

#if defined(_WIN32)
#define DLLEXPORT __declspec(dllexport) extern
#define DLLIMPORT __declspec(dllimport) extern
#define DLLHIDDEN
#elif defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 340)
#define DLLEXPORT __attribute__ ((visibility("default"))) extern
#define DLLIMPORT __attribute__ ((visibility("default"))) extern
#define DLLHIDDEN __attribute__ ((visibility("hidden")))
#else
#define DLLEXPORT extern
#define DLLIMPORT extern
#define DLLHIDDEN
#endif

#ifdef __cplusplus
#define INTERNAL_C_API extern "C"
#else
#define INTERNAL_C_API extern
#endif


#define _INLINE INLINE
#define _CONST const

/* todo: needs more testing, that nothing broke... */
#if defined(MAX_PATH)
#define MAXPATH MAX_PATH
#elif defined(PATH_MAX)
#define MAXPATH PATH_MAX
#else
#define MAXPATH 256
#endif
#define MAXPROTOCOL 16

#if MAXPATH < 1024
#define MAXPATHFULL 1024
#else
#define MAXPATHFULL MAXPATH
#endif

#define FOURCCBE(a,b,c,d) \
	(((uint8_t)(a) << 24) | ((uint8_t)(b) << 16) | \
	((uint8_t)(c) << 8) | ((uint8_t)(d) << 0))

#define FOURCCLE(a,b,c,d) \
	(((uint8_t)(a) << 0) | ((uint8_t)(b) << 8) | \
	((uint8_t)(c) << 16) | ((uint8_t)(d)<< 24))

#ifdef IS_BIG_ENDIAN
#define FOURCC(a,b,c,d) (fourcc_t)FOURCCBE(a,b,c,d)
#else
#define FOURCC(a,b,c,d) (fourcc_t)FOURCCLE(a,b,c,d)
#endif

#if defined(__CW32__)
void * __alloca(size_t size);
#ifndef alloca
#define alloca(size) __alloca(size)
#endif
#endif

#if defined(__GNUC__)

#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif

#endif /* __GNUC__ */

#if defined(_MSC_VER) && defined(TARGET_WIN)
#define TRY_BEGIN __try {
#define TRY_END   ;} __except (1) {}
#define TRY_END_FUNC(func) ;} __except (1) { func; }
#else
#define TRY_BEGIN {
#define TRY_END   }
#define TRY_END_FUNC(func) }
#endif

#ifndef NDEBUG
# include <assert.h>
#else // NDEBUG
#ifndef assert
#define assert(x)   ((void)0)
#endif
#endif // NDEBUG

#if defined(COMPILER_GCC)
#define UNUSED_PARAM(x) (x) __attribute__ ((unused))
#else
#define UNUSED_PARAM(x) (x)
#endif

#ifdef CONFIG_FILEPOS_64
typedef int_fast64_t filepos_t;
#define MAX_FILEPOS INT_FAST64_MAX
#define MIN_FILEPOS INT_FAST64_MIN
#else
typedef int_fast32_t filepos_t;
#define MAX_FILEPOS INT_FAST32_MAX
#define MIN_FILEPOS INT_FAST32_MIN
#endif

#define INVALID_FILEPOS_T  ((filepos_t)-1)

#if defined(TARGET_WIN)
typedef long systick_t; /* same size as DWORD */
#else
typedef int systick_t;
#endif
#define INVALID_SYSTICK_T  ((systick_t)-1)

#endif
