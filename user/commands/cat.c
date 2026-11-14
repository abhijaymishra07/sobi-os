#include "../libc/unistd.h"

int main(void)
{
    long fd = open("/etc/motd", 0);
    if (fd < 0) {
        write(2, "cat: /etc/motd not found\n", 25);
        return 1;
    }
    char buf[128];
    long n;
    while ((n = read((int)fd, buf, sizeof(buf))) > 0)
        write(1, buf, (unsigned long)n);
    close((int)fd);
    return 0;
}
