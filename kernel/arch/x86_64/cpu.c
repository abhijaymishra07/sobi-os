#include <sobi/cpu.h>

void cpu_halt_forever(void)
{
    for (;;) {
        cpu_cli();
        cpu_hlt();
    }
}
