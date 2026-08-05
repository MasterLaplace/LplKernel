#ifndef _STDBOOL_H
#define _STDBOOL_H

/* C++ has `bool`, `true` and `false` as keywords, and defining them as macros
   breaks every header that uses them as identifiers — <type_traits> declares
   `template<typename T, T __v>` over `bool`, so a macro turns the whole standard
   library into syntax errors. The C standard says as much: in C++, <stdbool.h>
   defines nothing but the feature-test macro.

   This mattered the moment a C header meant for both languages appeared. The
   kernel's own headers are included from C++ by libengine and libassistant, and
   without this guard a single `#include <stdbool.h>` three levels down poisons
   the translation unit with several hundred errors that name only libstdc++
   files. */
#ifndef __cplusplus

#    define bool _Bool

#    define true  (_Bool) 1
#    define false (_Bool) 0

#endif /* !__cplusplus */

// see C99 - 7.16 - 7.16 Boolean type and values <stdbool.h>
#define __bool_true_false_are_defined 1
#endif
