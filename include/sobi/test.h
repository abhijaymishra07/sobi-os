#ifndef SOBI_TEST_H
#define SOBI_TEST_H

#include <sobi/types.h>
#include <sobi/compiler.h>

#define TEST_EXIT_PORT 0xf4
#define TEST_EXIT_CODE_PASS 0x10
#define TEST_EXIT_CODE_PANIC 0x11
#define TEST_EXIT_CODE_FAIL 0x12

#ifdef SOBI_TEST
void test_exit(uint8_t code) __noreturn;
void test_run(const char *cmdline) __noreturn;
#else
static inline void test_exit(uint8_t code)
{
    (void)code;
}
#endif

#endif
