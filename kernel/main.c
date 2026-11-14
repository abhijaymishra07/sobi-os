#include <sobi/types.h>
#include <sobi/compiler.h>
#include <sobi/serial.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/string.h>
#include <sobi/boot.h>
#include <sobi/cpu.h>
#include <sobi/interrupt.h>
#include <sobi/pmm.h>
#include <sobi/vmm.h>
#include <sobi/kheap.h>
#include <sobi/timer.h>
#include <sobi/sched.h>
#include <sobi/proc.h>
#include <sobi/syscall.h>
#include <sobi/vfs.h>
#include <sobi/elf.h>
#include <sobi/test.h>
#include <sobi/console.h>
#include <limine.h>

extern volatile struct limine_framebuffer_request limine_fb_req;

#define SOBI_VERSION "0.11.0"

void kmain(void) __noreturn;

static void print_banner(void)
{
    kprintf("\n");
    kprintf("  ____   ___  ____ ___    ___  ____\n");
    kprintf(" / ___| / _ \\| __ )_ _|  / _ \\/ ___|\n");
    kprintf(" \\___ \\| | | |  _ \\| |  | | | \\___ \\\n");
    kprintf("  ___) | |_| | |_) | |  | |_| |___) |\n");
    kprintf(" |____/ \\___/|____/___|  \\___/|____/\n");
    kprintf("\n");
    kprintf("SOBI OS %s (x86-64, %s build)\n\n", SOBI_VERSION, SOBI_BUILD_NAME);
}

static void console_init(void)
{
    struct limine_framebuffer_response *r = limine_fb_req.response;
    if (r && r->framebuffer_count > 0) {
        struct limine_framebuffer *f = r->framebuffers[0];
        console_fb_boot.phys = (uint64_t)f->address - boot_info->hhdm_offset;
        console_fb_boot.width = (uint32_t)f->width;
        console_fb_boot.height = (uint32_t)f->height;
        console_fb_boot.pitch = (uint32_t)f->pitch;
        console_fb_boot.bpp = f->bpp;
        console_fb_boot.present = 1;
        fb_attach(console_fb_boot.phys, console_fb_boot.width, console_fb_boot.height,
                  console_fb_boot.pitch, console_fb_boot.bpp);
        pr_info("console", "framebuffer %ux%u bpp %u", console_fb_boot.width,
                console_fb_boot.height, console_fb_boot.bpp);
    }
    ioapic_init(0);
    kbd_init();
}

void kmain(void)
{
    serial_init();
    print_banner();

    boot_collect_info();
    boot_dump_info();
    arch_init();
    pmm_init(boot_info);
    if (boot_info->module && boot_info->module_size) {
        uint64_t pages = (boot_info->module_size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t frame = pmm_alloc_contig(pages, PAGE_SIZE);
        if (frame) {
            memcpy((void *)(boot_info->hhdm_offset + frame), boot_info->module, (size_t)boot_info->module_size);
            vfs_set_initramfs((void *)(boot_info->hhdm_offset + frame), (size_t)boot_info->module_size);
            pr_info("boot", "module %lu bytes", boot_info->module_size);
        }
    }
    vmm_init();
    kheap_init();
    timer_init();
    console_init();
    sched_init();
    proc_init();
    syscall_init();
    vfs_init();

    pr_info("boot", "boot: ok");

#ifdef SOBI_TEST
    test_run(boot_info->cmdline);
#else
    if (elf_spawn_path_async("/bin/init", NULL, NULL, NULL) < 0) {
        pr_info("boot", "no /bin/init, halting");
        cpu_halt_forever();
    }
    for (;;)
        sched_sleep_ms(1000);
#endif
}
