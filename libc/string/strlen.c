#include <string.h>

size_t strlen(const char *s)
{
    size_t len = 0u;

    while (s[len])
        len++;
    return len;
}
