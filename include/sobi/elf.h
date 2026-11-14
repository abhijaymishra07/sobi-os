#ifndef SOBI_ELF_H
#define SOBI_ELF_H

#include <sobi/types.h>
#include <sobi/proc.h>

int elf_validate(const void *image, size_t len);
int elf_spawn(const void *image, size_t len, struct process **out);
int elf_spawn_path(const char *path, struct process **out);
void elf_self_test(void);
void shell_self_test(void);
void multi_self_test(void);
struct pipe;
int elf_spawn_path_async(const char *path, struct pipe *in, struct pipe *out, struct process **out_proc);

#endif
