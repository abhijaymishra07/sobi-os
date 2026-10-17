#include "../libc/unistd.h"

int main(void)
{
    write(1, "echo\n", 5);
    return 0;
}
