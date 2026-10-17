#ifndef SOBI_VMM_H
#define SOBI_VMM_H

#include <sobi/types.h>

#define VMM_PRESENT (1ULL << 0)
#define VMM_WRITE   (1ULL << 1)
#define VMM_USER    (1ULL << 2)
#define VMM_PWT     (1ULL << 3)
#define VMM_PCD     (1ULL << 4)
#define VMM_GLOBAL  (1ULL << 8)
#define VMM_NX      (1ULL << 63)

#define VMM_MMIO_BASE  0xfffffe0000000000ULL
#define VMM_STACK_BASE 0xfffffe8000000000ULL
#define VMM_HEAP_BASE  0xffffff8000000000ULL
#define VMM_HEAP_LIMIT (VMM_HEAP_BASE + 64ULL * 1024 * 1024)

void vmm_init(void);
bool vmm_map(uint64_t virt, uint64_t phys, uint64_t pages, uint64_t flags);
bool vmm_unmap(uint64_t virt, uint64_t pages);
bool vmm_protect(uint64_t virt, uint64_t pages, uint64_t flags);
bool vmm_translate(uint64_t virt, uint64_t *phys, uint64_t *flags);
void *vmm_map_mmio(uint64_t phys, uint64_t size);
uint64_t vmm_alloc_stack(uint64_t pages);
bool vmm_nx_supported(void);
uint64_t vmm_kernel_cr3(void);

#endif
