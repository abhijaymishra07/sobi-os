#include "libc/unistd.h"

static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static int starts(const char *s, const char *p)
{
    while (*p) {
        if (*s != *p)
            return 0;
        s++;
        p++;
    }
    return 1;
}

static void run_line(char *line)
{
    while (*line == ' ')
        line++;
    if (!*line)
        return;
    if (starts(line, "exit") && (line[4] == 0 || line[4] == ' '))
        exit(0);
    if (starts(line, "help")) {
        write(1, "echo exit help pwd ls\n", 22);
        return;
    }
    if (starts(line, "pwd")) {
        write(1, "/\n", 2);
        return;
    }
    if (starts(line, "echo")) {
        const char *rest = line + 4;
        while (*rest == ' ')
            rest++;
        if (*rest)
            write(1, rest, (unsigned long)slen(rest));
        write(1, "\n", 1);
        return;
    }
    char path[64];
    path[0] = '/';
    path[1] = 'b';
    path[2] = 'i';
    path[3] = 'n';
    path[4] = '/';
    int i = 0;
    while (line[i] && line[i] != ' ' && i < 40) {
        path[5 + i] = line[i];
        i++;
    }
    path[5 + i] = 0;
    long pid = spawn(path);
    if (pid < 0) {
        write(1, "not found\n", 10);
        return;
    }
    waitpid();
}

int main(void)
{
    char line[128];
    int n = 0;
    write(1, "$ ", 2);
    for (;;) {
        char c = 0;
        long r = read(0, &c, 1);
        if (r < 0)
            continue;
        if (r == 0)
            continue;
        if (c == '\b' || c == 127) {
            if (n > 0)
                n--;
            continue;
        }
        if (c != '\n') {
            if (n < 127)
                line[n++] = c;
            continue;
        }
        line[n] = 0;
        run_line(line);
        n = 0;
        write(1, "$ ", 2);
    }
}
