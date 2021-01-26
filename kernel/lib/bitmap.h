#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <lib/asm.h>

#define bit_test(var, offset) ({                      \
    bool __ret;                                       \
    asm volatile (                                    \
        "bt %1, %2;"                                  \
        : "=@ccc" (__ret)                             \
        : "r" ((uint32_t)var), "r" ((uint32_t)offset) \
    );                                                \
    __ret;                                            \
})

static inline bool bitmap_test(void *bitmap, size_t bit) {
    bool ret;
    asm volatile (
        "bt %1, %2"
        : "=@ccc" (ret)
        : "m" (FLAT_PTR(bitmap)), "r" (bit)
        : "memory"
    );
    return ret;
}

static inline bool bitmap_set(void *bitmap, size_t bit) {
    bool ret;
    asm volatile (
        "bts %1, %2"
        : "=@ccc" (ret), "+m" (FLAT_PTR(bitmap))
        : "r" (bit)
        : "memory"
    );
    return ret;
}

static inline bool bitmap_unset(void *bitmap, size_t bit) {
    bool ret;
    asm volatile (
        "btr %1, %2"
        : "=@ccc" (ret), "+m" (FLAT_PTR(bitmap))
        : "r" (bit)
        : "memory"
    );
    return ret;
}
