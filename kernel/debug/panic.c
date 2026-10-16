#include <sobi/panic.h>
#include <sobi/log.h>
#include <sobi/cpu.h>
#include <sobi/serial.h>
#include <sobi/test.h>

static volatile int panic_depth;

void panic_at(const char *file, int line, const char *func, const char *fmt, ...)
{
    cpu_cli();

    if (++panic_depth > 1) {
        serial_puts("\n!!! nested panic, halting\n");
        cpu_halt_forever();
    }

    kprintf("\n!!! KERNEL PANIC !!!\n");
    kprintf("at %s:%d in %s()\n", file, line, func);
    kprintf("reason: ");
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");
    kprintf("rsp=%#018lx rflags=%#018lx cr2=%#018lx cr3=%#018lx\n",
            cpu_read_rsp(), cpu_read_flags(), cpu_read_cr2(), cpu_read_cr3());
    kprintf("system halted\n");

    test_exit(TEST_EXIT_CODE_PANIC);
    cpu_halt_forever();
}
