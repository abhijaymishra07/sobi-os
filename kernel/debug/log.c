#include <sobi/log.h>
#include <sobi/serial.h>
#include <sobi/string.h>

static enum log_level current_level = LOG_DEBUG;

struct out {
    char *buf;
    size_t size;
    size_t pos;
};

static void out_char(struct out *o, char c)
{
    if (o->pos + 1 < o->size)
        o->buf[o->pos] = c;
    o->pos++;
}

static void out_str(struct out *o, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out_char(o, s[i]);
}

static void out_pad(struct out *o, char c, int n)
{
    while (n-- > 0)
        out_char(o, c);
}

enum {
    F_LEFT = 1 << 0,
    F_ZERO = 1 << 1,
    F_ALT = 1 << 2,
    F_PLUS = 1 << 3,
    F_SPACE = 1 << 4,
};

static void fmt_number(struct out *o, uint64_t val, bool negative, unsigned base,
                       bool upper, int flags, int width, int precision)
{
    char tmp[24];
    int len = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0 && precision != 0)
        tmp[len++] = '0';
    while (val) {
        tmp[len++] = digits[val % base];
        val /= base;
    }

    int zeros = 0;
    if (precision > len)
        zeros = precision - len;

    char sign = 0;
    if (negative)
        sign = '-';
    else if (flags & F_PLUS)
        sign = '+';
    else if (flags & F_SPACE)
        sign = ' ';

    int prefix_len = 0;
    if ((flags & F_ALT) && base == 16 && len > 0)
        prefix_len = 2;

    int total = len + zeros + prefix_len + (sign ? 1 : 0);
    int pad = width > total ? width - total : 0;

    if (!(flags & F_LEFT) && !(flags & F_ZERO))
        out_pad(o, ' ', pad);
    if (sign)
        out_char(o, sign);
    if (prefix_len) {
        out_char(o, '0');
        out_char(o, upper ? 'X' : 'x');
    }
    if (!(flags & F_LEFT) && (flags & F_ZERO) && precision < 0)
        out_pad(o, '0', pad);
    out_pad(o, '0', zeros);
    while (len--)
        out_char(o, tmp[len]);
    if (flags & F_LEFT)
        out_pad(o, ' ', pad);
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o = { .buf = buf, .size = size, .pos = 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_char(&o, *fmt);
            continue;
        }
        fmt++;

        int flags = 0;
        for (;; fmt++) {
            if (*fmt == '-')
                flags |= F_LEFT;
            else if (*fmt == '0')
                flags |= F_ZERO;
            else if (*fmt == '#')
                flags |= F_ALT;
            else if (*fmt == '+')
                flags |= F_PLUS;
            else if (*fmt == ' ')
                flags |= F_SPACE;
            else
                break;
        }

        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= F_LEFT;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    precision = precision * 10 + (*fmt++ - '0');
            }
        }

        int longs = 0;
        bool size_t_mod = false;
        for (;; fmt++) {
            if (*fmt == 'l')
                longs++;
            else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't')
                size_t_mod = true;
            else if (*fmt == 'h')
                ;
            else
                break;
        }

        switch (*fmt) {
        case '%':
            out_char(&o, '%');
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!(flags & F_LEFT))
                out_pad(&o, ' ', width - 1);
            out_char(&o, c);
            if (flags & F_LEFT)
                out_pad(&o, ' ', width - 1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            size_t n = precision >= 0 ? strnlen(s, (size_t)precision) : strlen(s);
            int pad = width > (int)n ? width - (int)n : 0;
            if (!(flags & F_LEFT))
                out_pad(&o, ' ', pad);
            out_str(&o, s, n);
            if (flags & F_LEFT)
                out_pad(&o, ' ', pad);
            break;
        }
        case 'd':
        case 'i': {
            int64_t v;
            if (longs || size_t_mod)
                v = va_arg(ap, int64_t);
            else
                v = va_arg(ap, int);
            bool neg = v < 0;
            uint64_t mag = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
            fmt_number(&o, mag, neg, 10, false, flags, width, precision);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            uint64_t v;
            if (longs || size_t_mod)
                v = va_arg(ap, uint64_t);
            else
                v = va_arg(ap, unsigned int);
            unsigned base = *fmt == 'u' ? 10 : (*fmt == 'o' ? 8 : 16);
            fmt_number(&o, v, false, base, *fmt == 'X', flags, width, precision);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
            fmt_number(&o, v, false, 16, false, F_ALT | F_ZERO, 18, -1);
            break;
        }
        case '\0':
            fmt--;
            break;
        default:
            out_char(&o, '%');
            out_char(&o, *fmt);
            break;
        }
    }

    if (size > 0)
        buf[o.pos < size ? o.pos : size - 1] = '\0';
    return (int)o.pos;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int kvprintf(const char *fmt, va_list ap)
{
    char buf[512];
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    size_t len = n < (int)sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    serial_write(buf, len);
    return n;
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvprintf(fmt, ap);
    va_end(ap);
    return n;
}

void log_set_level(enum log_level level)
{
    current_level = level;
}

static const char *const level_names[] = {
    [LOG_DEBUG] = "debug",
    [LOG_INFO] = "info ",
    [LOG_WARN] = "warn ",
    [LOG_ERROR] = "error",
    [LOG_FATAL] = "fatal",
};

void klog(enum log_level level, const char *subsys, const char *fmt, ...)
{
    if (level < current_level)
        return;
    kprintf("[%s] %s: ", level_names[level], subsys);
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    serial_putc('\n');
}
