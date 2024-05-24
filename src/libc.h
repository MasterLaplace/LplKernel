/*
** LAPLACE&ME PROJECT, 2024
** Laplace [LIB_C]
** Author:
** Master Laplace
** File description:
** LIB_C
*/

#ifndef LIB_C_H_
    #define LIB_C_H_

#include "config.h"

size_t libc_strlen(const char *str);

bool libc_strcmp(const char *str, const char *str2);

bool libc_memset(void *dest, const void *src, size_t size);

#endif /* !LIB_C_H_ */


////////////////////////////////////////////////////////////////////////////////
/*                               IMPLEMENTATION                               */
////////////////////////////////////////////////////////////////////////////////
#ifdef LIB_C_IMPLEMENTATION
#ifndef LIB_C_C_ONCE
    #define LIB_C_C_ONCE

size_t libc_strlen(const char *str)
{
    size_t len = 0;
    for (; *str && *str++; ++len);
    return len;
}

bool libc_strcmp(const char *str, const char *str2)
{
    for (; *str && *str2 && *str == *str2; ++str, ++str2);
    return *str == *str;
}

bool libc_memset(void *dest, const void *src, size_t size)
{
    for (; size != 0; ++dest, ++src, --size)
        *(uint8_t*)dest = *(uint8_t*)src;
    return true;
}

#endif /* !LIB_C_C_ONCE */
#endif /* !LIB_C_IMPLEMENTATION */
