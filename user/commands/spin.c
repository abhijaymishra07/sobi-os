#include "../libc/unistd.h"

int main(void)
{
    long me = getpid();
    for (int i = 0; i < 200; i++) {
        yield();
        if (getpid() != me)
            return 2;
    }
    write(1, "spin done\n", 10);
    return 0;
}
