#include <sobi/vfs.h>
#include <sobi/kheap.h>
#include <sobi/string.h>

struct tmp_node {
    char name[VFS_NAME_MAX];
    struct tmp_node *parent;
    struct tmp_node *child;
    struct tmp_node *next;
    struct vfs_blob blob;
    bool dir;
    uint32_t ino;
    struct vfs_vnode vn;
};

static uint32_t tmp_next_ino = 1;
static const struct vfs_vnode_ops tmp_ops;

static struct tmp_node *tmp_of(struct vfs_vnode *vn)
{
    return vn->data;
}

static void tmp_free(struct vfs_vnode *vn)
{
    struct tmp_node *n = vn->data;
    vfs_blob_free(&n->blob);
    kfree(n);
}

static int tmp_bad_name(const char *name)
{
    if (!name || !name[0])
        return -EINVAL;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -EEXIST;
    if (strlen(name) >= VFS_NAME_MAX)
        return -ENAMETOOLONG;
    return 0;
}

static struct tmp_node *tmp_find(struct tmp_node *dir, const char *name)
{
    for (struct tmp_node *c = dir->child; c; c = c->next) {
        if (strcmp(c->name, name) == 0)
            return c;
    }
    return NULL;
}

static struct tmp_node *tmp_make(struct tmp_node *parent, const char *name, bool dir)
{
    struct tmp_node *n = kzalloc(sizeof(*n));
    if (!n)
        return NULL;
    size_t nl = strlen(name);
    if (nl >= VFS_NAME_MAX) {
        kfree(n);
        return NULL;
    }
    if (nl)
        memcpy(n->name, name, nl);
    n->dir = dir;
    n->ino = tmp_next_ino++;
    n->parent = parent ? parent : n;
    n->vn.ops = &tmp_ops;
    n->vn.data = n;
    n->vn.ref = 1;
    n->vn.flags = dir ? VFS_VN_DIR : 0;
    n->vn.drop = tmp_free;
    if (parent) {
        n->next = parent->child;
        parent->child = n;
    }
    return n;
}

static void tmp_unlink_child(struct tmp_node *parent, struct tmp_node *n)
{
    struct tmp_node **pp = &parent->child;
    while (*pp) {
        if (*pp == n) {
            *pp = n->next;
            n->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

static void tmp_fill(struct vfs_dirent *out, const char *name, uint32_t ino, bool dir)
{
    memset(out, 0, sizeof(*out));
    size_t nl = strlen(name);
    if (nl >= VFS_NAME_MAX)
        nl = VFS_NAME_MAX - 1;
    memcpy(out->name, name, nl);
    out->ino = ino;
    out->mode = dir ? VFS_S_IFDIR : VFS_S_IFREG;
}

static int tmp_lookup(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    struct tmp_node *n = tmp_of(dir);
    if (!name || !out)
        return -EINVAL;
    if (strcmp(name, ".") == 0) {
        *out = &n->vn;
        return 0;
    }
    if (strcmp(name, "..") == 0) {
        *out = &n->parent->vn;
        return 0;
    }
    struct tmp_node *c = tmp_find(n, name);
    if (!c)
        return -ENOENT;
    *out = &c->vn;
    return 0;
}

static int tmp_read(struct vfs_vnode *vn, void *buf, size_t len, uint64_t off, size_t *nout)
{
    struct tmp_node *n = tmp_of(vn);
    if (n->dir)
        return -EISDIR;
    return vfs_blob_read(&n->blob, off, buf, len, nout);
}

static int tmp_write(struct vfs_vnode *vn, const void *buf, size_t len, uint64_t off, size_t *nout)
{
    struct tmp_node *n = tmp_of(vn);
    if (n->dir)
        return -EISDIR;
    return vfs_blob_write(&n->blob, off, buf, len, nout);
}

static int tmp_readdir(struct vfs_vnode *dir, uint64_t index, struct vfs_dirent *out)
{
    struct tmp_node *n = tmp_of(dir);
    if (!n->dir)
        return -ENOTDIR;
    if (index == 0) {
        tmp_fill(out, ".", n->ino, true);
        return 0;
    }
    if (index == 1) {
        tmp_fill(out, "..", n->parent->ino, true);
        return 0;
    }
    uint64_t skip = index - 2;
    for (struct tmp_node *c = n->child; c; c = c->next) {
        if (skip == 0) {
            tmp_fill(out, c->name, c->ino, c->dir);
            return 0;
        }
        skip--;
    }
    return -ENOENT;
}

static int tmp_stat(struct vfs_vnode *vn, struct vfs_stat *st)
{
    struct tmp_node *n = tmp_of(vn);
    if (!st)
        return -EINVAL;
    st->ino = n->ino;
    st->size = n->dir ? 0 : n->blob.size;
    st->mode = (n->dir ? VFS_S_IFDIR : VFS_S_IFREG) | 0755u;
    return 0;
}

static int tmp_create(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    int rc = tmp_bad_name(name);
    if (rc)
        return rc;
    struct tmp_node *parent = tmp_of(dir);
    if (!parent->dir)
        return -ENOTDIR;
    if (tmp_find(parent, name))
        return -EEXIST;
    struct tmp_node *n = tmp_make(parent, name, false);
    if (!n)
        return -ENOMEM;
    if (out)
        *out = &n->vn;
    return 0;
}

static int tmp_mkdir(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    int rc = tmp_bad_name(name);
    if (rc)
        return rc;
    struct tmp_node *parent = tmp_of(dir);
    if (!parent->dir)
        return -ENOTDIR;
    if (tmp_find(parent, name))
        return -EEXIST;
    struct tmp_node *n = tmp_make(parent, name, true);
    if (!n)
        return -ENOMEM;
    if (out)
        *out = &n->vn;
    return 0;
}

static int tmp_unlink(struct vfs_vnode *dir, const char *name)
{
    if (!name)
        return -EINVAL;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -EINVAL;
    struct tmp_node *parent = tmp_of(dir);
    struct tmp_node *n = tmp_find(parent, name);
    if (!n)
        return -ENOENT;
    if (n->dir && n->child)
        return -ENOTEMPTY;
    tmp_unlink_child(parent, n);
    n->vn.flags |= VFS_VN_ORPHAN;
    n->parent = NULL;
    if (n->vn.ref > 0)
        n->vn.ref--;
    if (n->vn.ref == 0)
        tmp_free(&n->vn);
    return 0;
}

static const struct vfs_vnode_ops tmp_ops = {
    .lookup = tmp_lookup,
    .read = tmp_read,
    .write = tmp_write,
    .readdir = tmp_readdir,
    .stat = tmp_stat,
    .create = tmp_create,
    .unlink = tmp_unlink,
    .mkdir = tmp_mkdir,
};

struct vfs_vnode *tmpfs_new(void)
{
    struct tmp_node *n = tmp_make(NULL, "", true);
    if (!n)
        return NULL;
    return &n->vn;
}
