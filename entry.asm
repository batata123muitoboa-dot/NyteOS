[bits 32]

[extern kernel_main]
[extern __bss_start]
[extern __bss_end]
[extern stack_top]

global _start

_start:
    mov esp, stack_top
    mov ebp, stack_top

    pushad

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi

    xor eax, eax

    mov edx, ecx
    shr ecx, 2
    rep stosd

    mov ecx, edx
    and ecx, 3
    rep stosb

    popad

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang