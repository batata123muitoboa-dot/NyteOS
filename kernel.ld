ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss ALIGN(4) :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);

    stack_bottom = .;

    . += 16384;

    stack_top = .;

    _end = .;
}