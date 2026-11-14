#include <sobi/console.h>
#include <sobi/irq.h>
#include <sobi/apic.h>
#include <sobi/io.h>
#include <sobi/cpu.h>

#define KBD_DATA 0x60
#define KBD_STAT 0x64
#define KBD_CAP  128
#define KBD_VEC  0x31

static const char set1[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

static uint8_t ring[KBD_CAP];
static uint32_t rhead;
static uint32_t rtail;

static void ring_push(uint8_t ch)
{
    uint32_t next = (rhead + 1) % KBD_CAP;
    if (next == rtail)
        return;
    ring[rhead] = ch;
    rhead = next;
}

static int ring_pop(void)
{
    int ch;
    if (rtail == rhead)
        return -1;
    ch = ring[rtail];
    rtail = (rtail + 1) % KBD_CAP;
    return ch;
}

static void irq_restore(uint64_t flags)
{
    if (flags & 0x200)
        cpu_sti();
}

void kbd_push_scancode(uint8_t scancode)
{
    uint64_t flags = cpu_read_flags();
    char ch;
    cpu_cli();
    if (scancode < 128) {
        ch = set1[scancode];
        if (ch)
            ring_push((uint8_t)ch);
    }
    irq_restore(flags);
}

int kbd_poll(void)
{
    uint64_t flags = cpu_read_flags();
    int ch;
    cpu_cli();
    ch = ring_pop();
    irq_restore(flags);
    return ch;
}

static void kbd_irq(struct int_frame *frame)
{
    (void)frame;
    if (inb(KBD_STAT) & 0x01)
        kbd_push_scancode(inb(KBD_DATA));
    apic_eoi();
}

void kbd_init(void)
{
    irq_register(KBD_VEC, kbd_irq);
    while (inb(KBD_STAT) & 0x01)
        (void)inb(KBD_DATA);
}
