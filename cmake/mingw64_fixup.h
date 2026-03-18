/*
 * mingw64_fixup.h
 * Force-included before every translation unit when cross-compiling
 * Linux (LP64) → Windows x64 (LLP64) with MinGW-w64.
 *
 * Problem: on LP64 (Linux), 'unsigned long' = 64-bit.
 *          on LLP64 (Windows), 'unsigned long' = 32-bit.
 * GCC's x86_64-w64-mingw32 cross-compiler may inherit the LP64 host's
 * __UINT64_TYPE__ = 'unsigned long', making uint64_t 32-bit on the target.
 * We override all 64-bit type GCC builtins to 'long long' before any
 * system header (stdint.h, ratio, chrono, …) uses them.
 */

/* --- 64-bit GCC builtin type overrides ---------------------------------- */
#ifdef __UINT64_TYPE__
# undef  __UINT64_TYPE__
#endif
#define __UINT64_TYPE__  unsigned long long

#ifdef __INT64_TYPE__
# undef  __INT64_TYPE__
#endif
#define __INT64_TYPE__   long long

#ifdef __UINTMAX_TYPE__
# undef  __UINTMAX_TYPE__
#endif
#define __UINTMAX_TYPE__ unsigned long long

#ifdef __INTMAX_TYPE__
# undef  __INTMAX_TYPE__
#endif
#define __INTMAX_TYPE__  long long

/* --- Pull in wide-char / stdio CRT declarations ------------------------- */
/*
 * stralign.h (via windows.h) calls _wcsicmp without including <wchar.h>.
 * miniaudio.h calls _wfopen without including <stdio.h>.
 * Including them here guarantees they are declared before those headers run.
 */
#include <wchar.h>
#include <string.h>
#include <stdio.h>
