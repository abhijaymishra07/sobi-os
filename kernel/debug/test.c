#ifdef SOBI_TEST

#include <sobi/test.h>
#include <sobi/io.h>
#include <sobi/cpu.h>
#include <sobi/log.h>
#include <sobi/string.h>
#include <sobi/panic.h>
#include <sobi/interrupt.h>
#include <sobi/ksym.h>

extern char exc_ud2_insn[], exc_ud2_resume[];
extern char exc_de_insn[], exc_de_div[], exc_de_resume[];
extern char exc_pf_insn[], exc_pf_load[], exc_pf_resume[];
extern char exc_gp_insn[], exc_gp_load[], exc_gp_resume[];
extern char exc_int3_insn[];
extern char exc_df_trigger[];

static void call_at(char *addr)
{
    ((void (*)(void))addr)();
}

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

static void fail(const char *msg)
{
    pr_err("test", "%s", msg);
    test_exit(TEST_EXIT_CODE_FAIL);
}

static void test_exc(void)
{
    uint64_t before = breakpoint_hits;
    call_at(exc_int3_insn);
    if (breakpoint_hits != before + 1)
        fail("int3 did not fire");
    pr_info("test", "exc: int3 ok");

    uint64_t off = 1;
    const char *sym = ksym_lookup((uint64_t)exc_ud2_insn, &off);
    if (!sym || strcmp(sym, "exc_ud2_insn") != 0 || off != 0)
        fail("symbol lookup mismatch");
    pr_info("test", "exc: ksym ok");

    fault_expect(VEC_UD, (uint64_t)exc_ud2_insn, (uint64_t)exc_ud2_resume);
    call_at(exc_ud2_insn);
    if (!fault_took(NULL))
        fail("ud2 was not caught");
    pr_info("test", "exc: ud2 ok");

    fault_expect(VEC_DE, (uint64_t)exc_de_div, (uint64_t)exc_de_resume);
    call_at(exc_de_insn);
    if (!fault_took(NULL))
        fail("divide error was not caught");
    pr_info("test", "exc: de ok");

    fault_expect(VEC_PF, (uint64_t)exc_pf_load, (uint64_t)exc_pf_resume);
    call_at(exc_pf_insn);
    uint64_t cr2 = 0;
    if (!fault_took(&cr2) || cr2 != 0x1000)
        fail("page fault cr2 mismatch");
    pr_info("test", "exc: pf ok cr2=%#lx", cr2);

    fault_expect(VEC_GP, (uint64_t)exc_gp_load, (uint64_t)exc_gp_resume);
    call_at(exc_gp_insn);
    if (!fault_took(NULL))
        fail("general protection was not caught");
    pr_info("test", "exc: gp ok");

    pr_info("test", "exc: all faults handled");
    test_exit(TEST_EXIT_CODE_PASS);
}

static void test_df(void)
{
    pr_info("test", "exc: triggering double fault");
    call_at(exc_df_trigger);
    fail("double fault returned");
}

void test_run(const char *cmdline)
{
    pr_info("test", "cmdline: \"%s\"", cmdline ? cmdline : "");

    if (cmdline_has(cmdline, "test=panic"))
        panic("deliberate test panic (value=%d)", 42);

    if (cmdline_has(cmdline, "test=exc"))
        test_exc();

    if (cmdline_has(cmdline, "test=df"))
        test_df();

    if (cmdline_has(cmdline, "test=boot") || !cmdline || !*cmdline) {
        pr_info("test", "boot test complete");
        test_exit(TEST_EXIT_CODE_PASS);
    }

    pr_err("test", "unknown test selector in cmdline");
    test_exit(TEST_EXIT_CODE_FAIL);
}

#endif
