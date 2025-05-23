#ifndef _CTYPE_H
    #define _CTYPE_H

    #define _bounds(c, lo, hi) (((c) >= (lo)) && (c <= (hi)))
    #define _lower(c) ((c) | (1 << 5))
    #define _upper(c) ((c) & ~(1 << 5))

// Character classification

    // https://en.cppreference.com/w/c/string/byte/isprint

    #define iscntrl(c) _bounds(c, '\0', ' ')

    #define isprint(c) _bounds(c, ' ', '~')

    #define isspace(c) (_bounds(c, '\t', '\r') || ((c) == ' ') || ((c) == 127))

    #define isblank(c) ((c) == '\t' || ((c) == ' '))

    #define isgraph(c) (isprint(c) && ((c) != ' '))

    #define ispunc(c) (_bounds(c, '!', '/') \
        || _bounds(c, ':', '@')             \
        || _bounds(c, '[', '`'))

    #define isalpha(c) _bounds(_lower(c), 'a', 'z')

    #define isupper(c) _bounds(c, 'A', 'Z')

    #define islower(c) _bounds(c, 'a', 'z')

    #define isdigit(c) _bounds(c, '0', '9')

    #define isxdigit(c) (isdigit(c) || _bounds(_lower(c), 'a', 'f'))

// Character case mapping

    #define tolower(c) (isupper(c) ? (_lower(c)) : (c))
    #define toupper(c) (islower(c) ? (_upper(c)) : (c))

#endif
