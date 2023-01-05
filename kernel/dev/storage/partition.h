#ifndef _DEV__STORAGE__PARTITION_H
#define _DEV__STORAGE__PARTITION_H

#include <lib/resource.h>
#include <stdint.h>

struct mbr_entry {
    uint8_t status;
    uint8_t start[3];
    uint8_t type;
    uint8_t end[3];
    uint32_t startsect;
    uint32_t sectors;
};

struct partition_device {
    struct resource;
    uint64_t start;
    uint64_t sectors;
    uint16_t blksize;
    struct resource *root; // root block device
};

void partition_enum(struct resource *root, const char *rootname, uint16_t blocksize);

#endif
