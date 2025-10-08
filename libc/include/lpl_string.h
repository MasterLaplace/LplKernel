/**************************************************************************
 * Laplace String Library v1.0.0
 *
 * Laplace String Library is a library that contains a lot of useful
 * functions for C programming. It is made to be easy to use and to be
 * used in any project. It is also made to be easy to add new functions.
 * It is made by MasterLaplace.
 *
 * Laplace String Library is under MIT License.
 * https://opensource.org/licenses/MIT
 * © 2023 MasterLaplace
 * @version 1.0.0
 * @date 2023-10-14
 **************************************************************************/

#ifndef LAPLACE_STRING_H_
    #define LAPLACE_STRING_H_

////////////////////////////////////////////////////////////
// Include the appropriate header based on the platform used
////////////////////////////////////////////////////////////
// #include "laplace_string_config.h"
// #include "laplace_string_version.h"

////////////////////////////////////////////////////////////
// Include necessary headers for the string library
////////////////////////////////////////////////////////////
#ifdef  __cplusplus
    extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief The string structure that contains the string and its length.
 *
 * @param str The string.
 * @param len The length of the string.
 */
typedef struct string_s {
    char *str;
    unsigned len;
} string_t;

//* It creates a string from a char *.
extern string_t *lp_string_create(char *str)
{
    string_t *string = malloc(sizeof(string_t));
    if (string == NULL) {
        printf("[Laplace@String]: Error while allocating memory for string.\n");
        return (NULL);
    }
    if (str == NULL) {
        string->str = NULL;
        string->len = 0;
        return string;
    }
    string->str = str;
    string->len = strlen(str);
    return string;
}

//* It adds a char to the string.
extern void lp_string_char_append(string_t *string, char c)
{
    if (string == NULL) {
        printf("[Laplace@String]: Error while appending char to string.\n");
        return;
    }
    string->str = realloc(string->str, string->len + 2);
    if (string->str == NULL) {
        printf("[Laplace@String]: Error while appending char to string.\n");
        return;
    }
    string->str[string->len] = c;
    string->str[string->len + 1] = '\0';
    string->len++;
}

//* It adds a char to the beginning of the string.
extern void lp_string_char_appstart(string_t *string, char c)
{
    if (string == NULL) {
        printf("[Laplace@String]: Error while appending char to string.\n");
        return;
    }
    string->str = realloc(string->str, string->len + 2);
    if (string->str == NULL) {
        printf("[Laplace@String]: Error while appending char to string.\n");
        return;
    }
    for (unsigned i = string->len; i > 0; i--)
        string->str[i] = string->str[i - 1];
    string->str[0] = c;
    string->str[string->len + 1] = '\0';
    string->len++;
}

//* It adds a string to the string.
extern void lp_string_concat(string_t *string1, const string_t *string2)
{
    if (string1 == NULL || string2 == NULL) {
        printf("[Laplace@String]: Error while concatenating strings.\n");
        return;
    }
    string1->str = realloc(string1->str, string1->len + string2->len + 1);
    if (string1->str == NULL) {
        printf("[Laplace@String]: Error while concatenating strings.\n");
        return;
    }
    strcat(string1->str, string2->str);
    string1->len += string2->len;
}

//* It erases a part of the string.
extern void lp_string_erase(string_t *string, const unsigned start, const unsigned end)
{
    if (string == NULL) {
        printf("[Laplace@String]: Error while erasing string.\n");
        return;
    }
    if (start > string->len || end > string->len) {
        printf("[Laplace@String]: Error while erasing string.\n");
        return;
    }
    if (start > end) {
        printf("[Laplace@String]: Error while erasing string.\n");
        return;
    }
    unsigned len = end - start;
    for (unsigned i = start; i < string->len - len; i++)
        string->str[i] = string->str[i + len];
    string->str = realloc(string->str, string->len - len + 1);
    if (string->str == NULL) {
        printf("[Laplace@String]: Error while erasing string.\n");
        return;
    } //
    string->str[string->len - len] = '\0';
    string->len -= len;
}

//* It checks if the string is null.
extern bool lp_string_is_null(string_t *string)
{
    return (string == NULL);
}

//* It returns the size of the string.
extern unsigned lp_string_len(string_t *string)
{
    if (string == NULL) {
        printf("[Laplace@String]: Error while getting string length.\n");
        return (0);
    }
    return (string->len);
}

//* It prints the string.
extern void lp_string_print(string_t *list)
{
    if (list == NULL) {
        printf("[Laplace@String]: Error while printing string.\n");
        return;
    }
    #if defined(LAPLACE_STRING_SYSTEM_WINDOWS)
        printf("%s\r\n", list->str);
    #else
        printf("%s\n", list->str);
    #endif
}

#ifdef  __cplusplus
    }   // extern "C"
#endif

#endif/* !LAPLACE_STRING_H_ */
