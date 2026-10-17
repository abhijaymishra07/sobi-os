#ifndef SOBI_KSYM_H
#define SOBI_KSYM_H

#include <sobi/types.h>

struct ksym {
    uint64_t addr;
    const char *name;
};

const char *ksym_lookup(uint64_t addr, uint64_t *offset);

#endif
