[bits 16]
[org 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE], dl

    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    mov dl, [BOOT_DRIVE]
    jmp 0x0200:0x0000

disk_error:
    mov si, msg_error
    mov ah, 0x0E
.print:
    lodsb
    test al, al
    jz .halt
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

BOOT_DRIVE db 0
msg_error db "NyteOS: stage2 disk error", 0

align 4
dap:
    db 0x10, 0
    dw 23
    dw 0
    dw 0x0200
    dq 107

times 510-($-$$) db 0
dw 0xAA55