#include <sobi/serial.h>
#include <sobi/io.h>

#define REG_DATA 0
#define REG_IER 1
#define REG_FCR 2
#define REG_LCR 3
#define REG_MCR 4
#define REG_LSR 5
#define REG_DLL 0
#define REG_DLM 1

#define LCR_DLAB 0x80
#define LCR_8N1 0x03
#define LSR_DATA_READY 0x01
#define LSR_THR_EMPTY 0x20

static const uint16_t base = SERIAL_COM1;
static bool present;

bool serial_init(void)
{
    outb(base + REG_IER, 0x00);
    outb(base + REG_LCR, LCR_DLAB);
    outb(base + REG_DLL, 0x01);
    outb(base + REG_DLM, 0x00);
    outb(base + REG_LCR, LCR_8N1);
    outb(base + REG_FCR, 0xC7);
    outb(base + REG_MCR, 0x1E);
    outb(base + REG_DATA, 0xAE);
    if (inb(base + REG_DATA) != 0xAE) {
        present = false;
        return false;
    }
    outb(base + REG_MCR, 0x0F);
    present = true;
    return true;
}

static inline void wait_tx(void)
{
    while ((inb(base + REG_LSR) & LSR_THR_EMPTY) == 0)
        ;
}

void serial_putc(char c)
{
    if (!present)
        return;
    if (c == '\n') {
        wait_tx();
        outb(base + REG_DATA, '\r');
    }
    wait_tx();
    outb(base + REG_DATA, (uint8_t)c);
}

void serial_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        serial_putc(s[i]);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

bool serial_rx_ready(void)
{
    return present && (inb(base + REG_LSR) & LSR_DATA_READY) != 0;
}

char serial_getc(void)
{
    while (!serial_rx_ready())
        ;
    return (char)inb(base + REG_DATA);
}
