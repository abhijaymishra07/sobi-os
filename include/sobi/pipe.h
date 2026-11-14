#ifndef SOBI_PIPE_H
#define SOBI_PIPE_H

#include <sobi/types.h>

#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef EPIPE
#define EPIPE 32
#endif

#define PIPE_END_READ  0
#define PIPE_END_WRITE 1
#define PIPE_SIZE      4096

struct pipe;

struct pipe *pipe_alloc(void);
int pipe_read(struct pipe *p, void *buf, size_t len);
int pipe_write(struct pipe *p, const void *buf, size_t len);
void pipe_close(struct pipe *p, int end);
void pipe_self_test(void);

#endif
