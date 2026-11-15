ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(16)
    {
        *(.text*)
    } :text

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    } :text

    .data : ALIGN(16)
    {
        *(.data*)
    } :data

    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(16);

    stack_bottom = .;

    . += 16384;

    stack_top = .;

    . = ALIGN(4096);

    _end = .;
}