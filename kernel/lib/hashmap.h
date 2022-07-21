#ifndef _LIB__HASHMAP_H
#define _LIB__HASHMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/alloc.h>
#include <lib/libc.h>

// sdbm from: http://www.cse.yorku.ca/~oz/hash.html
static inline uint32_t hash(const void *data, size_t length) {
    const uint8_t *data_u8 = data;
    uint32_t hash = 0;

    for (size_t i = 0; i < length; i++) {
        uint32_t c = data_u8[i];
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

#define HASHMAP_KEY_DATA_MAX 256

#define HASHMAP_INIT(CAP) { .cap = (CAP), .buckets = NULL }

#define HASHMAP_TYPE(TYPE) \
    struct { \
        size_t cap; \
        struct { \
            size_t cap; \
            size_t filled; \
            struct { \
                uint8_t key_data[HASHMAP_KEY_DATA_MAX]; \
                size_t key_length; \
                TYPE item; \
            } *items; \
        } *buckets; \
    }

#define HASHMAP_GET(HASHMAP, RET, KEY_DATA, KEY_LENGTH) ({ \
    bool HASHMAP_GET_ok = false; \
    \
    typeof(KEY_DATA) HASHMAP_GET_key_data = KEY_DATA; \
    typeof(KEY_LENGTH) HASHMAP_GET_key_length = KEY_LENGTH; \
    \
    typeof(HASHMAP) HASHMAP_GET_hashmap = HASHMAP; \
    \
    size_t HASHMAP_GET_hash = hash(HASHMAP_GET_key_data, HASHMAP_GET_key_length); \
    size_t HASHMAP_GET_index = HASHMAP_GET_hash % HASHMAP_GET_hashmap->cap; \
    \
    typeof(HASHMAP_GET_hashmap->buckets) HASHMAP_GET_bucket = \
        &HASHMAP_GET_hashmap->buckets[HASHMAP_GET_index]; \
    \
    if (HASHMAP_GET_bucket->filled == 1) { \
        RET = HASHMAP_GET_bucket->items[0].item; \
        HASHMAP_GET_ok = true; \
    } else { \
        for (size_t HASHMAP_GET_i = 0; HASHMAP_GET_i < HASHMAP_GET_bucket->filled; HASHMAP_GET_i++) { \
            if (HASHMAP_GET_key_length != HASHMAP_GET_bucket->items[HASHMAP_GET_i].key_length) { \
                continue; \
            } \
            if (memcmp(HASHMAP_GET_key_data, \
                       HASHMAP_GET_bucket->items[HASHMAP_GET_i].key_data, \
                       HASHMAP_GET_key_length) == 0) { \
                RET = HASHMAP_GET_bucket->items[HASHMAP_GET_i].item; \
                HASHMAP_GET_ok = true; \
                break; \
            } \
        } \
    } \
    \
    HASHMAP_GET_ok; \
})

#define HASHMAP_SGET(HASHMAP, RET, STRING) ({ \
    const char *HASHMAP_SGET_string = (STRING); \
    HASHMAP_GET(HASHMAP, RET, HASHMAP_SGET_string, strlen(HASHMAP_SGET_string)); \
})

#define HASHMAP_INSERT(HASHMAP, KEY_DATA, KEY_LENGTH, ITEM) do { \
    typeof(KEY_DATA) HASHMAP_INSERT_key_data = KEY_DATA; \
    typeof(KEY_LENGTH) HASHMAP_INSERT_key_length = KEY_LENGTH; \
    \
    typeof(HASHMAP) HASHMAP_INSERT_hashmap = HASHMAP; \
    if (HASHMAP_INSERT_hashmap->buckets == NULL) { \
        HASHMAP_INSERT_hashmap->buckets = \
            alloc(HASHMAP_INSERT_hashmap->cap * sizeof(*HASHMAP_INSERT_hashmap->buckets)); \
    } \
    \
    size_t HASHMAP_INSERT_hash = hash(HASHMAP_INSERT_key_data, HASHMAP_INSERT_key_length); \
    size_t HASHMAP_INSERT_index = HASHMAP_INSERT_hash % HASHMAP_INSERT_hashmap->cap; \
    \
    typeof(HASHMAP_INSERT_hashmap->buckets) HASHMAP_INSERT_bucket = \
        &HASHMAP_INSERT_hashmap->buckets[HASHMAP_INSERT_index]; \
    \
    if (HASHMAP_INSERT_bucket->cap == 0) { \
        HASHMAP_INSERT_bucket->cap = 16; \
        HASHMAP_INSERT_bucket->items = \
            alloc(HASHMAP_INSERT_bucket->cap * sizeof(*HASHMAP_INSERT_bucket->items)); \
    } \
    \
    if (HASHMAP_INSERT_bucket->filled == HASHMAP_INSERT_bucket->cap) { \
        HASHMAP_INSERT_bucket->cap *= 2; \
        HASHMAP_INSERT_bucket->items = \
            realloc(HASHMAP_INSERT_bucket->items, \
                    HASHMAP_INSERT_bucket->cap * sizeof(*HASHMAP_INSERT_bucket->items)); \
    } \
    \
    typeof(HASHMAP_INSERT_bucket->items) HASHMAP_INSERT_item = \
        &HASHMAP_INSERT_bucket->items[HASHMAP_INSERT_bucket->filled]; \
    \
    memcpy(HASHMAP_INSERT_item->key_data, HASHMAP_INSERT_key_data, HASHMAP_INSERT_key_length); \
    HASHMAP_INSERT_item->key_length = HASHMAP_INSERT_key_length; \
    HASHMAP_INSERT_item->item = ITEM; \
    \
    HASHMAP_INSERT_bucket->filled++; \
} while (0)

#define HASHMAP_SINSERT(HASHMAP, STRING, ITEM) do { \
    const char *HASHMAP_SINSERT_string = (STRING); \
    HASHMAP_INSERT(HASHMAP, HASHMAP_SINSERT_string, strlen(HASHMAP_SINSERT_string), ITEM); \
} while (0)

#endif
