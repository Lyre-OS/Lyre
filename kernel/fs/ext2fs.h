#ifndef _FS__EXT2FS_H
#define _FS__EXT2FS_H

#include <fs/vfs/vfs.h>
#include <lib/resource.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct ext2fs_superblock {
    uint32_t inodecnt;
    uint32_t blockcnt;
    uint32_t sbrsvd;
    uint32_t unallocb;
    uint32_t unalloci;
    uint32_t sb;
    uint32_t blksize;
    uint32_t fragsize;
    uint32_t blockspergroup;
    uint32_t fragspergroup;
    uint32_t inodespergroup;
    uint32_t lastmnt; // unix epoch for last mount
    uint32_t lastwritten; // unix epoch for last write
    uint16_t mountcnt;
    uint16_t mountallowed; // are we allowed to mount this filesystem?
    uint16_t sig;
    uint16_t fsstate;
    uint16_t errorresp;
    uint16_t vermin;
    uint32_t lastfsck; // last time we cleaned the filesystem
    uint32_t forcedfsck;
    uint32_t osid;
    uint32_t vermaj;
    uint16_t uid;
    uint16_t gid;

    uint32_t first;
    uint16_t inodesize;
    uint16_t sbbgd;
    uint32_t optionalfts;
    uint32_t reqfts;
    uint64_t uuid[2]; // filesystem uuid
    uint64_t name[2];
    uint64_t lastmountedpath[8]; // last path we had when mounted
} __attribute__((packed));

struct ext2fs_blockgroupdesc {
    uint32_t addrbitmap;
    uint32_t addrinode;
    uint32_t inodetable;
    uint16_t unallocb;
    uint16_t unalloci;
    uint16_t dircnt;
    uint16_t unused[7];
} __attribute__((packed));

struct ext2fs_inode {
    uint16_t perms;
    uint16_t uid;
    uint32_t sizelo;
    uint32_t accesstime;
    uint32_t creationtime;
    uint32_t modifiedtime;
    uint32_t deletedtime;
    uint16_t gid;
    uint16_t hardlinkcnt;
    uint32_t sectors;
    uint32_t flags;
    uint32_t oss;
    uint32_t blocks[15];
    uint32_t gennum;
    uint32_t eab;
    uint32_t sizehi;
    uint32_t fragaddr;
} __attribute__((packed));

struct ext2fs_direntry {
    uint32_t inodeidx;
    uint16_t entsize;
    uint8_t namelen;
    uint8_t dirtype;
} __attribute__((packed));

struct ext2fs {
    struct vfs_filesystem;

    uint64_t devid;

    struct vfs_node *backing; // block device this filesystem exists on
    struct ext2fs_inode root;
    struct ext2fs_superblock sb;

    uint64_t blksize;
    uint64_t fragsize;
    uint64_t bgdcnt;
};

struct ext2fs_resource {
    struct resource;   

    struct ext2fs *fs;
};

ssize_t ext2fs_inoderead(struct ext2fs_inode *inode, struct ext2fs *fs, void *buf, off_t off, size_t count);
ssize_t ext2fs_inodereadentry(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx);
ssize_t ext2fs_inodewriteentry(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx);
uint32_t ext2fs_inodegetblock(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t iblock);
ssize_t ext2fs_inoderesize(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx, size_t start, size_t count);
ssize_t ext2fs_inodewrite(struct ext2fs_inode *inode, struct ext2fs *fs, const void *buf, uint32_t inodeidx, off_t off, size_t count);
size_t ext2fs_bgdallocblock(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t bgdidx);
ssize_t ext2fs_bgdreadentry(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t idx);
ssize_t ext2fs_bgdwriteentry(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t idx);
void ext2fs_init(void);

#endif
