[bits 32]
[extern keyboard_handler_main]
[global keyboard_stub]

keyboard_stub:
    pushad
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    call keyboard_handler_main

    pop gs
    pop fs
    pop es
    pop ds
    popad

    iretd
