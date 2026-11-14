#include <sobi/vfs.h>
#include <sobi/kheap.h>
#include <sobi/string.h>

struct ir_node {
    char name[VFS_NAME_MAX];
    struct ir_node *parent;
    struct ir_node *child;
    struct ir_node *next;
    struct vfs_blob blob;
    bool dir;
    uint32_t ino;
    struct vfs_vnode vn;
};

static uint32_t ir_next_ino;
static const struct vfs_vnode_ops ir_ops;

static struct ir_node *ir_of(struct vfs_vnode *vn)
{
    return vn->data;
}

static struct ir_node *ir_find(struct ir_node *dir, const char *name)
{
    for (struct ir_node *c = dir->child; c; c = c->next) {
        if (strcmp(c->name, name) == 0)
            return c;
    }
    return NULL;
}

static struct ir_node *ir_add(struct ir_node *parent, const char *name, bool dir)
{
    size_t nl = strlen(name);
    if (nl >= VFS_NAME_MAX)
        return NULL;
    struct ir_node *n = kzalloc(sizeof(*n));
    if (!n)
        return NULL;
    if (nl)
        memcpy(n->name, name, nl);
    n->dir = dir;
    n->ino = ir_next_ino++;
    n->parent = parent ? parent : n;
    n->vn.ops = &ir_ops;
    n->vn.data = n;
    n->vn.ref = 1;
    n->vn.flags = VFS_VN_RO | (dir ? VFS_VN_DIR : 0);
    if (parent) {
        n->next = parent->child;
        parent->child = n;
    }
    return n;
}

static void ir_fill(struct vfs_dirent *out, const char *name, uint32_t ino, bool dir)
{
    memset(out, 0, sizeof(*out));
    size_t nl = strlen(name);
    if (nl >= VFS_NAME_MAX)
        nl = VFS_NAME_MAX - 1;
    memcpy(out->name, name, nl);
    out->ino = ino;
    out->mode = dir ? VFS_S_IFDIR : VFS_S_IFREG;
}

static int ir_lookup(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    struct ir_node *n = ir_of(dir);
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
    struct ir_node *c = ir_find(n, name);
    if (!c)
        return -ENOENT;
    *out = &c->vn;
    return 0;
}

static int ir_read(struct vfs_vnode *vn, void *buf, size_t len, uint64_t off, size_t *nout)
{
    struct ir_node *n = ir_of(vn);
    if (n->dir)
        return -EISDIR;
    return vfs_blob_read(&n->blob, off, buf, len, nout);
}

static int ir_write(struct vfs_vnode *vn, const void *buf, size_t len, uint64_t off, size_t *nout)
{
    (void)vn;
    (void)buf;
    (void)len;
    (void)off;
    (void)nout;
    return -EROFS;
}

static int ir_readdir(struct vfs_vnode *dir, uint64_t index, struct vfs_dirent *out)
{
    struct ir_node *n = ir_of(dir);
    if (!n->dir)
        return -ENOTDIR;
    if (index == 0) {
        ir_fill(out, ".", n->ino, true);
        return 0;
    }
    if (index == 1) {
        ir_fill(out, "..", n->parent->ino, true);
        return 0;
    }
    uint64_t skip = index - 2;
    for (struct ir_node *c = n->child; c; c = c->next) {
        if (skip == 0) {
            ir_fill(out, c->name, c->ino, c->dir);
            return 0;
        }
        skip--;
    }
    return -ENOENT;
}

static int ir_stat(struct vfs_vnode *vn, struct vfs_stat *st)
{
    struct ir_node *n = ir_of(vn);
    if (!st)
        return -EINVAL;
    st->ino = n->ino;
    st->size = n->dir ? 0 : n->blob.size;
    st->mode = (n->dir ? VFS_S_IFDIR : VFS_S_IFREG) | 0555u;
    return 0;
}

static int ir_create(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    (void)dir;
    (void)name;
    (void)out;
    return -EROFS;
}

static int ir_unlink(struct vfs_vnode *dir, const char *name)
{
    (void)dir;
    (void)name;
    return -EROFS;
}

static const struct vfs_vnode_ops ir_ops = {
    .lookup = ir_lookup,
    .read = ir_read,
    .write = ir_write,
    .readdir = ir_readdir,
    .stat = ir_stat,
    .create = ir_create,
    .unlink = ir_unlink,
    .mkdir = ir_create,
};

static bool ir_zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i])
            return false;
    }
    return true;
}

static uint64_t ir_octal(const char *s, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == 0 || c == ' ')
            break;
        if (c < '0' || c > '7')
            break;
        v = (v << 3) + (uint64_t)(c - '0');
    }
    return v;
}

static uint32_t ir_ustar_sum(const uint8_t *hdr)
{
    uint32_t sum = 0;
    for (int i = 0; i < 512; i++) {
        if (i >= 148 && i < 156)
            sum += (uint8_t)' ';
        else
            sum += hdr[i];
    }
    return sum;
}

static int ir_ustar_name(const uint8_t *hdr, char *out, size_t outn)
{
    char name[101];
    char prefix[156];
    size_t nl = 0;
    while (nl < 100 && hdr[nl])
        nl++;
    memcpy(name, hdr, nl);
    name[nl] = 0;
    size_t pl = 0;
    const uint8_t *pre = hdr + 345;
    while (pl < 155 && pre[pl])
        pl++;
    memcpy(prefix, pre, pl);
    prefix[pl] = 0;
    char joined[256];
    const char *path = name;
    if (pl) {
        if (pl + 1 + nl >= sizeof(joined))
            return -ENAMETOOLONG;
        memcpy(joined, prefix, pl);
        joined[pl] = '/';
        memcpy(joined + pl + 1, name, nl + 1);
        path = joined;
    }
    while (path[0] == '/')
        path++;
    if (path[0] == '.' && path[1] == '/')
        path += 2;
    size_t L = strlen(path);
    if (L >= outn)
        return -ENAMETOOLONG;
    memcpy(out, path, L + 1);
    return 0;
}

static int ir_install(struct ir_node *root, const char *path, bool as_dir, const uint8_t *data, size_t len)
{
    if (!path[0])
        return as_dir ? 0 : -EINVAL;
    struct ir_node *cur = root;
    const char *p = path;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t nl = (size_t)(p - start);
        if (nl == 0 || nl >= VFS_NAME_MAX)
            return -ENAMETOOLONG;
        char name[VFS_NAME_MAX];
        memcpy(name, start, nl);
        name[nl] = 0;
        const char *rest = p;
        while (*rest == '/')
            rest++;
        bool leaf = *rest == 0;
        struct ir_node *child = ir_find(cur, name);
        if (!child) {
            bool dir = !leaf || as_dir;
            child = ir_add(cur, name, dir);
            if (!child)
                return -ENOMEM;
            if (leaf && !as_dir && len) {
                size_t wrote = 0;
                int rc = vfs_blob_write(&child->blob, 0, data, len, &wrote);
                if (rc || wrote != len)
                    return rc ? rc : -EIO;
            }
        } else if (!child->dir) {
            return -EEXIST;
        } else if (leaf && !as_dir) {
            return -EEXIST;
        }
        cur = child;
        p = rest;
        if (leaf)
            break;
    }
    return 0;
}

struct vfs_vnode *initramfs_load(const void *data, size_t len)
{
    if (!data || len < 512)
        return NULL;
    ir_next_ino = 1;
    struct ir_node *root = ir_add(NULL, "", true);
    if (!root)
        return NULL;
    const uint8_t *image = data;
    size_t off = 0;
    while (off + 512 <= len) {
        const uint8_t *hdr = image + off;
        if (ir_zero(hdr, 512))
            break;
        if (memcmp(hdr + 257, "ustar", 5) != 0)
            return NULL;
        uint32_t stored = (uint32_t)ir_octal((const char *)hdr + 148, 8);
        if (stored != ir_ustar_sum(hdr))
            return NULL;
        char path[256];
        if (ir_ustar_name(hdr, path, sizeof(path)))
            return NULL;
        uint64_t sz = ir_octal((const char *)hdr + 124, 12);
        char type = (char)hdr[156];
        off += 512;
        if (sz > len - off)
            return NULL;
        size_t span = (size_t)((sz + 511u) & ~(uint64_t)511u);
        if (span < (size_t)sz || span > len - off)
            return NULL;
        int rc;
        if (type == '5')
            rc = ir_install(root, path, true, NULL, 0);
        else if (type == '0' || type == '\0')
            rc = ir_install(root, path, false, image + off, (size_t)sz);
        else
            rc = 0;
        if (rc)
            return NULL;
        off += span;
    }
    return &root->vn;
}
