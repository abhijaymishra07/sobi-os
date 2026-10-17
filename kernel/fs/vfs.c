#include <sobi/vfs.h>
#include <sobi/kheap.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/proc.h>
#include <sobi/syscall.h>
#include <sobi/uaccess.h>

struct fsnode {
    char name[VFS_NAME_MAX];
    struct fsnode *parent;
    struct fsnode *child;
    struct fsnode *next;
    struct vfs_vnode *vn;
    bool dir;
    bool ro;
    uint8_t *data;
    size_t size;
    size_t cap;
};

static struct vfs_vnode *root_vn;
static struct vfs_vnode *tmp_vn;
static const void *initramfs_data;
static size_t initramfs_len;

static const struct vfs_vnode_ops node_ops;

static struct fsnode *node_of(struct vfs_vnode *vn)
{
    return vn ? vn->data : NULL;
}

static void copy_name(char *dst, const char *src)
{
    size_t i = 0;
    if (!src)
        src = "";
    while (src[i] && i + 1 < VFS_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static struct vfs_vnode *make_vn(const char *name, bool dir, bool ro)
{
    struct vfs_vnode *vn = kzalloc(sizeof(*vn));
    struct fsnode *node = kzalloc(sizeof(*node));
    if (!vn || !node)
        panic("vfs: out of memory");
    copy_name(node->name, name);
    node->dir = dir;
    node->ro = ro;
    node->vn = vn;
    vn->ops = &node_ops;
    vn->data = node;
    vn->flags = dir ? VFS_VN_DIR : 0;
    if (ro)
        vn->flags |= VFS_VN_RO;
    vn->ref = 1;
    return vn;
}

static struct fsnode *find_child(struct fsnode *dir, const char *name)
{
    for (struct fsnode *c = dir->child; c; c = c->next) {
        if (strcmp(c->name, name) == 0)
            return c;
    }
    return NULL;
}

static int node_lookup(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    struct fsnode *node = node_of(dir);
    if (!node)
        return -ENOENT;
    if (strcmp(name, "..") == 0) {
        struct vfs_vnode *up = node->parent ? node->parent->vn : dir;
        *out = up;
        up->ref++;
        return 0;
    }
    if (!node->dir)
        return -ENOTDIR;
    if (strcmp(name, ".") == 0) {
        *out = dir;
        dir->ref++;
        return 0;
    }
    struct fsnode *child = find_child(node, name);
    if (!child)
        return -ENOENT;
    *out = child->vn;
    child->vn->ref++;
    return 0;
}

static int node_read(struct vfs_vnode *vn, void *buf, size_t len, uint64_t off, size_t *nout)
{
    struct fsnode *node = node_of(vn);
    if (node->dir)
        return -EISDIR;
    if (off >= node->size) {
        *nout = 0;
        return 0;
    }
    size_t have = node->size - (size_t)off;
    if (len > have)
        len = have;
    memcpy(buf, node->data + off, len);
    *nout = len;
    return 0;
}

static int node_write(struct vfs_vnode *vn, const void *buf, size_t len, uint64_t off, size_t *nout)
{
    struct fsnode *node = node_of(vn);
    if (node->ro)
        return -EROFS;
    if (node->dir)
        return -EISDIR;
    uint64_t end = off + len;
    if (end < off)
        return -EFBIG;
    if (end > node->cap) {
        size_t cap = node->cap ? node->cap : 64;
        while (cap < end) {
            if (cap > (SIZE_MAX / 2))
                return -EFBIG;
            cap *= 2;
        }
        uint8_t *next = krealloc(node->data, cap);
        if (!next)
            return -ENOMEM;
        if (cap > node->cap)
            memset(next + node->cap, 0, cap - node->cap);
        node->data = next;
        node->cap = cap;
    }
    memcpy(node->data + off, buf, len);
    if (end > node->size)
        node->size = (size_t)end;
    *nout = len;
    return 0;
}

static int node_readdir(struct vfs_vnode *dir, uint64_t index, struct vfs_dirent *out)
{
    struct fsnode *node = node_of(dir);
    if (!node->dir)
        return -ENOTDIR;
    memset(out, 0, sizeof(*out));
    if (index == 0) {
        copy_name(out->name, ".");
        out->mode = VFS_S_IFDIR;
        return 0;
    }
    if (index == 1) {
        copy_name(out->name, "..");
        out->mode = VFS_S_IFDIR;
        return 0;
    }
    uint64_t n = 2;
    for (struct fsnode *c = node->child; c; c = c->next, n++) {
        if (n == index) {
            copy_name(out->name, c->name);
            out->mode = c->dir ? VFS_S_IFDIR : VFS_S_IFREG;
            return 0;
        }
    }
    return -ENOENT;
}

static int node_stat(struct vfs_vnode *vn, struct vfs_stat *st)
{
    struct fsnode *node = node_of(vn);
    memset(st, 0, sizeof(*st));
    st->mode = node->dir ? VFS_S_IFDIR : VFS_S_IFREG;
    st->size = node->dir ? 0 : node->size;
    return 0;
}

static struct vfs_vnode *add_child(struct fsnode *dir, const char *name, bool is_dir)
{
    struct vfs_vnode *vn = make_vn(name, is_dir, dir->ro);
    struct fsnode *node = node_of(vn);
    node->parent = dir;
    node->next = dir->child;
    dir->child = node;
    return vn;
}

static int node_create(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    struct fsnode *node = node_of(dir);
    if (!node->dir)
        return -ENOTDIR;
    if (node->ro)
        return -EROFS;
    if (!name || !name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -EINVAL;
    if (find_child(node, name))
        return -EEXIST;
    *out = add_child(node, name, false);
    return 0;
}

static int node_mkdir(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out)
{
    struct fsnode *node = node_of(dir);
    if (!node->dir)
        return -ENOTDIR;
    if (node->ro)
        return -EROFS;
    if (!name || !name[0])
        return -EINVAL;
    if (find_child(node, name))
        return -EEXIST;
    *out = add_child(node, name, true);
    return 0;
}

static int node_unlink(struct vfs_vnode *dir, const char *name)
{
    struct fsnode *node = node_of(dir);
    if (!node->dir)
        return -ENOTDIR;
    if (node->ro)
        return -EROFS;
    struct fsnode **pp = &node->child;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            struct fsnode *dead = *pp;
            if (dead->dir && dead->child)
                return -ENOTEMPTY;
            *pp = dead->next;
            dead->vn->flags |= VFS_VN_ORPHAN;
            if (dead->vn->ref <= 1) {
                kfree(dead->data);
                kfree(dead->vn);
                kfree(dead);
            }
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -ENOENT;
}

static const struct vfs_vnode_ops node_ops = {
    node_lookup, node_read, node_write, node_readdir, node_stat, node_create, node_unlink, node_mkdir,
};

static struct vfs_vnode *make_vn(const char *name, bool dir, bool ro);

static struct vfs_vnode *vnode_new(const char *name, bool dir, bool ro)
{
    struct vfs_vnode *vn = make_vn(name, dir, ro);
    vn->ops = &node_ops;
    return vn;
}

static struct vfs_vnode *make_tmpfs(void)
{
    return vnode_new("tmp", true, false);
}

static uint64_t parse_octal(const char *s, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n && s[i] && s[i] != ' '; i++) {
        if (s[i] < '0' || s[i] > '7')
            break;
        v = (v << 3) + (uint64_t)(s[i] - '0');
    }
    return v;
}

static struct vfs_vnode *ensure_dir(struct vfs_vnode *base, const char *name)
{
    struct vfs_vnode *child = NULL;
    int rc = node_lookup(base, name, &child);
    if (rc == 0)
        return child;
    struct fsnode *node = node_of(base);
    bool saved = node->ro;
    node->ro = false;
    rc = node_mkdir(base, name, &child);
    node->ro = saved;
    if (rc != 0)
        return NULL;
    node_of(child)->ro = true;
    child->flags |= VFS_VN_RO;
    return child;
}

static struct vfs_vnode *load_ustar(const void *data, size_t len)
{
    struct vfs_vnode *tree = vnode_new("", true, true);
    const uint8_t *p = data;
    size_t off = 0;
    while (off + 512 <= len) {
        const uint8_t *hdr = p + off;
        if (hdr[0] == 0)
            break;
        char name[VFS_NAME_MAX];
        memset(name, 0, sizeof(name));
        size_t nlen = 0;
        while (nlen < 100 && hdr[nlen] && nlen + 1 < VFS_NAME_MAX) {
            name[nlen] = (char)hdr[nlen];
            nlen++;
        }
        name[nlen] = 0;
        uint64_t size = parse_octal((const char *)hdr + 124, 12);
        char type = (char)hdr[156];
        off += 512;
        if (off + size > len)
            break;
        struct vfs_vnode *dir = tree;
        char leaf[VFS_NAME_MAX];
        memset(leaf, 0, sizeof(leaf));
        const char *s = name;
        while (*s) {
            while (*s == '/')
                s++;
            if (!*s)
                break;
            char part[VFS_NAME_MAX];
            size_t i = 0;
            while (s[i] && s[i] != '/' && i + 1 < VFS_NAME_MAX) {
                part[i] = s[i];
                i++;
            }
            part[i] = 0;
            s += i;
            if (*s == '/') {
                struct vfs_vnode *next = ensure_dir(dir, part);
                if (dir != tree)
                    dir->ref--;
                if (!next)
                    break;
                dir = next;
                continue;
            }
            copy_name(leaf, part);
            break;
        }
        if (leaf[0]) {
            if (type == '5') {
                struct vfs_vnode *d = ensure_dir(dir, leaf);
                if (d && d != tree)
                    d->ref--;
            } else {
                struct fsnode *parent = node_of(dir);
                bool saved = parent->ro;
                parent->ro = false;
                struct vfs_vnode *file = NULL;
                if (node_create(dir, leaf, &file) == 0) {
                    size_t wrote = 0;
                    node_of(file)->ro = false;
                    node_write(file, p + off, (size_t)size, 0, &wrote);
                    node_of(file)->ro = true;
                    file->flags |= VFS_VN_RO;
                }
                parent->ro = saved;
            }
        }
        if (dir != tree)
            dir->ref--;
        off += (size + 511) & ~511ULL;
    }
    return tree;
}

void vfs_set_initramfs(const void *data, size_t len)
{
    initramfs_data = data;
    initramfs_len = len;
}

struct vfs_filetable *vfs_table_new(void)
{
    return kzalloc(sizeof(struct vfs_filetable));
}

void vfs_init(void)
{
    if (initramfs_data && initramfs_len)
        root_vn = load_ustar(initramfs_data, initramfs_len);
    else
        root_vn = vnode_new("", true, false);
    tmp_vn = make_tmpfs();
    pr_info("vfs", "root %s, tmpfs at /tmp", initramfs_data ? "initramfs" : "tmpfs");
}

static int split_next(const char **path, char *part)
{
    const char *s = *path;
    while (*s == '/')
        s++;
    if (!*s) {
        *path = s;
        part[0] = 0;
        return 0;
    }
    size_t i = 0;
    while (s[i] && s[i] != '/') {
        if (i + 1 >= VFS_NAME_MAX)
            return -ENAMETOOLONG;
        part[i] = s[i];
        i++;
    }
    part[i] = 0;
    *path = s + i;
    return 1;
}

static int walk(const char *path, struct vfs_vnode **out)
{
    if (!path || path[0] != '/')
        return -EINVAL;
    struct vfs_vnode *cur = root_vn;
    cur->ref++;
    char part[VFS_NAME_MAX];
    const char *p = path;
    bool first = true;
    for (;;) {
        int got = split_next(&p, part);
        if (got < 0) {
            cur->ref--;
            return got;
        }
        if (got == 0)
            break;
        if (first && strcmp(part, "tmp") == 0) {
            cur->ref--;
            cur = tmp_vn;
            cur->ref++;
            first = false;
            continue;
        }
        first = false;
        struct vfs_vnode *next = NULL;
        int rc = cur->ops->lookup(cur, part, &next);
        cur->ref--;
        if (rc != 0)
            return rc;
        cur = next;
    }
    *out = cur;
    return 0;
}

static int walk_parent(const char *path, struct vfs_vnode **dir, char *leaf)
{
    if (!path || path[0] != '/')
        return -EINVAL;
    char parts[8][VFS_NAME_MAX];
    int count = 0;
    const char *p = path;
    for (;;) {
        int got = split_next(&p, parts[count]);
        if (got < 0)
            return got;
        if (got == 0)
            break;
        if (count == 7)
            return -ENAMETOOLONG;
        count++;
    }
    if (count == 0)
        return -EINVAL;
    copy_name(leaf, parts[count - 1]);
    struct vfs_vnode *cur = root_vn;
    cur->ref++;
    int begin = 0;
    if (strcmp(parts[0], "tmp") == 0) {
        cur->ref--;
        cur = tmp_vn;
        cur->ref++;
        begin = 1;
    }
    for (int i = begin; i < count - 1; i++) {
        struct vfs_vnode *next = NULL;
        int rc = cur->ops->lookup(cur, parts[i], &next);
        cur->ref--;
        if (rc != 0)
            return rc;
        cur = next;
    }
    *dir = cur;
    return 0;
}

int vfs_open(struct vfs_filetable *ft, const char *path, int flags)
{
    if (!ft)
        return -EINVAL;
    int fd = -1;
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (!ft->files[i].used) {
            fd = i;
            break;
        }
    }
    if (fd < 0)
        return -EMFILE;
    struct vfs_vnode *vn = NULL;
    int rc = walk(path, &vn);
    if (rc == -ENOENT && (flags & O_CREAT)) {
        struct vfs_vnode *dir = NULL;
        char leaf[VFS_NAME_MAX];
        rc = walk_parent(path, &dir, leaf);
        if (rc == 0) {
            rc = dir->ops->create(dir, leaf, &vn);
            dir->ref--;
        }
    }
    if (rc != 0)
        return rc;
    if ((flags & O_DIRECTORY) && !(vn->flags & VFS_VN_DIR)) {
        vn->ref--;
        return -ENOTDIR;
    }
    ft->files[fd].vn = vn;
    ft->files[fd].pos = 0;
    ft->files[fd].flags = flags;
    ft->files[fd].used = true;
    return fd;
}

int vfs_close(struct vfs_filetable *ft, int fd)
{
    if (!ft || fd < 0 || fd >= VFS_MAX_FDS || !ft->files[fd].used)
        return -EBADF;
    struct vfs_vnode *vn = ft->files[fd].vn;
    if (vn && vn->ref > 0)
        vn->ref--;
    memset(&ft->files[fd], 0, sizeof(ft->files[fd]));
    return 0;
}

int vfs_read(struct vfs_filetable *ft, int fd, void *buf, size_t len)
{
    if (!ft || fd < 0 || fd >= VFS_MAX_FDS || !ft->files[fd].used)
        return -EBADF;
    size_t n = 0;
    int rc = ft->files[fd].vn->ops->read(ft->files[fd].vn, buf, len, ft->files[fd].pos, &n);
    if (rc != 0)
        return rc;
    ft->files[fd].pos += n;
    return (int)n;
}

int vfs_write(struct vfs_filetable *ft, int fd, const void *buf, size_t len)
{
    if (!ft || fd < 0 || fd >= VFS_MAX_FDS || !ft->files[fd].used)
        return -EBADF;
    if ((ft->files[fd].flags & 3) == O_RDONLY)
        return -EBADF;
    size_t n = 0;
    int rc = ft->files[fd].vn->ops->write(ft->files[fd].vn, buf, len, ft->files[fd].pos, &n);
    if (rc != 0)
        return rc;
    ft->files[fd].pos += n;
    return (int)n;
}

int64_t vfs_lseek(struct vfs_filetable *ft, int fd, int64_t off, int whence)
{
    if (!ft || fd < 0 || fd >= VFS_MAX_FDS || !ft->files[fd].used)
        return -EBADF;
    struct vfs_stat st;
    ft->files[fd].vn->ops->stat(ft->files[fd].vn, &st);
    int64_t base = 0;
    if (whence == SEEK_CUR)
        base = (int64_t)ft->files[fd].pos;
    else if (whence == SEEK_END)
        base = (int64_t)st.size;
    else if (whence != SEEK_SET)
        return -EINVAL;
    if (base + off < 0)
        return -EINVAL;
    ft->files[fd].pos = (uint64_t)(base + off);
    return (int64_t)ft->files[fd].pos;
}

int vfs_readdir(struct vfs_filetable *ft, int fd, struct vfs_dirent *ent)
{
    if (!ft || fd < 0 || fd >= VFS_MAX_FDS || !ft->files[fd].used)
        return -EBADF;
    int rc = ft->files[fd].vn->ops->readdir(ft->files[fd].vn, ft->files[fd].pos, ent);
    if (rc == 0)
        ft->files[fd].pos++;
    return rc;
}

int vfs_stat(const char *path, struct vfs_stat *st)
{
    struct vfs_vnode *vn = NULL;
    int rc = walk(path, &vn);
    if (rc != 0)
        return rc;
    rc = vn->ops->stat(vn, st);
    vn->ref--;
    return rc;
}

int vfs_mkdir(const char *path)
{
    struct vfs_vnode *dir = NULL;
    char leaf[VFS_NAME_MAX];
    int rc = walk_parent(path, &dir, leaf);
    if (rc != 0)
        return rc;
    struct vfs_vnode *created = NULL;
    rc = dir->ops->mkdir(dir, leaf, &created);
    dir->ref--;
    return rc;
}

int vfs_unlink(const char *path)
{
    struct vfs_vnode *dir = NULL;
    char leaf[VFS_NAME_MAX];
    int rc = walk_parent(path, &dir, leaf);
    if (rc != 0)
        return rc;
    rc = dir->ops->unlink(dir, leaf);
    dir->ref--;
    return rc;
}

int vfs_dup2(struct vfs_filetable *ft, int oldfd, int newfd)
{
    if (!ft || oldfd < 0 || newfd < 0 || oldfd >= VFS_MAX_FDS || newfd >= VFS_MAX_FDS)
        return -EBADF;
    if (!ft->files[oldfd].used)
        return -EBADF;
    if (oldfd == newfd)
        return newfd;
    if (ft->files[newfd].used)
        vfs_close(ft, newfd);
    ft->files[newfd] = ft->files[oldfd];
    if (ft->files[newfd].vn)
        ft->files[newfd].vn->ref++;
    return newfd;
}

int vfs_blob_read(const struct vfs_blob *blob, uint64_t off, void *buf, size_t len, size_t *nout)
{
    if (!blob || off >= blob->size) {
        *nout = 0;
        return 0;
    }
    size_t n = blob->size - (size_t)off;
    if (n > len)
        n = len;
    memcpy(buf, blob->page[0] + off, n);
    *nout = n;
    return 0;
}

int vfs_blob_write(struct vfs_blob *blob, uint64_t off, const void *buf, size_t len, size_t *nout)
{
    (void)blob;
    (void)off;
    (void)buf;
    (void)len;
    *nout = 0;
    return -EROFS;
}

void vfs_blob_free(struct vfs_blob *blob)
{
    if (!blob)
        return;
    kfree(blob->page);
    kfree(blob);
}

static void put_octal(char *dst, size_t width, uint64_t val)
{
    for (size_t i = 0; i < width; i++)
        dst[i] = '0';
    dst[width - 1] = 0;
    for (int i = (int)width - 2; i >= 0; i--) {
        dst[i] = (char)('0' + (val & 7));
        val >>= 3;
    }
}

static void ustar_file(uint8_t *dst, const char *name, const void *data, size_t size)
{
    memset(dst, 0, 512);
    size_t n = 0;
    while (name[n] && n < 99) {
        dst[n] = (uint8_t)name[n];
        n++;
    }
    put_octal((char *)dst + 100, 8, 0644);
    put_octal((char *)dst + 108, 8, 0);
    put_octal((char *)dst + 116, 8, 0);
    put_octal((char *)dst + 124, 12, size);
    put_octal((char *)dst + 136, 12, 0);
    dst[156] = '0';
    memcpy(dst + 257, "ustar", 5);
    dst[262] = '0';
    dst[263] = '0';
    memset(dst + 148, ' ', 8);
    unsigned sum = 0;
    for (int i = 0; i < 512; i++)
        sum += dst[i];
    put_octal((char *)dst + 148, 7, sum);
    dst[154] = ' ';
    if (size)
        memcpy(dst + 512, data, size);
}

void vfs_self_test(void)
{
    uint8_t image[2048];
    memset(image, 0, sizeof(image));
    const char *hello = "hello-from-ustar";
    ustar_file(image, "hello", hello, 15);
    vfs_set_initramfs(image, sizeof(image));
    vfs_init();

    struct vfs_stat st;
    if (vfs_stat("/hello", &st) != 0 || st.size != 15)
        panic("vfs: stat /hello");
    if (vfs_stat("/./hello", &st) != 0)
        panic("vfs: dot path");
    if (vfs_stat("/hello/..", &st) != 0 || !(st.mode & VFS_S_IFDIR))
        panic("vfs: dotdot");
    if (vfs_stat("/missing", &st) != -ENOENT)
        panic("vfs: missing path");
    pr_info("test", "vfs: lookup ok");

    struct vfs_filetable ft;
    memset(&ft, 0, sizeof(ft));
    int fd = vfs_open(&ft, "/hello", O_RDONLY);
    if (fd < 0)
        panic("vfs: open hello");
    char buf[32];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(&ft, fd, buf, sizeof(buf));
    if (n != 15 || memcmp(buf, hello, 15) != 0)
        panic("vfs: read hello");
    n = vfs_read(&ft, fd, buf, sizeof(buf));
    if (n != 0)
        panic("vfs: read past eof");
    pr_info("test", "vfs: eof ok");
    if (vfs_close(&ft, fd) != 0 || vfs_close(&ft, fd) != -EBADF)
        panic("vfs: double close");
    pr_info("test", "vfs: double close");

    fd = vfs_open(&ft, "/tmp/note", O_CREAT | O_RDWR);
    if (fd < 0)
        panic("vfs: tmp create");
    if (vfs_write(&ft, fd, "abc", 3) != 3)
        panic("vfs: tmp write");
    if (vfs_lseek(&ft, fd, 0, SEEK_SET) != 0)
        panic("vfs: lseek");
    memset(buf, 0, sizeof(buf));
    if (vfs_read(&ft, fd, buf, 8) != 3 || memcmp(buf, "abc", 3) != 0)
        panic("vfs: tmp read");
    vfs_close(&ft, fd);
    if (vfs_unlink("/tmp/note") != 0 || vfs_stat("/tmp/note", &st) != -ENOENT)
        panic("vfs: unlink");
    pr_info("test", "vfs: tmpfs ok");

    int opened = 0;
    int fds[VFS_MAX_FDS];
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        int one = vfs_open(&ft, "/hello", O_RDONLY);
        if (one < 0)
            break;
        fds[opened++] = one;
    }
    if (opened == 0 || vfs_open(&ft, "/hello", O_RDONLY) != -EMFILE)
        panic("vfs: fd limit");
    for (int i = 0; i < opened; i++)
        vfs_close(&ft, fds[i]);
    pr_info("test", "vfs: fd limit");
    if (syscall_invoke(SYS_OPEN, 0xffffffff80000000ULL, O_RDONLY, 0, 0, 0, 0) != -EFAULT)
        panic("vfs: syscall accepted a kernel pointer");
    pr_info("test", "vfs: syscall bad pointer");
    pr_info("test", "vfs: all checks passed");
}
