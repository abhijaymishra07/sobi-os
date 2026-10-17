#include "../libc/unistd.h"

int main(void)
{
    write(1, "cat\n", 4);
    return 0;
}
