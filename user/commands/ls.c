#include "../libc/unistd.h"

static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static int list(const char *dir)
{
    long fd = open(dir, 0);
    if (fd < 0) {
        write(2, "ls: cannot open\n", 16);
        return 1;
    }
    write(1, dir, (unsigned long)slen(dir));
    write(1, ":\n", 2);
    struct dirent ent;
    while (readdir((int)fd, &ent) == 0) {
        if (ent.name[0] == '.')
            continue;
        write(1, "  ", 2);
        write(1, ent.name, (unsigned long)slen(ent.name));
        if (ent.mode & S_IFDIR)
            write(1, "/", 1);
        write(1, "\n", 1);
    }
    close((int)fd);
    return 0;
}

int main(void)
{
    return list("/") | list("/bin");
}
