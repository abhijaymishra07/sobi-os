#ifndef SOBI_TYPES_H
#define SOBI_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;

#define KiB(x) ((uint64_t)(x) << 10)
#define MiB(x) ((uint64_t)(x) << 20)
#define GiB(x) ((uint64_t)(x) << 30)

#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)

#endif
