/**************************************************************************
 * LplKernel v0.0.0
 *
 * LplKernel is a C kernel iso for Laplace-Project. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by the
 * Open Source Initiative. See the Anti-NN License for more details.
 *
 * @file string.h
 * @brief Compile-Time Configuration Parameters for LplKernel.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-05-22
 **************************************************************************/

#ifndef STRING_H_
    #define STRING_H_

#include <sys/cdefs.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int memcmp(const void *, const void *, size_t);
void *memcpy(void  *__restrict, const void *__restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
size_t strlen(const char*);

// char *strcpy(char *, const char *);

// strcat(), strchr()

#ifdef __cplusplus
}
#endif

#endif  /* !STRING_H_ */


////////////////////////////////////////////////////////////////////////////////
/*                               IMPLEMENTATION                               */
////////////////////////////////////////////////////////////////////////////////
#ifdef STRING_IMPLEMENTATION
#ifndef STRING_C_ONCE
    #define STRING_C_ONCE

bool memset(void *dest, const void *src, size_t size)
{
    for (; size != 0; ++dest, ++src, --size)
        *(uint8_t*)dest = *(uint8_t*)src;
    return true;
}

#endif /* !STRING_C_ONCE */
#endif /* !STRING_IMPLEMENTATION */
