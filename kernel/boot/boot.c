#include <sobi/boot.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/assert.h>
#include <sobi/string.h>

extern volatile uint64_t limine_base_revision[];
extern volatile struct limine_bootloader_info_request limine_bootloader_info_req;
extern volatile struct limine_executable_cmdline_request limine_cmdline_req;
extern volatile struct limine_hhdm_request limine_hhdm_req;
extern volatile struct limine_memmap_request limine_memmap_req;
extern volatile struct limine_executable_address_request limine_exec_addr_req;
extern volatile struct limine_stack_size_request limine_stack_size_req;

static struct boot_info info;
const struct boot_info *boot_info = &info;

static const char *const memmap_type_names[] = {
    [LIMINE_MEMMAP_USABLE] = "usable",
    [LIMINE_MEMMAP_RESERVED] = "reserved",
    [LIMINE_MEMMAP_ACPI_RECLAIMABLE] = "acpi-reclaimable",
    [LIMINE_MEMMAP_ACPI_NVS] = "acpi-nvs",
    [LIMINE_MEMMAP_BAD_MEMORY] = "bad",
    [LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] = "bootloader-reclaimable",
    [LIMINE_MEMMAP_EXECUTABLE_AND_MODULES] = "executable+modules",
    [LIMINE_MEMMAP_FRAMEBUFFER] = "framebuffer",
    [LIMINE_MEMMAP_RESERVED_MAPPED] = "reserved-mapped",
};

const char *boot_memmap_type_name(uint64_t type)
{
    if (type < ARRAY_SIZE(memmap_type_names) && memmap_type_names[type])
        return memmap_type_names[type];
    return "unknown";
}

void boot_collect_info(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        if (LIMINE_LOADED_BASE_REVISION_VALID(limine_base_revision))
            panic("limine base revision 6 not supported, bootloader used revision %lu",
                  LIMINE_LOADED_BASE_REVISION(limine_base_revision));
        panic("limine base revision 6 not supported and no revision reported");
    }
    info.base_revision = 6;

    if (limine_bootloader_info_req.response) {
        info.bootloader_name = limine_bootloader_info_req.response->name;
        info.bootloader_version = limine_bootloader_info_req.response->version;
    } else {
        info.bootloader_name = "unknown";
        info.bootloader_version = "unknown";
    }

    if (!limine_hhdm_req.response)
        panic("limine did not provide an HHDM response");
    info.hhdm_offset = limine_hhdm_req.response->offset;

    if (!limine_memmap_req.response)
        panic("limine did not provide a memory map response");
    info.memmap = limine_memmap_req.response;
    if (info.memmap->entry_count == 0)
        panic("limine memory map is empty");

    if (!limine_exec_addr_req.response)
        panic("limine did not provide an executable address response");
    info.kernel_phys_base = limine_exec_addr_req.response->physical_base;
    info.kernel_virt_base = limine_exec_addr_req.response->virtual_base;

    if (limine_cmdline_req.response && limine_cmdline_req.response->cmdline)
        info.cmdline = limine_cmdline_req.response->cmdline;
    else
        info.cmdline = "";

    ASSERT(info.kernel_virt_base == (vaddr_t)__kernel_start);
    ASSERT(info.hhdm_offset >= 0xffff800000000000ULL);
    ASSERT(IS_ALIGNED(info.kernel_phys_base, PAGE_SIZE));
}

void boot_dump_info(void)
{
    pr_info("boot", "bootloader: %s %s (limine protocol base revision %lu)",
            info.bootloader_name, info.bootloader_version, info.base_revision);
    pr_info("boot", "stack size request %s",
            limine_stack_size_req.response ? "honoured" : "ignored");
    pr_info("boot", "cmdline: \"%s\"", info.cmdline);
    pr_info("boot", "hhdm offset: %#lx", info.hhdm_offset);
    pr_info("boot", "kernel phys %#lx virt %#lx size %lu KiB",
            info.kernel_phys_base, info.kernel_virt_base,
            (uint64_t)(__kernel_end - __kernel_start) / 1024);
    pr_info("boot", "  .text   %p-%p", (void *)__text_start, (void *)__text_end);
    pr_info("boot", "  .rodata %p-%p", (void *)__rodata_start, (void *)__rodata_end);
    pr_info("boot", "  .data   %p-%p", (void *)__data_start, (void *)__data_end);
    pr_info("boot", "  .bss    %p-%p", (void *)__bss_start, (void *)__bss_end);

    uint64_t usable = 0, reclaimable = 0, kernel = 0;
    pr_info("boot", "memory map (%lu entries):", info.memmap->entry_count);
    for (uint64_t i = 0; i < info.memmap->entry_count; i++) {
        const struct limine_memmap_entry *e = info.memmap->entries[i];
        pr_info("boot", "  %#018lx-%#018lx %10lu KiB %s", e->base,
                e->base + e->length, e->length / 1024, boot_memmap_type_name(e->type));
        if (e->type == LIMINE_MEMMAP_USABLE)
            usable += e->length;
        else if (e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
            reclaimable += e->length;
        else if (e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES)
            kernel += e->length;
    }
    pr_info("boot", "memory: %lu KiB usable, %lu KiB bootloader-reclaimable, %lu KiB executable+modules",
            usable / KiB(1), reclaimable / KiB(1), kernel / KiB(1));
}

#define OWNED_REGIONS 256
#define OWNED_STR 128

static struct limine_memmap_entry owned_entries[OWNED_REGIONS];
static struct limine_memmap_entry *owned_ptrs[OWNED_REGIONS];
static struct limine_memmap_response owned_map;
static char owned_name[OWNED_STR];
static char owned_version[OWNED_STR];
static char owned_cmdline[512];

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    size_t i = 0;
    if (!src)
        src = "";
    while (src[i] && i + 1 < dstsz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void boot_own_firmware_data(void)
{
    const struct limine_memmap_response *src = info.memmap;
    if (src->entry_count > OWNED_REGIONS)
        panic("boot: %lu memmap entries exceeds %d", src->entry_count, OWNED_REGIONS);
    for (uint64_t i = 0; i < src->entry_count; i++) {
        owned_entries[i] = *src->entries[i];
        owned_ptrs[i] = &owned_entries[i];
    }
    owned_map.revision = src->revision;
    owned_map.entry_count = src->entry_count;
    owned_map.entries = owned_ptrs;
    info.memmap = &owned_map;

    copy_str(owned_name, sizeof(owned_name), info.bootloader_name);
    copy_str(owned_version, sizeof(owned_version), info.bootloader_version);
    copy_str(owned_cmdline, sizeof(owned_cmdline), info.cmdline);
    info.bootloader_name = owned_name;
    info.bootloader_version = owned_version;
    info.cmdline = owned_cmdline;
}
