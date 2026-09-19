#!/bin/bash
set -e

echo "[+] A compilar bootloader..."
nasm -f bin boot.asm -o boot.bin

echo "[+] A compilar entry e stubs ASM..."
nasm -f elf32 entry.asm -o entry.o
nasm -f elf32 keyboard_stub.asm -o keyboard_stub.o

echo "[+] A compilar kernel em C..."
gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c kernel.c -o kernel.o

echo "[+] A compilar interface em C..."
gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c shell.c -o shell.o

echo "[+] A compilar Real-Time Clock em C..."
gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -c rtc.c -o rtc.o

echo "[+] A fazer link do kernel e módulos..."
ld -m elf_i386 -Ttext 0x10000 entry.o kernel.o shell.o keyboard_stub.o rtc.o -o kernel.bin --oformat binary

echo "[+] A atualizar imagem NyteFS..."
python3 mknytefs.py
truncate -s 512K nyteos.img

echo "[+] A arrancar QEMU..."
qemu-system-i386 -m 3M -drive format=raw,file=nyteos.img -d int,cpu_reset -D qemu.log