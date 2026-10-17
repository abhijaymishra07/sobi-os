#ifndef SOBI_VFS_H
#define SOBI_VFS_H

#include <sobi/types.h>

#define EPERM        1
#define ENOENT       2
#define EIO          5
#define EBADF        9
#define ENOMEM       12
#define EBUSY        16
#define EEXIST       17
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define EFBIG        27
#define ENOSPC       28
#define EROFS        30
#define ENAMETOOLONG 36
#define ENOTEMPTY    39

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0x40
#define O_DIRECTORY 0x10000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define VFS_MAX_FDS  16
#define VFS_NAME_MAX 100
#define VFS_PATH_MAX 256

#define VFS_VN_DIR    1u
#define VFS_VN_RO     2u
#define VFS_VN_ORPHAN 4u

#define VFS_S_IFREG 0x8000u
#define VFS_S_IFDIR 0x4000u

struct vfs_vnode;

struct vfs_dirent {
    char name[VFS_NAME_MAX];
    uint32_t ino;
    uint32_t mode;
};

struct vfs_stat {
    uint32_t mode;
    uint32_t ino;
    uint64_t size;
};

struct vfs_blob {
    uint8_t **page;
    uint32_t npages;
    size_t size;
};

struct vfs_vnode_ops {
    int (*lookup)(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out);
    int (*read)(struct vfs_vnode *vn, void *buf, size_t len, uint64_t off, size_t *nout);
    int (*write)(struct vfs_vnode *vn, const void *buf, size_t len, uint64_t off, size_t *nout);
    int (*readdir)(struct vfs_vnode *dir, uint64_t index, struct vfs_dirent *out);
    int (*stat)(struct vfs_vnode *vn, struct vfs_stat *st);
    int (*create)(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out);
    int (*unlink)(struct vfs_vnode *dir, const char *name);
    int (*mkdir)(struct vfs_vnode *dir, const char *name, struct vfs_vnode **out);
};

struct vfs_vnode {
    const struct vfs_vnode_ops *ops;
    void *data;
    uint32_t flags;
    int ref;
    void (*drop)(struct vfs_vnode *vn);
};

struct vfs_file {
    struct vfs_vnode *vn;
    uint64_t pos;
    int flags;
    bool used;
};

struct vfs_filetable {
    struct vfs_file files[VFS_MAX_FDS];
};

void vfs_set_initramfs(const void *data, size_t len);
void vfs_init(void);
struct vfs_filetable *vfs_table_new(void);
void vfs_self_test(void);

int vfs_open(struct vfs_filetable *ft, const char *path, int flags);
int vfs_close(struct vfs_filetable *ft, int fd);
int vfs_read(struct vfs_filetable *ft, int fd, void *buf, size_t len);
int vfs_write(struct vfs_filetable *ft, int fd, const void *buf, size_t len);
int64_t vfs_lseek(struct vfs_filetable *ft, int fd, int64_t off, int whence);
int vfs_readdir(struct vfs_filetable *ft, int fd, struct vfs_dirent *ent);
int vfs_stat(const char *path, struct vfs_stat *st);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_dup2(struct vfs_filetable *ft, int oldfd, int newfd);

struct vfs_vnode *tmpfs_new(void);
struct vfs_vnode *initramfs_load(const void *data, size_t len);

int vfs_blob_read(const struct vfs_blob *blob, uint64_t off, void *buf, size_t len, size_t *nout);
int vfs_blob_write(struct vfs_blob *blob, uint64_t off, const void *buf, size_t len, size_t *nout);
void vfs_blob_free(struct vfs_blob *blob);

#endif
