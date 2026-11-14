#include "../libc/unistd.h"

int main(void)
{
    char buf[128];
    long n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++)
            if (buf[i] >= 'a' && buf[i] <= 'z')
                buf[i] = (char)(buf[i] - 'a' + 'A');
        write(1, buf, (unsigned long)n);
    }
    return n < 0 ? 1 : 0;
}
