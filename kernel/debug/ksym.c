#include <sobi/ksym.h>
#include <sobi/compiler.h>

__attribute__((weak)) const struct ksym ksym_table[] = {
    { 0, 0 },
};
__attribute__((weak)) const size_t ksym_count = 0;

const char *ksym_lookup(uint64_t addr, uint64_t *offset)
{
    const struct ksym *best = NULL;
    for (size_t i = 0; i < ksym_count; i++) {
        if (ksym_table[i].addr == 0 || ksym_table[i].name == NULL)
            continue;
        if (ksym_table[i].addr <= addr)
            best = &ksym_table[i];
        else
            break;
    }
    if (!best)
        return NULL;
    if (offset)
        *offset = addr - best->addr;
    return best->name;
}
