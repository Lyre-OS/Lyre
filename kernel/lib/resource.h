#ifndef _LIB__RESOURCE_H
#define _LIB__RESOURCE_H

#include <stddef.h>
#include <lib/lock.h>
#include <sys/stat.h>

struct resource {
    size_t actual_size;
    size_t refcount;
    spinlock_t lock;
    struct stat stat;
};

#endif
