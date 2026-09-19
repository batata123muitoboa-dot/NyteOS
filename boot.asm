[bits 16]
[org 0x7c00]

KERNEL_OFFSET equ 0x10000

VBE_INFO_ADDR equ 0x8000
VBE_MODE_INFO equ 0x8200
BOOT_INFO_ADDR equ 0x8800

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    mov [BOOT_DRIVE], dl

    ; ========================================================
    ; CARREGA KERNEL VIA LBA
    ; ========================================================

    mov bx, KERNEL_OFFSET

    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    mov si, dap
    int 0x13

    jc disk_error

    ; ========================================================
    ; VBE - VERIFICA SE EXISTE
    ; ========================================================

    mov ax, 0x4F00
    mov di, VBE_INFO_ADDR

    xor ax, ax
    mov es, ax

    mov ax, 0x4F00
    mov di, VBE_INFO_ADDR

    int 0x10

    cmp ax, 0x004F
    jne vbe_error

    ; ========================================================
    ; PEGA PONTEIRO PARA LISTA DE MODOS
    ;
    ; VBEInfoBlock + 0x0E = ModeList offset
    ; VBEInfoBlock + 0x10 = ModeList segment
    ; ========================================================

    mov si, VBE_INFO_ADDR

    mov dx, [si + 0x0E]
    mov bx, [si + 0x10]

    mov [MODE_LIST_OFFSET], dx
    mov [MODE_LIST_SEGMENT], bx

    ; ========================================================
    ; PROCURA 800x600x32
    ; ========================================================

    mov ax, bx
    mov es, ax

    mov di, dx

find_mode:

    mov cx, [es:di]

    cmp cx, 0xFFFF
    je vbe_error

    mov [CURRENT_MODE], cx

    ; ========================================================
    ; sei lá deve ser vbe get mode info
    ; ========================================================

    push di
    push es

    xor ax, ax
    mov es, ax

    mov ax, 0x4F01
    mov cx, [CURRENT_MODE]
    mov di, VBE_MODE_INFO

    int 0x10

    pop es
    pop di

    cmp ax, 0x004F
    jne next_mode

    ; ========================================================
    ; Verifica se modo suporta framebuffer linear
    ;
    ; ModeAttributes bit 7 = Linear Framebuffer
    ; ========================================================

    mov ax, [VBE_MODE_INFO + 0x00]

    test ax, 0x80
    jz next_mode

    mov ax, [VBE_MODE_INFO + 0x12]

    cmp ax, 800
    jne next_mode

    mov ax, [VBE_MODE_INFO + 0x14]

    cmp ax, 600
    jne next_mode

    ; ========================================================
    ; 32 bits
    ; ========================================================

    mov al, [VBE_MODE_INFO + 0x19]

    cmp al, 32
    jne next_mode

    mov bx, [CURRENT_MODE]

    or bx, 0x4000

    mov ax, 0x4F02
    mov cx, bx

    int 0x10

    cmp ax, 0x004F
    jne vbe_error

    ; ========================================================
    ; PEGA FRAMEBUFFER
    ; ========================================================

    mov eax, [VBE_MODE_INFO + 0x28]

    mov [FRAMEBUFFER], eax

    ; ========================================================
    ; PEGA PITCH
    ; ========================================================

    xor eax, eax

    mov ax, [VBE_MODE_INFO + 0x10]

    mov [PITCH], eax

    ; ========================================================
    ; PEGA BPP
    ; ========================================================

    xor eax, eax

    mov al, [VBE_MODE_INFO + 0x19]

    mov [BPP], eax

    jmp enter_pm


next_mode:

    add di, 2

    jmp find_mode


; ============================================================
; PROTECTED MODE
; ============================================================

enter_pm:

    cli

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_pm


; ============================================================
; ERROS
; ============================================================

disk_error:

    mov si, msg_disk_error

    call print_str

    jmp $


vbe_error:

    mov si, msg_vbe_error

    call print_str

    jmp $


print_str:

    mov ah, 0x0E

.loop:

    lodsb

    cmp al, 0
    je .done

    int 0x10

    jmp .loop

.done:

    ret


msg_disk_error db "Erro LBA ao ler o kernel!", 0
msg_vbe_error  db "VBE 800x600x32 nao encontrado!", 0

BOOT_DRIVE db 0

CURRENT_MODE dw 0

MODE_LIST_OFFSET dw 0
MODE_LIST_SEGMENT dw 0

FRAMEBUFFER dd 0
PITCH       dd 0
BPP         dd 0


; ============================================================
; DAP
; ============================================================

align 4

dap:
    db 0x10
    db 0x00
    dw 100        ; quantidade de setores, mt importante
    dw 0x0000
    dw 0x1000
    dq 1


; ============================================================
; GDT
; ============================================================

gdt_start:

gdt_null:
    dd 0x00000000
    dd 0x00000000


gdt_code:

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00


gdt_data:

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00


gdt_end:


gdt_descriptor:

    dw gdt_end - gdt_start - 1
    dd gdt_start


CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start


; ============================================================
; 32-BIT PROTECTED MODE
; ============================================================

[bits 32]

init_pm:

    mov ax, DATA_SEG

    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov edi, 0x80000
    mov ecx, 0x10000
    mov eax, 0xCC

    .fill_stack:
        mov [edi], eax
        add edi, 4
        loop .fill_stack

    mov ebp, 0x90000
    mov esp, ebp

    mov ebp, 0x90000
    mov esp, ebp

    ; --------------------------------------------------------
    ; Passa informações pro kernel
    ;
    ; 0x90000 = framebuffer
    ; 0x90004 = pitch
    ; 0x90008 = bpp
    ; --------------------------------------------------------

    mov eax, [FRAMEBUFFER]
    mov [BOOT_INFO_ADDR], eax

    mov eax, [PITCH]
    mov [BOOT_INFO_ADDR + 4], eax

    mov eax, [BPP]
    mov [BOOT_INFO_ADDR + 8], eax

    ; --------------------------------------------------------
    ; Kernel
    ; --------------------------------------------------------

    call KERNEL_OFFSET

    jmp $


times 510-($-$$) db 0

dw 0xAA55