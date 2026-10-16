#ifndef SOBI_COMPILER_H
#define SOBI_COMPILER_H

#define __used __attribute__((used))
#define __unused __attribute__((unused))
#define __noreturn __attribute__((noreturn))
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __section(s) __attribute__((section(s)))
#define __printf(fmt, args) __attribute__((format(printf, fmt, args)))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define barrier() __asm__ volatile("" ::: "memory")

#endif
