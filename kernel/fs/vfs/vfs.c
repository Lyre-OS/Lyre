#include <stdbool.h>
#include <fs/vfs/vfs.h>
#include <lib/alloc.h>
#include <lib/hashmap.h>
#include <lib/lock.h>
#include <lib/errno.h>
#include <lib/print.h>
#include <bits/posix/stat.h>

static spinlock_t vfs_lock = SPINLOCK_INIT;

struct vfs_node *vfs_create_node(struct vfs_filesystem *fs, struct vfs_node *parent,
                                 const char *name, bool dir) {
    struct vfs_node *node = ALLOC(struct vfs_node);

    node->name = alloc(strlen(name) + 1);
    strcpy(node->name, name);

    node->parent = parent;
    node->filesystem = fs;

    if (dir) {
        node->children = (typeof(node->children))HASHMAP_INIT(256);
    }

    return node;
}

static void create_dotentries(struct vfs_node *node, struct vfs_node *parent) {
    struct vfs_node *dot = vfs_create_node(node->filesystem, node, ".", false);
    struct vfs_node *dotdot = vfs_create_node(node->filesystem, node, "..", false);

    dot->redir = node;
    dotdot->redir = parent;

    HASHMAP_SINSERT(&node->children, ".", dot);
    HASHMAP_SINSERT(&node->children, "..", dotdot);
}

static HASHMAP_TYPE(struct vfs_filesystem *) filesystems;

void vfs_add_filesystem(struct vfs_filesystem *fs, const char *identifier) {
    spinlock_acquire(&vfs_lock);

    HASHMAP_SINSERT(&filesystems, identifier, fs);

    spinlock_release(&vfs_lock);
}

struct vfs_node *vfs_root = NULL;

void vfs_init(void) {
    vfs_root = vfs_create_node(NULL, NULL, "", false);

    filesystems = (typeof(filesystems))HASHMAP_INIT(256);
}

struct path2node_res {
    struct vfs_node *target_parent;
    struct vfs_node *target;
    char *basename;
};

static struct vfs_node *reduce_node(struct vfs_node *node, bool follow_symlinks);

static struct path2node_res path2node(struct vfs_node *parent, const char *path) {
    if (path == NULL || strlen(path) == 0) {
        errno = ENOENT;
        return (struct path2node_res){NULL, NULL, NULL};
    }

    size_t path_len = strlen(path);

    size_t index = 0;
    struct vfs_node *current_node = reduce_node(parent, false);

    if (path[index] == '/') {
        current_node = reduce_node(vfs_root, false);
        while (path[index] == '/') {
            if (index == path_len - 2) {
                return (struct path2node_res){current_node, current_node, ""};
            }
            index++;
        }
    }

    for (;;) {
        const char *elem = &path[index];
        size_t elem_len = 0;

        while (index < path_len && path[index] != '/') {
            elem_len++, index++;
        }

        while (index < path_len && path[index] != '/') {
            index++;
        }

        bool last = index == path_len;

        char *elem_str = alloc(elem_len + 1);
        memcpy(elem_str, elem, elem_len);

        current_node = reduce_node(current_node, false);

        struct vfs_node *new_node;
        if (!HASHMAP_SGET(&current_node->children, new_node, elem_str)) {
            errno = ENOENT;
            if (last) {
                return (struct path2node_res){current_node, NULL, elem_str};
            }
            return (struct path2node_res){NULL, NULL, NULL};
        }

        new_node = reduce_node(new_node, false);

        if (last) {
            return (struct path2node_res){current_node, new_node, elem_str};
        }

        current_node = new_node;

        if (S_ISLNK(current_node->resource->stat.st_mode)) {
            struct path2node_res r = path2node(current_node->parent, current_node->symlink_target);
            if (r.target == NULL) {
                return (struct path2node_res){NULL, NULL, NULL};
            }
            continue;
        }

        if (!S_ISDIR(current_node->resource->stat.st_mode)) {
            errno = ENOTDIR;
            return (struct path2node_res){NULL, NULL, NULL};
        }
    }

    errno = ENOENT;
    return (struct path2node_res){NULL, NULL, NULL};
}

static struct vfs_node *reduce_node(struct vfs_node *node, bool follow_symlinks) {
    if (node->redir != NULL) {
        return reduce_node(node->redir, follow_symlinks);
    }
    if (node->mountpoint != NULL) {
        return reduce_node(node->mountpoint, follow_symlinks);
    }
    if (node->symlink_target != NULL && follow_symlinks == true) {
        struct path2node_res r = path2node(node->parent, node->symlink_target);
        if (r.target == NULL) {
            return NULL;
        }
        return reduce_node(r.target, follow_symlinks);
    }
    return node;
}

struct vfs_node *vfs_get_node(struct vfs_node *parent, const char *path, bool follow_links) {
    spinlock_acquire(&vfs_lock);

    struct vfs_node *ret = NULL;

    struct path2node_res r = path2node(parent, path);
    if (r.target == NULL) {
        goto cleanup;
    }

    if (follow_links) {
        ret = reduce_node(r.target, true);
        goto cleanup;
    }

    ret = r.target;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}

bool vfs_mount(struct vfs_node *parent, const char *source, const char *target,
               const char *fs_name) {
    spinlock_acquire(&vfs_lock);

    bool ret = false;
    struct path2node_res r = {0};

    struct vfs_filesystem *fs;
    if (!HASHMAP_SGET(&filesystems, fs, fs_name)) {
        errno = ENODEV;
        goto cleanup;
    }

    struct vfs_node *source_node = NULL;
    if (source != NULL && strlen(source) != 0) {
        struct path2node_res rr = path2node(parent, source);
        source_node = rr.target;
        if (rr.basename != NULL) {
            free(rr.basename);
        }
        if (source_node == NULL) {
            goto cleanup;
        }
        if (!S_ISDIR(source_node->resource->stat.st_mode)) {
            errno = EISDIR;
            goto cleanup;
        }
    }

    r = path2node(parent, target);

    bool mounting_root = r.target == vfs_root;

    if (r.target == NULL) {
        goto cleanup;
    }

    if (!mounting_root && !S_ISDIR(r.target->resource->stat.st_mode)) {
        errno = EISDIR;
        goto cleanup;
    }

    fs = fs->instantiate();

    struct vfs_node *mount_node = fs->mount(r.target_parent, r.basename, source_node);

    r.target->mountpoint = mount_node;

    create_dotentries(mount_node, r.target_parent);

    if (source != NULL && strlen(source) != 0) {
        print("vfs: Mounted `%s` on `%s` with filesystem `%s`\n", source, target, fs_name);
    } else {
        print("vfs: Mounted %s on `%s`\n", fs_name, target);
    }

    ret = true;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}

struct vfs_node *vfs_symlink(struct vfs_node *parent, const char *dest,
                             const char *target) {
    spinlock_acquire(&vfs_lock);

    struct vfs_node *ret = NULL;

    struct path2node_res r = path2node(parent, target);

    if (r.target_parent == NULL) {
        goto cleanup;
    }

    if (r.target != NULL) {
        errno = EEXIST;
        goto cleanup;
    }

    struct vfs_node *target_node =
        r.target_parent->filesystem->symlink(r.target_parent, dest, r.basename);

    HASHMAP_SINSERT(&r.target_parent->children, r.basename, r.target);

    ret = target_node;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}

struct vfs_node *vfs_create(struct vfs_node *parent, const char *name, int mode) {
    spinlock_acquire(&vfs_lock);

    struct vfs_node *ret = NULL;

    struct path2node_res r = path2node(parent, name);

    if (r.target_parent == NULL) {
        goto cleanup;
    }

    if (r.target != NULL) {
        errno = EEXIST;
        goto cleanup;
    }

    struct vfs_node *target_node =
        r.target_parent->filesystem->create(r.target_parent, r.basename, mode);

    HASHMAP_SINSERT(&r.target_parent->children, r.basename, r.target);

    if (S_ISDIR(target_node->resource->stat.st_mode)) {
        create_dotentries(target_node, r.target_parent);
    }

    ret = target_node;

cleanup:
    if (r.basename != NULL) {
        free(r.basename);
    }
    spinlock_release(&vfs_lock);
    return ret;
}
