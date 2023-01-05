#include <dev/storage/partition.h>
#include <fs/devtmpfs.h>
#include <lib/print.h>

// wrappers for partition offset read/writes
static ssize_t readpart(struct resource *this_, struct f_description *description, void *buf, off_t loc, size_t count) {
    if(loc >= ((struct partition_device *)this_)->sectors * ((struct partition_device *)this_)->blksize) return -1;
    return ((struct partition_device *)this_)->root->read(((struct partition_device *)this_)->root, description, buf, loc + ((struct partition_device *)this_)->start * ((struct partition_device *)this_)->blksize, count);
}

static ssize_t writepart(struct resource *this_, struct f_description *description, const void *buf, off_t loc, size_t count) {
    if(loc >= ((struct partition_device *)this_)->sectors * ((struct partition_device *)this_)->blksize) return -1;
    return ((struct partition_device *)this_)->root->write(((struct partition_device *)this_)->root, description, buf, loc + ((struct partition_device *)this_)->start * ((struct partition_device *)this_)->blksize, count);
}

void partition_enum(struct resource *root, const char *rootname, uint16_t blocksize) {
    // TODO: GPT (as it's a bit more complex than MBR)
    uint16_t hint;
    off_t loc = 444;
    root->read(root, NULL, &hint, loc, 2);
    loc += 2;
    if(hint && hint != 0x5a5a) { goto gpt; }
    struct mbr_entry entries[4];
    root->read(root, NULL, entries, loc, sizeof(struct mbr_entry) * 4);

    for(size_t i = 0; i < 4; i++) {
        if(!entries[i].type) continue;
        kernel_print("partition: mbr: p%d start: %u (+%u)\n", i, entries[i].startsect, entries[i].sectors);

        struct partition_device *part_res = resource_create(sizeof(struct partition_device));
        part_res->root = root;
        part_res->blksize = blocksize;
        part_res->start = entries[i].startsect;
        part_res->sectors = entries[i].sectors;

        part_res->stat.st_blksize = blocksize;
        part_res->stat.st_size = entries[i].sectors * blocksize;
        part_res->stat.st_blocks = entries[i].sectors;
        part_res->stat.st_rdev = resource_create_dev_id();
        part_res->stat.st_mode = 0666 | S_IFCHR;
        part_res->can_mmap = false;
        part_res->write = writepart;
        part_res->read = readpart;
        part_res->ioctl = NULL;
        char partname[64];
        snprint(partname, sizeof(partname) - 1, "%sp%u", rootname, i);
        devtmpfs_add_device((struct resource *)part_res, partname);
    }
gpt: return;
}
