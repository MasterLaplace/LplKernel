#ifndef _CTYPE_H
    #define _CTYPE_H

    #define _bounds(c, lo, hi) (((c) >= (lo)) && (c <= (hi)))
    #define _lower_or(c) ((c) | (1 << 5))

    // https://en.cppreference.com/w/c/string/byte/isprint

    #define iscntrl(c) _bounds(c, '\0', ' ')

    #define isprint(c) _bounds(c, ' ', '~')

    #define isspace(c) (_bounds(c, '\t', '\r') || ((c) == ' '))

    #define isblank(c) ((c) == '\t' || ((c) == ' '))

    #define isgraph(c) (isprint(c) && ((c) != ' '))

    #define ispunc(c) (_bounds(c, '!', '/') \
        || _bounds(c, ':', '@')             \
        || _bounds(c, '[', '`'))

    #define isalpha(c) _bounds(_lower_or(c), 'a', 'z')

    #define isupper(c) _bounds(c, 'A', 'Z')

    #define islower(c) _bounds(c, 'a', 'z')

    #define isdigit(c) _bounds(c, '0', '9')

    #define isxdigit(c) (isdigit(c) || _bounds(_lower_or(c), 'a', 'f'))

#endif
