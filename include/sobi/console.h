#ifndef SOBI_CONSOLE_H
#define SOBI_CONSOLE_H

#include <sobi/types.h>

struct console_fb_boot {
    uint64_t phys;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    int present;
};

extern struct console_fb_boot console_fb_boot;

void ioapic_init(uint64_t mmio_phys);
void ioapic_route(uint32_t irq, uint32_t vector);
void ioapic_mask(uint32_t irq);

void kbd_init(void);
int kbd_poll(void);
uint64_t kbd_irq_count(void);
void kbd_push_scancode(uint8_t scancode);

void fb_attach(uint64_t phys, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp);
void fb_putc(char ch);
void fb_puts(const char *s);
void fb_write(const char *s, size_t n);
bool fb_live(void);
void fb_scroll(void);
uint32_t fb_read_pixel(uint32_t x, uint32_t y);
bool fb_self_test_pattern(void);
void console_self_test(void);

#endif
