#ifndef _LIB__MISC_H
#define _LIB__MISC_H

#include <lib/panic.h>

#define DIV_ROUNDUP(VALUE, DIV) ({ \
    typeof(VALUE) DIV_ROUNDUP_value = VALUE; \
    typeof(DIV) DIV_ROUNDUP_div = DIV; \
    (DIV_ROUNDUP_value + (DIV_ROUNDUP_div - 1)) / DIV_ROUNDUP_div; \
})

#define ALIGN_UP(VALUE, ALIGN) ({ \
    typeof(VALUE) ALIGN_UP_value = VALUE; \
    typeof(ALIGN) ALIGN_UP_align = ALIGN; \
    DIV_ROUNDUP(ALIGN_UP_value, ALIGN_UP_align) * ALIGN_UP_align; \
})

#define ALIGN_DOWN(VALUE, ALIGN) ({ \
    typeof(VALUE) ALIGN_DOWN_value = VALUE; \
    typeof(ALIGN) ALIGN_DOWN_align = ALIGN; \
    (ALIGN_DOWN_value / ALIGN_DOWN_align) * ALIGN_DOWN_align; \
})

#define SIZEOF_ARRAY(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))

#define ASSERT(COND) do { \
    if (!(COND)) { \
        panic(NULL, "Assertion failed: " ##COND); \
    } \
} while (0)

#define ASSERT_MSG(COND, ...) do { \
    if (!(COND)) { \
        panic(NULL, __VA_ARGS__); \
    } \
} while (0)

typedef char symbol[];

#endif
