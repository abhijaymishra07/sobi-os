#ifndef SOBI_BOOT_H
#define SOBI_BOOT_H

#include <sobi/types.h>
#include <limine.h>

struct boot_info {
    uint64_t base_revision;
    const char *bootloader_name;
    const char *bootloader_version;
    uint64_t hhdm_offset;
    paddr_t kernel_phys_base;
    vaddr_t kernel_virt_base;
    const struct limine_memmap_response *memmap;
    const char *cmdline;
};

extern const struct boot_info *boot_info;

void boot_collect_info(void);
void boot_dump_info(void);
const char *boot_memmap_type_name(uint64_t type);

extern char __kernel_start[], __kernel_end[];
extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[];
extern char __bss_start[], __bss_end[];

#endif
