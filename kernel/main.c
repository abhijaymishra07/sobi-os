#include <sobi/types.h>
#include <sobi/compiler.h>
#include <sobi/serial.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/boot.h>
#include <sobi/cpu.h>
#include <sobi/interrupt.h>
#include <sobi/pmm.h>
#include <sobi/vmm.h>
#include <sobi/kheap.h>
#include <sobi/test.h>

#define SOBI_VERSION "0.4.0"

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

void kmain(void)
{
    serial_init();
    print_banner();

    boot_collect_info();
    boot_dump_info();
    arch_init();
    pmm_init(boot_info);
    vmm_init();
    kheap_init();

    pr_info("boot", "boot: ok");

#ifdef SOBI_TEST
    test_run(boot_info->cmdline);
#else
    pr_info("boot", "nothing more to do, halting");
    cpu_halt_forever();
#endif
}
