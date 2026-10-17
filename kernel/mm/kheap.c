#include <sobi/kheap.h>
#include <sobi/vmm.h>
#include <sobi/pmm.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/panic.h>

#define SLAB_MAGIC 0x51ab51ab51ab51abULL
#define REDZONE 0xa5
#define NCLASS 8

static const uint16_t class_size[NCLASS] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };

struct slab {
    uint64_t magic;
    uint16_t class_index;
    uint16_t class_sz;
    uint16_t in_use;
    uint16_t capacity;
    struct slab *next;
    uint64_t bitmap[4];
    uint16_t req[220];
    uint8_t data[];
};

struct big {
    uint8_t *ptr;
    uint64_t phys[16];
    uint64_t pages;
    uint64_t size;
    bool used;
};

static struct slab *slabs[NCLASS];
static struct big bigs[64];
static uint64_t heap_next = VMM_HEAP_BASE;
static struct kheap_stats stats;

static int class_for(size_t size)
{
    for (int i = 0; i < NCLASS; i++) {
        if (size <= class_size[i])
            return i;
    }
    return -1;
}

static bool bit_on(const struct slab *s, uint32_t i)
{
    return (s->bitmap[i >> 6] & (1ULL << (i & 63))) != 0;
}

static void bit_set(struct slab *s, uint32_t i)
{
    s->bitmap[i >> 6] |= 1ULL << (i & 63);
}

static void bit_clr(struct slab *s, uint32_t i)
{
    s->bitmap[i >> 6] &= ~(1ULL << (i & 63));
}

static struct slab *new_slab(int cls)
{
    if (heap_next + PAGE_SIZE > VMM_HEAP_LIMIT)
        return NULL;
    uint64_t frame = pmm_alloc();
    if (frame == 0)
        return NULL;
    uint64_t virt = heap_next;
    heap_next += PAGE_SIZE;
    if (!vmm_map(virt, frame, 1, VMM_WRITE | VMM_NX))
        return NULL;
    struct slab *s = (struct slab *)virt;
    memset(s, 0, sizeof(*s));
    s->magic = SLAB_MAGIC;
    s->class_index = (uint16_t)cls;
    s->class_sz = class_size[cls];
    uint64_t data = ALIGN_UP(virt + offsetof(struct slab, data), 16);
    s->capacity = (uint16_t)((virt + PAGE_SIZE - data) / s->class_sz);
    if (s->capacity > 220)
        s->capacity = 220;
    s->next = slabs[cls];
    slabs[cls] = s;
    stats.slabs++;
    return s;
}

static void *slot_ptr(struct slab *s, uint32_t index)
{
    uint64_t data = ALIGN_UP((uint64_t)s + offsetof(struct slab, data), 16);
    return (void *)(data + (uint64_t)index * s->class_sz);
}

static bool find_slot(void *ptr, struct slab **out_s, uint32_t *out_i)
{
    for (int c = 0; c < NCLASS; c++) {
        for (struct slab *s = slabs[c]; s; s = s->next) {
            uint64_t data = ALIGN_UP((uint64_t)s + offsetof(struct slab, data), 16);
            uint64_t addr = (uint64_t)ptr;
            if (addr < data)
                continue;
            uint64_t off = addr - data;
            if (off % s->class_sz)
                continue;
            uint32_t index = (uint32_t)(off / s->class_sz);
            if (index >= s->capacity)
                continue;
            if (slot_ptr(s, index) != ptr)
                continue;
            *out_s = s;
            *out_i = index;
            return true;
        }
    }
    return false;
}

static void *alloc_big(size_t size)
{
    uint64_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    if (pages == 0 || pages > 16)
        return NULL;
    struct big *b = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(bigs); i++) {
        if (!bigs[i].used) {
            b = &bigs[i];
            break;
        }
    }
    if (!b)
        return NULL;
    if (heap_next + pages * PAGE_SIZE > VMM_HEAP_LIMIT)
        return NULL;
    uint64_t virt = heap_next;
    heap_next += pages * PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t frame = pmm_alloc();
        if (frame == 0)
            return NULL;
        if (!vmm_map(virt + i * PAGE_SIZE, frame, 1, VMM_WRITE | VMM_NX))
            return NULL;
        b->phys[i] = frame;
    }
    b->ptr = (uint8_t *)virt;
    b->pages = pages;
    b->size = size;
    b->used = true;
    stats.big_live += size;
    stats.live_bytes += size;
    stats.allocs++;
    return b->ptr;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;
    int cls = class_for(size);
    if (cls < 0)
        return alloc_big(size);

    struct slab *s = NULL;
    for (struct slab *it = slabs[cls]; it; it = it->next) {
        if (it->in_use < it->capacity) {
            s = it;
            break;
        }
    }
    if (!s)
        s = new_slab(cls);
    if (!s)
        return NULL;

    for (uint32_t i = 0; i < s->capacity; i++) {
        if (bit_on(s, i))
            continue;
        bit_set(s, i);
        s->in_use++;
        s->req[i] = (uint16_t)size;
        uint8_t *p = slot_ptr(s, i);
        if (size < s->class_sz)
            p[size] = REDZONE;
        stats.live_bytes += size;
        stats.allocs++;
        return p;
    }
    return NULL;
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

static void free_big(struct big *b)
{
    for (uint64_t i = 0; i < b->pages; i++) {
        vmm_unmap((uint64_t)b->ptr + i * PAGE_SIZE, 1);
        pmm_free(b->phys[i]);
    }
    stats.big_live -= b->size;
    stats.live_bytes -= b->size;
    stats.frees++;
    memset(b, 0, sizeof(*b));
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    for (size_t i = 0; i < ARRAY_SIZE(bigs); i++) {
        if (bigs[i].used && bigs[i].ptr == ptr) {
            free_big(&bigs[i]);
            return;
        }
    }
    struct slab *s = NULL;
    uint32_t index = 0;
    if (!find_slot(ptr, &s, &index) || s->magic != SLAB_MAGIC)
        panic("kheap: bad pointer %p", ptr);
    if (!bit_on(s, index))
        panic("kheap: double free %p", ptr);
    if (s->req[index] < s->class_sz) {
        uint8_t *p = ptr;
        if (p[s->req[index]] != REDZONE)
            panic("kheap: redzone %p", ptr);
    }
    stats.live_bytes -= s->req[index];
    stats.frees++;
    bit_clr(s, index);
    s->in_use--;
    s->req[index] = 0;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    size_t old = 0;
    for (size_t i = 0; i < ARRAY_SIZE(bigs); i++) {
        if (bigs[i].used && bigs[i].ptr == ptr)
            old = bigs[i].size;
    }
    if (old == 0) {
        struct slab *s = NULL;
        uint32_t index = 0;
        if (!find_slot(ptr, &s, &index))
            panic("kheap: bad pointer %p", ptr);
        old = s->req[index];
    }
    void *n = kmalloc(size);
    if (!n)
        return NULL;
    memcpy(n, ptr, old < size ? old : size);
    kfree(ptr);
    return n;
}

struct kheap_stats kheap_stats(void)
{
    return stats;
}

void kheap_init(void)
{
    pr_info("kheap", "heap at %#lx, %d slab classes", (uint64_t)VMM_HEAP_BASE, NCLASS);
}
