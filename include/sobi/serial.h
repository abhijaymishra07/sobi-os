#ifndef SOBI_SERIAL_H
#define SOBI_SERIAL_H

#include <sobi/types.h>

#define SERIAL_COM1 0x3F8

bool serial_init(void);
void serial_putc(char c);
void serial_write(const char *s, size_t n);
void serial_puts(const char *s);
bool serial_rx_ready(void);
char serial_getc(void);

#endif
