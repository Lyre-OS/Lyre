#ifndef _DEV__STORAGE__PARTITION_H
#define _DEV__STORAGE__PARTITION_H

#include <lib/resource.h>
#include <stdint.h>

void partition_enum(struct resource *root, const char *rootname, uint16_t blocksize, const char *convention);

#endif
