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

static void to_path(char *path, const char *word)
{
    const char *prefix = "/bin/";
    int n = 0;
    while (prefix[n]) {
        path[n] = prefix[n];
        n++;
    }
    for (int i = 0; word[i] && word[i] != ' ' && i < 40; i++)
        path[n++] = word[i];
    path[n] = 0;
}

static int run_pipeline(char *line)
{
    char *bar = line;
    while (*bar && *bar != '|')
        bar++;
    if (!*bar)
        return 0;
    *bar = 0;
    char *right = bar + 1;
    while (*right == ' ')
        right++;
    char a[64];
    char b[64];
    to_path(a, line);
    to_path(b, right);
    if (pipeline(a, b) < 0)
        write(1, "not found\n", 10);
    return 1;
}

static void run_line(char *line)
{
    while (*line == ' ')
        line++;
    if (!*line)
        return;
    if (run_pipeline(line))
        return;
    if (starts(line, "exit") && (line[4] == 0 || line[4] == ' '))
        exit(0);
    if (starts(line, "help")) {
        const char *h =
            "built-ins: help echo pwd uptime clear exit\n"
            "programs:  ls cat upper hello spin (from /bin)\n"
            "pipes:     cat | upper\n";
        write(1, h, (unsigned long)slen(h));
        return;
    }
    if (starts(line, "clear")) {
        write(1, "\033[2J\033[H", 7);
        return;
    }
    if (starts(line, "uptime")) {
        char buf[32];
        int n = 0;
        unsigned long ms = (unsigned long)uptime();
        unsigned long s = ms / 1000;
        char digits[20];
        int d = 0;
        do {
            digits[d++] = (char)('0' + s % 10);
            s /= 10;
        } while (s);
        while (d)
            buf[n++] = digits[--d];
        buf[n++] = '.';
        buf[n++] = (char)('0' + (ms % 1000) / 100);
        buf[n++] = 's';
        buf[n++] = '\n';
        write(1, "up ", 3);
        write(1, buf, (unsigned long)n);
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
    to_path(path, line);
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
        if (r <= 0) {
            yield();
            continue;
        }
        if (c == '\b' || c == 127) {
            if (n > 0) {
                n--;
                write(1, "\b \b", 3);
            }
            continue;
        }
        if (c == '\r')
            c = '\n';
        if (c != '\n') {
            if (n < 127) {
                line[n++] = c;
                write(1, &c, 1);
            }
            continue;
        }
        write(1, "\n", 1);
        line[n] = 0;
        run_line(line);
        n = 0;
        write(1, "$ ", 2);
    }
}
