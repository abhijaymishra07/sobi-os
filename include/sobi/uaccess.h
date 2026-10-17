#ifndef SOBI_UACCESS_H
#define SOBI_UACCESS_H

#include <sobi/types.h>

#define EFAULT 14
#define EINVAL 22
#define ECHILD 10
#define ENOENT 2
#define EBADF 9
#define ENOSYS 38
#define ENOEXEC 8
#define EMFILE 24
#define EPIPE 32
#define EAGAIN 11

bool user_range_ok(const void *ptr, size_t n);
int copy_from_user(void *dst, const void *src, size_t n);
int copy_to_user(void *dst, const void *src, size_t n);
int strncpy_from_user(char *dst, const void *src, size_t max);

#endif
