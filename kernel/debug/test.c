#ifdef SOBI_TEST

#include <sobi/test.h>
#include <sobi/io.h>
#include <sobi/cpu.h>
#include <sobi/log.h>
#include <sobi/string.h>
#include <sobi/panic.h>

void test_exit(uint8_t code)
{
    kprintf("test: exit %u\n", code);
    outb(TEST_EXIT_PORT, code);
    cpu_halt_forever();
}

static bool cmdline_has(const char *cmdline, const char *key)
{
    if (!cmdline)
        return false;
    size_t klen = strlen(key);
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ')
            p++;
        const char *start = p;
        while (*p && *p != ' ')
            p++;
        if ((size_t)(p - start) == klen && memcmp(start, key, klen) == 0)
            return true;
    }
    return false;
}

void test_run(const char *cmdline)
{
    pr_info("test", "cmdline: \"%s\"", cmdline ? cmdline : "");

    if (cmdline_has(cmdline, "test=panic"))
        panic("deliberate test panic (value=%d)", 42);

    if (cmdline_has(cmdline, "test=boot") || !cmdline || !*cmdline) {
        pr_info("test", "boot test complete");
        test_exit(TEST_EXIT_CODE_PASS);
    }

    pr_err("test", "unknown test selector in cmdline");
    test_exit(TEST_EXIT_CODE_FAIL);
}

#endif
