#include <sobi/uaccess.h>
#include <sobi/proc.h>
#include <sobi/interrupt.h>
#include <sobi/string.h>

extern int copy_user_load(void *dst, const void *src, size_t n);
extern char copy_user_load_insn[], copy_user_load_fault[];
extern int copy_user_store(void *dst, const void *src, size_t n);
extern char copy_user_store_insn[], copy_user_store_fault[];

bool user_range_ok(const void *ptr, size_t n)
{
    uint64_t addr = (uint64_t)ptr;
    if (n == 0)
        return true;
    if (addr < 0x1000 || addr >= USER_LIMIT)
        return false;
    if (addr + n < addr || addr + n > USER_LIMIT)
        return false;
    return true;
}

int copy_from_user(void *dst, const void *src, size_t n)
{
    if (!dst || !user_range_ok(src, n))
        return -EFAULT;
    fault_expect(VEC_PF, (uint64_t)copy_user_load_insn, (uint64_t)copy_user_load_fault);
    int rc = copy_user_load(dst, src, n);
    fault_took(NULL);
    return rc;
}

int copy_to_user(void *dst, const void *src, size_t n)
{
    if (!src || !user_range_ok(dst, n))
        return -EFAULT;
    fault_expect(VEC_PF, (uint64_t)copy_user_store_insn, (uint64_t)copy_user_store_fault);
    int rc = copy_user_store(dst, src, n);
    fault_took(NULL);
    return rc;
}

int strncpy_from_user(char *dst, const void *src, size_t max)
{
    if (!dst || max == 0 || !user_range_ok(src, 1))
        return -EFAULT;
    for (size_t i = 0; i < max; i++) {
        char ch = 0;
        int rc = copy_from_user(&ch, (const char *)src + i, 1);
        if (rc != 0)
            return rc;
        dst[i] = ch;
        if (ch == 0)
            return (int)i;
    }
    dst[max - 1] = 0;
    return (int)max;
}
