#ifndef SOBI_ELF_H
#define SOBI_ELF_H

#include <sobi/types.h>
#include <sobi/proc.h>

int elf_validate(const void *image, size_t len);
int elf_spawn(const void *image, size_t len, struct process **out);
void elf_self_test(void);

#endif
