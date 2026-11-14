#include "libc/unistd.h"

int main(void)
{
    for (;;) {
        long pid = spawn("/bin/sh");
        if (pid < 0)
            exit(1);
        waitpid();
    }
}
