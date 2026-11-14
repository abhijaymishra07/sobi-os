#include <sobi/console.h>
#include <sobi/vmm.h>
#include <sobi/kheap.h>
#include <sobi/string.h>
#include <sobi/panic.h>
#include <sobi/log.h>
#include <sobi/io.h>
#include <sobi/cpu.h>
#include <sobi/timer.h>

struct console_fb_boot console_fb_boot;

struct fb_state {
    uint8_t *base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t cols;
    uint32_t rows;
    uint32_t cx;
    uint32_t cy;
    int live;
};

static struct fb_state fb;

extern const uint8_t font8x16[95][16];

static uint8_t glyph_bits(unsigned ch, int row)
{
    if (row < 0 || row > 15)
        return 0;
    if (ch >= 0x20 && ch < 0x7F)
        return font8x16[ch - 0x20][row];
    if (row == 0 || row == 15)
        return 0xFF;
    return 0x81;
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    uint8_t *p;
    if (!fb.base || fb.bpp != 32 || x >= fb.width || y >= fb.height)
        return;
    p = fb.base + (size_t)y * fb.pitch + (size_t)x * 4;
    p[0] = (uint8_t)color;
    p[1] = (uint8_t)(color >> 8);
    p[2] = (uint8_t)(color >> 16);
    p[3] = (uint8_t)(color >> 24);
}

static void draw_glyph(uint32_t x, uint32_t y, unsigned ch, uint32_t color)
{
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph_bits(ch, row);
        for (int col = 0; col < 8; col++) {
            uint32_t px = x + (uint32_t)col;
            uint32_t py = y + (uint32_t)row;
            if (bits & (uint8_t)(0x80u >> col))
                put_pixel(px, py, color);
            else
                put_pixel(px, py, 0);
        }
    }
}

void fb_scroll(void)
{
    uint32_t row_bytes;
    uint32_t total;
    if (!fb.base || fb.height < 16)
        return;
    row_bytes = fb.pitch * 16;
    total = fb.pitch * fb.height;
    if (row_bytes >= total) {
        memset(fb.base, 0, total);
        return;
    }
    memmove(fb.base, fb.base + row_bytes, total - row_bytes);
    memset(fb.base + (total - row_bytes), 0, row_bytes);
}

void fb_attach(uint64_t phys, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp)
{
    void *mapped;
    uint64_t size;
    if (width == 0 || height == 0 || pitch == 0)
        return;
    size = (uint64_t)pitch * height;
    mapped = vmm_map_mmio(phys, size);
    if (!mapped)
        panic("fb: map");
    fb.base = mapped;
    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.bpp = bpp;
    fb.cols = width / 8;
    fb.rows = height / 16;
    fb.cx = 0;
    fb.cy = 0;
    fb.live = 1;
    memset(fb.base, 0, size);
}

void fb_putc(char ch)
{
    unsigned uc;
    if (!fb.base || fb.cols == 0 || fb.rows == 0)
        return;
    uc = (unsigned char)ch;
    if (uc == '\n') {
        fb.cx = 0;
        fb.cy++;
    } else if (uc == '\r') {
        fb.cx = 0;
    } else if (uc == '\b') {
        if (fb.cx > 0) {
            fb.cx--;
            draw_glyph(fb.cx * 8, fb.cy * 16, ' ', 0);
        }
    } else {
        draw_glyph(fb.cx * 8, fb.cy * 16, uc, 0x00FFFFFFu);
        fb.cx++;
        if (fb.cx >= fb.cols) {
            fb.cx = 0;
            fb.cy++;
        }
    }
    while (fb.cy >= fb.rows) {
        fb_scroll();
        fb.cy--;
    }
}

void fb_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        fb_putc(s[i]);
}

bool fb_live(void)
{
    return fb.live != 0;
}

void fb_puts(const char *s)
{
    if (!s)
        return;
    while (*s)
        fb_putc(*s++);
}

uint32_t fb_read_pixel(uint32_t x, uint32_t y)
{
    uint8_t *p;
    if (!fb.base || fb.bpp != 32 || x >= fb.width || y >= fb.height)
        return 0;
    p = fb.base + (size_t)y * fb.pitch + (size_t)x * 4;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool fb_self_test_pattern(void)
{
    struct fb_state saved;
    uint32_t *buf;
    uint32_t pitch = 64 * 4;
    uint32_t bytes = pitch * 32;
    int ok;
    buf = kmalloc(bytes);
    if (!buf)
        return false;
    memset(buf, 0, bytes);
    saved = fb;
    fb.base = (uint8_t *)buf;
    fb.width = 64;
    fb.height = 32;
    fb.pitch = pitch;
    fb.bpp = 32;
    fb.cols = 8;
    fb.rows = 2;
    fb.cx = 0;
    fb.cy = 0;
    fb.live = 0;
    fb_puts("A");
    ok = fb_read_pixel(0, 0) == 0 && fb_read_pixel(2, 3) == 0x00FFFFFFu && fb_read_pixel(0, 3) == 0;
    fb = saved;
    kfree(buf);
    return ok;
}

static uint32_t lit_pixels(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)
{
    uint32_t n = 0;
    for (uint32_t y = y0; y < y0 + h; y++)
        for (uint32_t x = x0; x < x0 + w; x++)
            if (fb_read_pixel(x, y))
                n++;
    return n;
}

void console_self_test(void)
{
    int ch;
    if (!fb_self_test_pattern())
        panic("console: glyph");
    pr_info("console", "glyph ok");

    if (!fb.live)
        panic("console: no framebuffer");
    uint32_t row = fb.cy;
    fb_puts("\nSOBI");
    if (lit_pixels(0, (row + 1) * 16, 32, 16) == 0)
        panic("console: framebuffer text not drawn");
    pr_info("console", "framebuffer %ux%u live", fb.width, fb.height);

    while (kbd_poll() >= 0)
        ;
    kbd_push_scancode(0x1E);
    ch = kbd_poll();
    if (ch != 'a')
        panic("console: key");
    pr_info("console", "key a");

    /* i8042 command 0xD2 queues a byte as if typed, raising a real IRQ 1. */
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xD2);
    while (inb(0x64) & 0x02)
        ;
    outb(0x60, 0x30);
    uint64_t deadline = uptime_ms() + 500;
    ch = -1;
    while (ch < 0 && uptime_ms() < deadline) {
        cpu_hlt();
        ch = kbd_poll();
    }
    if (ch != 'b')
        panic("console: keyboard irq (got %d)", ch);
    pr_info("console", "keyboard irq b");
    pr_info("console", "all checks passed");
}
