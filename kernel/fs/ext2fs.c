#include <bits/posix/stat.h>
#include <fs/ext2fs.h>
#include <fs/vfs/vfs.h>
#include <lib/bitmap.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <time/time.h>

static size_t ext2fs_allocblock(struct ext2fs *fs) {
    struct ext2fs_blockgroupdesc bgd = { 0 };

    for(size_t i = 0; i < fs->bgdcnt; i++) {
        ext2fs_bgdreadentry(&bgd, fs, i);

        size_t blockidx = ext2fs_bgdallocblock(&bgd, fs, i);

        return blockidx + i * fs->sb.blockspergroup;
    }

    return -1; // max
}

static size_t ext2fs_allocinode(struct ext2fs *fs) {
    struct ext2fs_blockgroupdesc bgd = { 0 };

    for(size_t i = 0; i < fs->bgdcnt; i++) {
        ext2fs_bgdreadentry(&bgd, fs, i);

        size_t inodeidx = ext2fs_bgdallocinode(&bgd, fs, i);

        return inodeidx + i * fs->sb.blockspergroup;
    }

    return -1; // max
}

size_t ext2fs_bgdallocblock(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t bgdidx) {
    if(bgd->unallocb == 0) return -1; // max
    
    uint8_t *bitmap = alloc(DIV_ROUNDUP(fs->blksize, 8));

    ASSERT_MSG(fs->backing->resource->read(fs->backing->resource, NULL, bitmap, bgd->addrbitmap * fs->blksize, fs->blksize), "ext2fs: unable to read bitmap (block group descriptor)");

    for(size_t i = 0; i < fs->blksize; i++) {
        if(bitmap_test(bitmap, i) == false) {
            bitmap_set(bitmap, i);

            ASSERT_MSG(fs->backing->resource->write(fs->backing->resource, NULL, bitmap, bgd->addrbitmap * fs->blksize, fs->blksize), "ext2fs: unable to write bitmap (block group descriptor)");
            bgd->unallocb--;
            ext2fs_bgdwriteentry(bgd, fs, bgdidx);

            free(bitmap);

            return i;
        }
    }

    free(bitmap);
    return -1;
}

size_t ext2fs_bgdallocinode(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t bgdidx) {
    if(bgd->unallocb == 0) return -1;

    void *bitmap = alloc(DIV_ROUNDUP(fs->blksize, 8));

    ASSERT_MSG(fs->backing->resource->read(fs->backing->resource, NULL, bitmap, bgd->addrinode * fs->blksize, fs->blksize), "ext2fs: unable to read inode bitmap (block group descriptor)");

    for(size_t i = 0; i < fs->blksize; i++) {
        if(bitmap_test(bitmap, i) == false) {
            bitmap_set(bitmap, i);

            ASSERT_MSG(fs->backing->resource->write(fs->backing->resource, NULL, bitmap, bgd->addrinode * fs->blksize, fs->blksize), "ext2fs: unable to write inode bitmap (block group descriptor)");
            bgd->unalloci--;
            ext2fs_bgdwriteentry(bgd, fs, bgdidx);

            free(bitmap);

            return i;
        }
    }

    free(bitmap);
    return -1;
}


uint32_t ext2fs_inodegetblock(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t iblock) {
    uint32_t blockidx = 0;
    uint32_t blocklvl = fs->blksize / 4;

    if(iblock < 12) {
        blockidx = inode->blocks[iblock];
        return blockidx;
    }

    iblock -= 12;

    if(iblock >= blocklvl) {
        iblock -= blocklvl;

        uint32_t singleidx = iblock / blocklvl;
        off_t indirectoff = iblock % blocklvl;
        uint32_t indirectblock = 0;

        if(singleidx >= blocklvl) {
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

ssize_t ext2fs_inodesetblock(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx, uint32_t iblock, uint32_t block) {
    uint32_t blocklvl = fs->blksize / 4;

    if(iblock < 12) {
        inode->blocks[iblock] = block;
        return block;
    }

    iblock -= 12;

    if(iblock >= blocklvl) {
        iblock -= blocklvl;

        uint32_t singleidx = iblock / blocklvl;
        off_t indirectoff = iblock % blocklvl;
        uint32_t indirectblock = 0;

        if(singleidx >= blocklvl) {
            iblock -= blocklvl * blocklvl; // square

            uint32_t doubleindirect = iblock / blocklvl;
            indirectoff = iblock % blocklvl;
            uint32_t singleindirectidx = 0;

            if(inode->blocks[14] == 0) {
                inode->blocks[14] = ext2fs_allocblock(fs);
                ext2fs_inodewriteentry(inode, fs, inodeidx);
            }

            fs->backing->resource->read(fs->backing->resource, NULL, &singleindirectidx, inode->blocks[14] * fs->blksize + doubleindirect * 4, sizeof(uint32_t));

            if(singleindirectidx == 0) {
                size_t newblock = ext2fs_allocblock(fs);

                fs->backing->resource->write(fs->backing->resource, NULL, &newblock, inode->blocks[14] * fs->blksize + doubleindirect * 4, sizeof(uint32_t));

                singleindirectidx = newblock;
            }

            fs->backing->resource->read(fs->backing->resource, NULL, &indirectblock, doubleindirect * fs->blksize + singleindirectidx * 4, sizeof(uint32_t));

            if(indirectblock == 0) {
                size_t newblock = ext2fs_allocblock(fs);

                fs->backing->resource->write(fs->backing->resource, NULL, &indirectblock, doubleindirect * fs->blksize + singleindirectidx * 4, sizeof(uint32_t));

                indirectblock = newblock;
            }

            fs->backing->resource->write(fs->backing->resource, NULL, &block, indirectblock * fs->blksize + indirectoff * 4, sizeof(uint32_t));

            return block;    
        }

        if(inode->blocks[13] == 0) {
            inode->blocks[13] = ext2fs_allocblock(fs);

            ext2fs_inodewriteentry(inode, fs, inodeidx);
        }

        fs->backing->resource->read(fs->backing->resource, NULL, &indirectblock, inode->blocks[13] * fs->blksize + singleidx * 4, sizeof(uint32_t));

        if(indirectblock == 0) {
            size_t newblock = ext2fs_allocblock(fs);

            fs->backing->resource->write(fs->backing->resource, NULL, &newblock, inode->blocks[13] * fs->blksize + singleidx * 4, sizeof(uint32_t));

            indirectblock = newblock;
        }

        fs->backing->resource->write(fs->backing->resource, NULL, &block, indirectblock * fs->blksize + indirectoff * 4, sizeof(uint32_t));

        return block;
    } else {
        if(inode->blocks[12] == 0) {
            inode->blocks[12] = ext2fs_allocblock(fs);

            ext2fs_inodewriteentry(inode, fs, inodeidx);
        }

        fs->backing->resource->write(fs->backing->resource, NULL, &block, inode->blocks[12] * fs->blksize + block * 4, sizeof(uint32_t));
    }

    return block;
}


ssize_t ext2fs_inoderead(struct ext2fs_inode *inode, struct ext2fs *fs, void *buf, off_t off, size_t count) {
    if(off > inode->sizelo) return 0;

    if((off + count) > inode->sizelo) count = inode->sizelo - off;

    for(size_t head = 0; head < count;) {
        size_t iblock = (off + head) / fs->blksize;

        size_t size = count - head;
        off = (off + head) % fs->blksize;

        if(size > (fs->blksize - off)) size = fs->blksize - off;

        uint32_t block = ext2fs_inodegetblock(inode, fs, (uint32_t)iblock);
        if(fs->backing->resource->read(fs->backing->resource, NULL, (void *)((uint64_t)buf + head), block * fs->blksize + off, size) == -1) return -1;

        head += size;
    }

    return count;
}

ssize_t ext2fs_inoderesize(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx, size_t start, size_t count) {
    size_t sectorsize = fs->backing->resource->stat.st_blksize;

    if((start + count) < (inode->sectors * sectorsize)) return 0;

    size_t iblockstart = (inode->sectors * sectorsize) / fs->blksize;
    size_t iblockend = (start + count) / fs->blksize;

    if(inode->sizelo < (start + count)) inode->sizelo = start + count;

    for(size_t i = iblockstart; i < iblockend; i++) {
        size_t block = ext2fs_allocblock(fs);

        inode->sectors = fs->blksize / sectorsize;
        ext2fs_inodesetblock(inode, fs, inodeidx, i, block);
    }

    ext2fs_inodewriteentry(inode, fs, inodeidx);

    return 0;
}

ssize_t ext2fs_inodewrite(struct ext2fs_inode *inode, struct ext2fs *fs, const void *buf, uint32_t inodeidx, off_t off, size_t count) {
    ext2fs_inoderesize(inode, fs, inodeidx, off, count);

    for(size_t head = 0; head < count;) {
        size_t iblock = (off + head) / fs->blksize;

        size_t size = count - head;
        off = (off + head) % fs->blksize;

        if(size > (fs->blksize - off)) size = fs->blksize - off;

        uint32_t block = ext2fs_inodegetblock(inode, fs, (uint32_t)iblock);
        if(fs->backing->resource->write(fs->backing->resource, NULL, (void *)((uint64_t)buf + head), block * fs->blksize + off, size) == -1) return -1;

        head += size;
    }

    return count;
}

ssize_t ext2fs_bgdreadentry(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t idx) {
    off_t off = 0;

    if(fs->blksize >= 2048) off = fs->blksize;
    else off = fs->blksize * 2;

    ASSERT_MSG(fs->backing->resource->read(fs->backing->resource, NULL, bgd, off + sizeof(struct ext2fs_blockgroupdesc) * idx, sizeof(struct ext2fs_blockgroupdesc)), "ext2fs: unable to read bgd entry");
    return 0;
}

ssize_t ext2fs_bgdwriteentry(struct ext2fs_blockgroupdesc *bgd, struct ext2fs *fs, uint32_t idx) {
    off_t off = 0;

    if(fs->blksize >= 2048) off = fs->blksize;
    else off = fs->blksize * 2;

    ASSERT_MSG(fs->backing->resource->write(fs->backing->resource, NULL, bgd, off + sizeof(struct ext2fs_blockgroupdesc) * idx, sizeof(struct ext2fs_blockgroupdesc)), "ext2fs: unable to write bgd entry");

    return 0;
}

ssize_t ext2fs_inodereadentry(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx) {
    size_t tableidx = (inodeidx - 1) % fs->sb.inodespergroup;
    size_t bgdidx = (inodeidx - 1) / fs->sb.inodespergroup;

    kernel_print("new thingy!\n");
    struct ext2fs_blockgroupdesc bgd = { 0 };
    ext2fs_bgdreadentry(&bgd, fs, bgdidx);

    ASSERT_MSG(fs->backing->resource->read(fs->backing->resource, NULL, inode, bgd.inodetable * fs->blksize + fs->sb.inodesize * tableidx, sizeof(struct ext2fs_inode)), "ext2fs: failed to read inode entry");

    return 1;
}

ssize_t ext2fs_inodewriteentry(struct ext2fs_inode *inode, struct ext2fs *fs, uint32_t inodeidx) {
    size_t tableidx = (inodeidx - 1) % fs->sb.inodespergroup;
    size_t bgdidx = (inodeidx - 1) / fs->sb.inodespergroup;

    struct ext2fs_blockgroupdesc bgd = { 0 };
    ext2fs_bgdreadentry(&bgd, fs, bgdidx);

    ASSERT_MSG(fs->backing->resource->write(fs->backing->resource, NULL, inode, bgd.inodetable * fs->blksize + fs->sb.inodesize * tableidx, sizeof(struct ext2fs_inode)), "ext2fs: failed to write inode entry");

    return 1;
}

static ssize_t ext2fs_createdirentry(struct ext2fs *fs, struct ext2fs_inode *parent, uint32_t parentidx, uint32_t newinode, uint8_t dirtype, const char *name) {
    void *buf = alloc(parent->sizelo);

    ext2fs_inoderead(parent, fs, buf, 0, parent->sizelo);

    bool found = false;

    for(size_t i = 0; i < parent->sizelo;) {
        struct ext2fs_direntry *direntry = (struct ext2fs_direntry *)((uint64_t)buf + i);

        if(found) {
            direntry->inodeidx = newinode;
            direntry->dirtype = dirtype;
            direntry->namelen = strlen(name);
            direntry->entsize = parent->sizelo - i;

            memcpy((void *)((uint64_t)direntry + sizeof(struct ext2fs_direntry)), name, direntry->namelen);

            ext2fs_inodewrite(parent, fs, buf, parentidx, 0, parent->sizelo);

            return 0;
        }

        size_t expected = ALIGN_UP(sizeof(struct ext2fs_direntry) + direntry->namelen, 4);
        if(direntry->entsize != expected) {
            direntry->entsize = expected;
            i += expected;
            found = true;
            continue;
        }

        i += direntry->entsize;

    }

    free(buf);

    return -1;
}

static ssize_t ext2fs_resread(struct resource *this_, struct f_description *description, void *buf, off_t loc, size_t count) {
    (void)description;
    struct ext2fs_inode curinode = { 0 };

    kernel_print("reading\n");
    ext2fs_inodereadentry(&curinode, ((struct ext2fs_resource *)this_)->fs, this_->stat.st_ino);

    return ext2fs_inoderead(&curinode, ((struct ext2fs_resource *)this_)->fs, buf, loc, count);
}

static ssize_t ext2fs_reswrite(struct resource *this_, struct f_description *description, const void *buf, off_t loc, size_t count) {
    (void)description;
    struct ext2fs_inode curinode = { 0 };

    kernel_print("writing %d\n", this_->stat.st_ino);
    ext2fs_inodereadentry(&curinode, ((struct ext2fs_resource *)this_)->fs, this_->stat.st_ino); // find inode entry associated with this

    return ext2fs_inodewrite(&curinode, ((struct ext2fs_resource *)this_)->fs, buf, this_->stat.st_ino, loc, count);
}

static struct vfs_node *ext2fs_mount(struct vfs_node *parent, const char *name, struct vfs_node *source);

static struct vfs_node *ext2fs_create(struct vfs_filesystem *this_, struct vfs_node *parent, const char *name, int mode) {
    struct vfs_node *node = NULL;
    struct ext2fs_resource *resource = NULL;
    struct ext2fs *this = (struct ext2fs *)this_;

    node = vfs_create_node(this_, parent, name, S_ISDIR(mode));
    if(node == NULL) goto fail;
    
    resource = resource_create(sizeof(struct ext2fs_resource));
    if(resource == NULL) goto fail;

    resource->read = ext2fs_resread;
    resource->write = ext2fs_reswrite;

    resource->stat.st_size = 0;
    resource->stat.st_blocks = 0;
    resource->stat.st_blksize = this->blksize;
    resource->stat.st_dev = this->devid;
    resource->stat.st_mode = mode;
    resource->stat.st_nlink = 1;

    resource->stat.st_atim = time_realtime;
    resource->stat.st_ctim = time_realtime;
    resource->stat.st_mtim = time_realtime;

    resource->stat.st_ino = ext2fs_allocinode(this);

    struct ext2fs_inode parentinode = { 0 };
    ext2fs_inodereadentry(&parentinode, this, parent->resource->stat.st_ino);

    uint8_t dirtype = S_ISREG(mode) ? 1 : 
        S_ISDIR(mode) ? 2 : 
        S_ISCHR(mode) ? 3 :
        S_ISBLK(mode) ? 4 :
        S_ISFIFO(mode) ? 5 :
        S_ISSOCK(mode) ? 6 :
        7;

    kernel_print("%llx\n", parent->resource);

    ext2fs_createdirentry(this, &parentinode, parent->resource->stat.st_ino, resource->stat.st_ino, dirtype, name);

    node->resource = (struct resource *)resource;
    kernel_print("created\n");
    return node;

fail:
    if(node != NULL) free(node); // TODO: Use vfs_destroy_node
    if(resource != NULL) free(resource);

    return NULL;
}

static inline struct vfs_filesystem *ext2fs_instantiate(void) {
    struct ext2fs *new_fs = alloc(sizeof(struct ext2fs));
    if(new_fs == NULL) return NULL; 

    new_fs->mount = ext2fs_mount;
    new_fs->create = ext2fs_create;
    // new_fs->symlink = ext2fs_symlink;
    // new_fs->link = ext2fs_link;

    return (struct vfs_filesystem *)new_fs;
}

static void ext2fs_populate(struct ext2fs *fs, struct vfs_node *node) {
    struct ext2fs_inode parent = { 0 };
    ext2fs_inodereadentry(&parent, fs, node->resource->stat.st_ino);

    void *buf = alloc(parent.sizelo);
    ext2fs_inoderead(&parent, fs, buf, 0, parent.sizelo);

    for(size_t i = 0; i < parent.sizelo;) {
        struct ext2fs_direntry *direntry = (struct ext2fs_direntry *)(buf + i);

        char *namebuf = alloc(direntry->namelen + 1);
        memcpy(namebuf, (void *)((uint64_t)direntry + sizeof(struct ext2fs_direntry)), direntry->namelen);

        if(direntry->inodeidx == 0) {
            free(namebuf);
            goto cleanup;
        }

        if(!strcmp(namebuf, ".") || !strcmp(namebuf, "..")) { i += direntry->entsize; continue; } // vfs already handles creating these

        kernel_print("populating %s\n", namebuf);

        struct ext2fs_inode inode = { 0 };
        ext2fs_inodereadentry(&inode, fs, direntry->inodeidx);

        uint16_t mode = 0644 | 
            (direntry->dirtype == 1 ? S_IFREG : 
             direntry->dirtype == 2 ? S_IFDIR : 
             direntry->dirtype == 3 ? S_IFCHR : 
             direntry->dirtype == 4 ? S_IFBLK : 
             direntry->dirtype == 5 ? S_IFIFO : 
             direntry->dirtype == 6 ? S_IFSOCK : 
             S_IFLNK);
        
        kernel_print("%d\n", S_ISDIR(mode));

        struct vfs_node *fnode = vfs_create_node((struct vfs_filesystem *)fs, node, namebuf, S_ISDIR(mode));
        struct ext2fs_resource *fres = resource_create(sizeof(struct ext2fs_resource));
        fres->read = ext2fs_resread;
        fres->write = ext2fs_reswrite;
        fres->stat.st_mode = mode;
        fres->stat.st_ino = direntry->inodeidx;
        fres->stat.st_size = inode.sizelo | ((uint64_t)inode.sizehi >> 32);
        fres->stat.st_nlink = inode.hardlinkcnt;
        fres->stat.st_blksize = fs->blksize;
        fres->stat.st_blocks = fres->stat.st_size / fs->blksize;

        fres->stat.st_atim = time_realtime;
        fres->stat.st_ctim = time_realtime;
        fres->stat.st_mtim = time_realtime;

        fres->fs = fs;

        fnode->resource = (struct resource *)fres;

        HASHMAP_SINSERT(&fnode->parent->children, namebuf, fnode);

        i += direntry->entsize;
    }
cleanup:
    free(buf);
    return;
}


static struct vfs_node *ext2fs_mount(struct vfs_node *parent, const char *name, struct vfs_node *source) {

    struct ext2fs *new_fs = (struct ext2fs *)ext2fs_instantiate();

    source->resource->read(source->resource, NULL, &new_fs->sb, source->resource->stat.st_blksize * 2, sizeof(struct ext2fs_superblock));

    if(new_fs->sb.sig != 0xef53) {
        panic(NULL, "Told to mount an ext2 filesystem whilst source is not ext2!");
    }

    new_fs->backing = source;
    new_fs->blksize = 1024 << new_fs->sb.blksize;
    new_fs->fragsize = 1024 << new_fs->sb.fragsize;
    new_fs->bgdcnt = new_fs->sb.blockcnt / new_fs->sb.blockspergroup;

    ASSERT_MSG(ext2fs_inodereadentry(&new_fs->root, new_fs, 2), "ext2fs: unable to read root inode");

    struct vfs_node *node = vfs_create_node((struct vfs_filesystem *)new_fs, parent, name, true);
    if(node == NULL) return NULL;
    struct ext2fs_resource *resource = resource_create(sizeof(struct ext2fs_resource));
    if(resource == NULL) return NULL; 

    resource->stat.st_size = new_fs->root.sizelo | ((uint64_t)new_fs->root.sizehi >> 32);
    resource->stat.st_blksize = new_fs->blksize;
    resource->stat.st_blocks = resource->stat.st_size / resource->stat.st_blksize;
    resource->stat.st_dev = source->resource->stat.st_rdev; // assign to device id of source device
    resource->stat.st_mode = 0644 | S_IFDIR;
    resource->stat.st_nlink = new_fs->root.hardlinkcnt;
    resource->stat.st_ino = 2;

    resource->stat.st_atim = time_realtime;
    resource->stat.st_ctim = time_realtime;
    resource->stat.st_mtim = time_realtime;

    resource->fs = new_fs;

    node->resource = (struct resource *)resource;

    ext2fs_populate(new_fs, node);

    return node; // root node (will become child of parent)
}

void ext2fs_init(void) {
    struct vfs_filesystem *new_fs = ext2fs_instantiate();
    if(new_fs == NULL) {
        panic(NULL, "Failed to instantiate ext2fs");
    } 

    vfs_add_filesystem(new_fs, "ext2fs");
}
