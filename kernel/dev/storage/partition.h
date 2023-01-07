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

#define GPT_IMPORTANT 1
#define GPT_DONTMOUNT 2
#define GPT_LEGACY 4

struct gpt_header {
    char sig[8];
    uint32_t rev;
    uint32_t len;
    uint32_t crc32;
    uint32_t unused;

    uint64_t lba;
    uint64_t altlba;
    uint64_t first;
    uint64_t last;

    uint64_t guidlow;
    uint64_t guidhi;

    uint64_t partlba;
    uint32_t entries;
    uint32_t entrysize;
    uint32_t crc32arr;
};

struct gpt_entry {
    uint64_t typelow;
    uint64_t typehi;

    uint64_t unilow;
    uint64_t unihi;

    uint64_t start;
    uint64_t end;

    uint64_t attr;

    uint16_t name[36]; // 36 UTF-16 unicode chars
};

struct partition_device {
    struct resource;
    uint64_t start;
    uint64_t sectors;
    uint16_t blksize;
    struct resource *root; // root block device
};

void partition_enum(struct resource *root, const char *rootname, uint16_t blocksize, const char *convention);

#endif
