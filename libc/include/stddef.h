#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL (void *) (0UL)

/* Defer the fundamental types to the compiler so they match exactly what the
 * toolchain (and libstdc++'s <cstddef>) expects. On i686-elf gcc, size_t is
 * `long unsigned int` (4 bytes) — NOT `unsigned int`; hardcoding the latter
 * created a type clash the moment any C++ TU pulled in both this header and
 * the compiler's <cstddef> (e.g. via the engine module / kstd). */
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;

#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

/* <cstddef> exposes std::max_align_t via `using ::max_align_t;`, so the C header
 * must define it. Use the compiler's notion of the most-aligned type. */
typedef struct {
    long long __max_align_ll;
    long double __max_align_ld;
} max_align_t;

#ifdef __GNUC__
#    define offsetof(type, member) __builtin_offsetof(type, member)
#else
#    define offsetof(type, member) ((size_t) & (((type *) 0)->member))
#endif
#endif
