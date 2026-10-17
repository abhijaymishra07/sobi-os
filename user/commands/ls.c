#include "../libc/unistd.h"

int main(void)
{
    write(1, "ls\n", 3);
    return 0;
}
