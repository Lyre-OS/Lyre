#include <bits/posix/stat.h>
#include <fs/ext2fs.h>
#include <fs/vfs/vfs.h>
#include <lib/bitmap.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <time/time.h>

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
    uint32_t addrblockbmp;
    uint32_t addrinodebmp;
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

static ssize_t ext2fs_inoderead(struct ext2fs_inode *inode, struct ext2fs *fs, void *buf, off_t off, size_t count);
static ssize_t ext2fs_inodereadentry(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx);
static uint32_t ext2fs_inodegetblock(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t iblock);
static ssize_t ext2fs_bgdreadentry(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t idx);

static uint32_t ext2fs_inodegetblock(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t iblock) {
    uint32_t blockidx = 0;
    uint32_t blocklvl = fs->blksize / 4;

    if (iblock < 12) {
        blockidx = inode->blocks[iblock];
        return blockidx;
    }

    iblock -= 12;

    if (iblock >= blocklvl) {
        iblock -= blocklvl;

        uint32_t singleidx = iblock / blocklvl;
        off_t indirectoff = iblock % blocklvl;
        uint32_t indirectblock = 0;

        if (singleidx >= blocklvl) {
            iblock -= blocklvl * blocklvl; // square

            uint32_t doubleindirect = iblock / blocklvl;
            indirectoff = iblock % blocklvl;
            uint32_t singleindirectidx = 0;
            fs->backing->resource->read(fs->backing->resource, NULL, &singleindirectidx, inode->blocks[14] * fs->blksize + doubleindirect * 4, sizeof(uint32_t));
            fs->backing->resource->read(fs->backing->resource, NULL, &indirectblock, doubleindirect * fs->blksize + singleindirectidx * 4, sizeof(uint32_t));
            fs->backing->resource->read(fs->backing->resource, NULL, &blockidx, indirectblock * fs->blksize + indirectoff * 4, sizeof(uint32_t));

            return blockidx;
        }

        fs->backing->resource->read(fs->backing->resource, NULL, &indirectblock, inode->blocks[13] * fs->blksize + singleidx * 4, sizeof(uint32_t));
        fs->backing->resource->read(fs->backing->resource, NULL, &blockidx, indirectblock * fs->blksize + indirectoff * 4, sizeof(uint32_t));

        return blockidx;
    }

    fs->backing->resource->read(fs->backing->resource, NULL, &blockidx, inode->blocks[12] * fs->blksize + iblock * 4, sizeof(uint32_t));

    return blockidx;
}

static ssize_t ext2fs_inoderead(struct ext2fs_inode *inode, struct ext2fs *fs, void *buf, off_t off, size_t count) {
    if (off > inode->sizelo) return 0;

    if ((off + count) > inode->sizelo) count = inode->sizelo - off;

    for (size_t head = 0; head < count;) {
        size_t iblock = (off + head) / fs->blksize;

        size_t size = count - head;
        off = (off + head) % fs->blksize;

        if (size > (fs->blksize - off)) {
            size = fs->blksize - off;
        }

        uint32_t block = ext2fs_inodegetblock(inode, fs, (uint32_t)iblock);
        if (fs->backing->resource->read(fs->backing->resource, NULL, (void *)((uint64_t)buf + head), block * fs->blksize + off, size) == -1) {
            return -1;
        }

        head += size;
    }

    return count;
}

static ssize_t ext2fs_bgdreadentry(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t idx) {
    off_t off = 0;

    if (fs->blksize >= 2048) {
        off = fs->blksize;
    } else {
        off = fs->blksize * 2;
    }

    ASSERT_MSG(fs->backing->resource->read(fs->backing->resource, NULL, bgd, off + sizeof(struct ext2fs_blockgroupdesc) * idx, sizeof(struct ext2fs_blockgroupdesc)), "ext2fs: unable to read bgd entry");
    return 0;
}

static ssize_t ext2fs_inodereadentry(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx) {
    size_t tableidx = (inodeidx - 1) % fs->sb.inodespergroup;
    size_t bgdidx = (inodeidx - 1) / fs->sb.inodespergroup;

    struct ext2fs_blockgroupdesc bgd = { 0 };
    ext2fs_bgdreadentry(&bgd, fs, bgdidx);

    ASSERT_MSG(fs->backing->resource->read(fs->backing->resource, NULL, inode, bgd.inodetable * fs->blksize + fs->sb.inodesize * tableidx, sizeof(struct ext2fs_inode)), "ext2fs: failed to read inode entry");

    return 1;
}

static ssize_t ext2fs_resread(struct resource *_this, struct f_description *description, void *buf, off_t loc, size_t count) {
    (void)description;
    struct ext2fs_resource *this = (struct ext2fs_resource *)_this;

    struct ext2fs_inode curinode = { 0 };

    ext2fs_inodereadentry(&curinode, this->fs, this->stat.st_ino);

    if ((off_t)(loc + count) >= this->stat.st_size) {
        count = count - ((loc + count) - this->stat.st_size);
    }

    return ext2fs_inoderead(&curinode, this->fs, buf, loc, count);
}

static ssize_t ext2fs_reswrite(struct resource *_this, struct f_description *description, const void *buf, off_t loc, size_t count) {
    (void)_this;
    (void)description;
    (void)buf;
    (void)loc;
    (void)count;
    return 0; 
}

static struct vfs_node *ext2fs_mount(struct vfs_node *parent, const char *name, struct vfs_node *source);

static struct vfs_node *ext2fs_create(struct vfs_filesystem *_this, struct vfs_node *parent, const char *name, int mode) {
    struct vfs_node *node = NULL;
    struct ext2fs_resource *resource = NULL;

    node = vfs_create_node(_this, parent, name, S_ISDIR(mode));
    if (node == NULL) {
        goto fail;
    }
    
    resource = resource_create(sizeof(struct ext2fs_resource));
    if (resource == NULL) {
        goto fail;
    }

    node->resource = (struct resource *)resource;
    return node;
fail:
    if (node != NULL) {
        free(node); // TODO: Use vfs_destroy_node
    }
    if (resource != NULL) {
        free(resource);
    }

    return NULL;
}

static void ext2fs_populate(struct vfs_filesystem *_this, struct vfs_node *node) {
    struct ext2fs *fs = (struct ext2fs *)_this;
    struct ext2fs_inode parent = { 0 };
    ext2fs_inodereadentry(&parent, fs, node->resource->stat.st_ino);

    void *buf = alloc(parent.sizelo);
    ext2fs_inoderead(&parent, fs, buf, 0, parent.sizelo);

    for (size_t i = 0; i < parent.sizelo;) {
        struct ext2fs_direntry *direntry = (struct ext2fs_direntry *)((uint64_t)buf + i);

        char *namebuf = alloc(direntry->namelen + 1);
        memcpy(namebuf, (void *)((uint64_t)direntry + sizeof(struct ext2fs_direntry)), direntry->namelen);

        if (direntry->inodeidx == 0) {
            free(namebuf);
            goto cleanup;
        }

        if (!strcmp(namebuf, ".") || !strcmp(namebuf, "..")) {
            i += direntry->entsize; // vfs already handles creating these
            continue;
        } 

        struct ext2fs_inode inode = { 0 };
        ext2fs_inodereadentry(&inode, fs, direntry->inodeidx);

        uint16_t mode = (inode.perms & 0xFFF) | 
            (direntry->dirtype == 1 ? S_IFREG : 
             direntry->dirtype == 2 ? S_IFDIR : 
             direntry->dirtype == 3 ? S_IFCHR : 
             direntry->dirtype == 4 ? S_IFBLK : 
             direntry->dirtype == 5 ? S_IFIFO : 
             direntry->dirtype == 6 ? S_IFSOCK : 
             S_IFLNK);
 
        struct vfs_node *fnode = vfs_create_node((struct vfs_filesystem *)fs, node, namebuf, S_ISDIR(mode));
        struct ext2fs_resource *fres = resource_create(sizeof(struct ext2fs_resource));
        fres->read = ext2fs_resread;
        fres->write = ext2fs_reswrite;
        fres->stat.st_uid = inode.uid;
        fres->stat.st_gid = inode.gid;
        fres->stat.st_mode = mode;
        fres->stat.st_ino = direntry->inodeidx;
        fres->stat.st_size = inode.sizelo | ((uint64_t)inode.sizehi >> 32);
        fres->stat.st_nlink = inode.hardlinkcnt;
        fres->stat.st_blksize = fs->blksize;
        fres->stat.st_blocks = fres->stat.st_size / fs->blksize;

        fres->stat.st_atim = (struct timespec) { .tv_sec = inode.accesstime, .tv_nsec = 0 };
        fres->stat.st_ctim = (struct timespec) { .tv_sec = inode.creationtime, .tv_nsec = 0 };
        fres->stat.st_mtim = (struct timespec) { .tv_sec = inode.modifiedtime, .tv_nsec = 0 };

        fres->fs = fs;

        fnode->resource = (struct resource *)fres;
        fnode->populated = false;

        HASHMAP_SINSERT(&fnode->parent->children, namebuf, fnode);

        if (S_ISDIR(mode)) {
            vfs_create_dotentries(fnode, node); // set up for correct directory structure
            ext2fs_populate((struct vfs_filesystem *)fs, fnode); // recurse filesystem
        }

        i += direntry->entsize;
    }

    node->populated = true; // we already populated this node with all existing files
cleanup:
    free(buf);
    return;
}

static inline struct vfs_filesystem *ext2fs_instantiate(void) {
    struct ext2fs *new_fs = alloc(sizeof(struct ext2fs));
    if (new_fs == NULL) {
        return NULL;
    }

    new_fs->mount = ext2fs_mount;
    new_fs->create = ext2fs_create;
    new_fs->populate = ext2fs_populate;
    // new_fs->symlink = ext2fs_symlink;
    // new_fs->link = ext2fs_link;

    return (struct vfs_filesystem *)new_fs;
}

static struct vfs_node *ext2fs_mount(struct vfs_node *parent, const char *name, struct vfs_node *source) {

    struct ext2fs *new_fs = (struct ext2fs *)ext2fs_instantiate();

    source->resource->read(source->resource, NULL, &new_fs->sb, source->resource->stat.st_blksize * 2, sizeof(struct ext2fs_superblock));

    if (new_fs->sb.sig != 0xef53) {
        panic(NULL, "Told to mount an ext2 filesystem whilst source is not ext2!");
    }

    new_fs->backing = source;
    new_fs->blksize = 1024 << new_fs->sb.blksize;
    new_fs->fragsize = 1024 << new_fs->sb.fragsize;
    new_fs->bgdcnt = new_fs->sb.blockcnt / new_fs->sb.blockspergroup;

    ASSERT_MSG(ext2fs_inodereadentry(&new_fs->root, new_fs, 2), "ext2fs: unable to read root inode");

    struct vfs_node *node = vfs_create_node((struct vfs_filesystem *)new_fs, parent, name, true);
    if (node == NULL) {
        return NULL;
    }
    struct ext2fs_resource *resource = resource_create(sizeof(struct ext2fs_resource));
    if (resource == NULL) {
        return NULL;
    }

    resource->stat.st_size = new_fs->root.sizelo | ((uint64_t)new_fs->root.sizehi >> 32);
    resource->stat.st_blksize = new_fs->blksize;
    resource->stat.st_blocks = resource->stat.st_size / resource->stat.st_blksize;
    resource->stat.st_dev = source->resource->stat.st_rdev; // assign to device id of source device
    resource->stat.st_mode = 0644 | S_IFDIR;
    resource->stat.st_nlink = new_fs->root.hardlinkcnt;
    resource->stat.st_ino = 2; // root inode

    resource->stat.st_atim = time_realtime;
    resource->stat.st_ctim = time_realtime;
    resource->stat.st_mtim = time_realtime;

    resource->fs = new_fs;

    node->resource = (struct resource *)resource;

    ext2fs_populate((struct vfs_filesystem *)new_fs, node); // recursively fill vfs with filesystem

    return node; // root node (will become child of parent)
}

void ext2fs_init(void) {
    struct vfs_filesystem *new_fs = ext2fs_instantiate();
    if (new_fs == NULL) {
        panic(NULL, "Failed to instantiate ext2fs");
    } 

    vfs_add_filesystem(new_fs, "ext2fs");
}
