#ifndef SOBI_ASSERT_H
#define SOBI_ASSERT_H

#include <sobi/panic.h>

#define ASSERT(cond)                                                      \
    do {                                                                  \
        if (unlikely(!(cond)))                                            \
            panic("assertion failed: %s", #cond);                         \
    } while (0)

#define ASSERT_MSG(cond, ...)                                             \
    do {                                                                  \
        if (unlikely(!(cond)))                                            \
            panic(__VA_ARGS__);                                           \
    } while (0)

#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

#endif
