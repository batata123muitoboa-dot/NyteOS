[bits 16]
[org 0x2000]

KERNEL_OFFSET equ 0x10000
VBE_INFO_ADDR equ 0x8000
VBE_MODE_INFO equ 0x8200
BOOT_INFO_ADDR equ 0x8800

BOOT_INSTALLED db 0

boot_marker_dap:
    db 0x10, 0
    dw 1
    dw 0
    dw 0x0900
    dq 183

legacy_marker_dap:
    db 0x10, 0
    dw 1
    dw 0
    dw 0x0900
    dq 130

%macro READ_KERNEL_CHUNK 3
    mov word [kernel_dap + 2], %1
    mov word [kernel_dap + 6], %2
    mov dword [kernel_dap + 8], %3
    mov dword [kernel_dap + 12], 0
    mov si, kernel_dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error
%endmacro

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE], dl

    ; Verifica se o disco de boot já possui NYTEOS01.
    mov byte [BOOT_INSTALLED], 0

    mov si, boot_marker_dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .legacy_boot_marker

    cmp dword [0x9000], 0x4554594E
    jne .legacy_boot_marker

    cmp dword [0x9004], 0x31305346
    jne .legacy_boot_marker

    cmp dword [0x9020], 0x4554594E
    jne .boot_marker_done

    cmp dword [0x9024], 0x3130534F
    jne .boot_marker_done

    mov byte [BOOT_INSTALLED], 1
    jmp .boot_marker_done

.legacy_boot_marker:
    mov si, legacy_marker_dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .boot_marker_done

    cmp dword [0x9000], 0x4554594E
    jne .boot_marker_done

    cmp dword [0x9004], 0x31305346
    jne .boot_marker_done

    cmp dword [0x9020], 0x4554594E
    jne .boot_marker_done

    cmp dword [0x9024], 0x3130534F
    jne .boot_marker_done

    mov byte [BOOT_INSTALLED], 1

.boot_marker_done:

    ; Lê em blocos de no máximo 32 setores para compatibilidade com BIOS.
    ; Cada bloco começa alinhado a 16 KiB e não cruza fronteiras de 64 KiB.
    READ_KERNEL_CHUNK 32, 0x1000, 1
    READ_KERNEL_CHUNK 32, 0x1400, 33
    READ_KERNEL_CHUNK 32, 0x1800, 65
    READ_KERNEL_CHUNK 32, 0x1C00, 97
    READ_KERNEL_CHUNK 31, 0x2000, 129

    ; Detecta memória estendida
    mov ah, 0x88
    int 0x15
    jc memory_error
    mov [EXT_MEM_KB], ax

    ; Obtém informações VBE
    mov ax, 0x4F00
    mov di, VBE_INFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne vbe_error

    mov si, VBE_INFO_ADDR
    mov dx, [si + 0x0E]
    mov bx, [si + 0x10]
    mov [MODE_LIST_OFFSET], dx
    mov [MODE_LIST_SEGMENT], bx

    mov es, bx
    mov di, dx

find_mode:
    mov cx, [es:di]
    cmp cx, 0xFFFF
    je vbe_error

    mov [CURRENT_MODE], cx

    push es
    push di
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, [CURRENT_MODE]
    mov di, VBE_MODE_INFO
    int 0x10
    pop di
    pop es

    cmp ax, 0x004F
    jne next_mode

    mov ax, [VBE_MODE_INFO]
    test ax, 0x80
    jz next_mode

    cmp word [VBE_MODE_INFO + 0x12], 800
    jne next_mode
    cmp word [VBE_MODE_INFO + 0x14], 600
    jne next_mode
    cmp byte [VBE_MODE_INFO + 0x19], 32
    jne next_mode

    mov bx, [CURRENT_MODE]
    or bx, 0x4000
    mov ax, 0x4F02
    mov cx, bx
    int 0x10
    cmp ax, 0x004F
    jne vbe_error

    mov eax, [VBE_MODE_INFO + 0x28]
    mov [BOOT_INFO_ADDR], eax

    xor eax, eax
    mov ax, [VBE_MODE_INFO + 0x10]
    mov [BOOT_INFO_ADDR + 4], eax

    xor eax, eax
    mov al, [VBE_MODE_INFO + 0x19]
    mov [BOOT_INFO_ADDR + 8], eax

    ; Memória total aproximada, em KB
    movzx eax, word [EXT_MEM_KB]
    add eax, 1024
    mov [BOOT_INFO_ADDR + 12], eax

    movzx eax, byte [BOOT_DRIVE]
    mov [BOOT_INFO_ADDR + 16], eax

    movzx eax, byte [BOOT_INSTALLED]
    mov [BOOT_INFO_ADDR + 20], eax

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:init_pm

next_mode:
    add di, 2
    jmp find_mode

disk_error:
    mov si, msg_disk
    jmp fatal

memory_error:
    mov si, msg_memory
    jmp fatal

vbe_error:
    mov si, msg_vbe

fatal:
    xor ax, ax
    mov ds, ax
    mov ax, 0x0003
    int 0x10
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

msg_disk db "NyteOS: kernel disk error", 0
msg_memory db "NyteOS: RAM detection error", 0
msg_vbe db "NyteOS: VBE 800x600x32 missing", 0

BOOT_DRIVE db 0
CURRENT_MODE dw 0
MODE_LIST_OFFSET dw 0
MODE_LIST_SEGMENT dw 0
EXT_MEM_KB dw 0

align 4
kernel_dap:
    db 0x10, 0
    dw 0
    dw 0
    dw 0
    dq 0

gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0
    db 0, 0x9A, 0xCF, 0
gdt_data:
    dw 0xFFFF, 0
    db 0, 0x92, 0xCF, 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, [0x880C]
    shl eax, 10

    and eax, 0xFFFFFFF0
    mov esp, eax
    mov ebp, esp

    mov eax, KERNEL_OFFSET
    call eax

.hang:
    cli
    hlt
    jmp .hang
