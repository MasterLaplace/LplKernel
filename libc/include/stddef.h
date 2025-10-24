#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL (void *) (0L)

typedef int ptrdiff_t;
typedef unsigned int size_t;

#ifndef __cplusplus
typedef unsigned short wchar_t;
#endif

#ifdef __GNUC__
#    define offsetof(type, member) __builtin_offsetof(type, member)
#else
#    define offsetof(type, member) ((size_t) & (((type *) 0)->member))
#endif
#endif
