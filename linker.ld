OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS((1 << 0) | (1 << 2));
    rodata PT_LOAD FLAGS((1 << 2));
    data   PT_LOAD FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = 0xffffffff80000000;
    __kernel_start = .;
    __text_start = .;

    .text :
    {
        *(.text .text.*)
    } :text

    __text_end = .;

    . = ALIGN(0x1000);
    __rodata_start = .;

    .rodata :
    {
        *(.rodata .rodata.*)
    } :rodata

    .eh_frame : ALIGN(8)
    {
        KEEP(*(.eh_frame .eh_frame.*))
    } :rodata

    __rodata_end = .;

    . = ALIGN(0x1000);
    __data_start = .;

    .data :
    {
        *(.data .data.*)
    } :data

    .limine_requests_start : ALIGN(8)
    {
        KEEP(*(.limine_requests_start))
    } :data

    .limine_requests : ALIGN(8)
    {
        KEEP(*(.limine_requests))
    } :data

    .limine_requests_end : ALIGN(8)
    {
        KEEP(*(.limine_requests_end))
    } :data

    . = ALIGN(16);
    __bss_start = .;

    .bss :
    {
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(0x1000);
        __stack_guard_boot = .;
        . += 0x1000;
        boot_stack_bottom = .;
        . += 0x4000;
        boot_stack_top = .;
        __stack_guard_ist1 = .;
        . += 0x1000;
        ist1_stack_bottom = .;
        . += 0x4000;
        ist1_stack_top = .;
        __stack_guard_ist2 = .;
        . += 0x1000;
        ist2_stack_bottom = .;
        . += 0x4000;
        ist2_stack_top = .;
    } :data

    . = ALIGN(16);
    __bss_end = .;
    __data_end = .;

    . = ALIGN(0x1000);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.note .note.*)
        *(.comment)
        *(.eh_frame_hdr)
    }
}
