#ifndef _STDARG_H
    #define _STDARG_H

typedef char *va_list;

    #define _va_align(type) \
        (((sizeof(type) + sizeof(int) - 1) / sizeof(int)) * sizeof(int))

    #define va_start(ap, last_fixed_arg) \
        ((ap) = (va_list)&(last_fixed_arg) + _va_align(typeof(last_fixed_arg)))

    #define va_arg(ap, type) \
        (*(type *)(((ap) += _va_align(type)) - _va_align(type)))

    #define va_end(ap) ((ap) = (va_list)0)

#endif
