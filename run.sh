#!/bin/bash

set -e

echo "[+] Compiling bootloader..."

nasm -f bin boot.asm -o boot.bin

echo "[+] Compiling entry & stubs..."

nasm -f elf32 entry.asm -o entry.o
nasm -f elf32 keyboard_stub.asm -o keyboard_stub.o

echo "[+] Compiling kernel..."

gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c kernel.c -o kernel.o

echo "[+] Compiling interface..."

gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c shell.c -o shell.o

echo "[+] Compiling RTC..."

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -c rtc.c -o rtc.o

echo "[+] Compiling PIT..."

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -c pit.c -o pit.o

echo "[+] Compiling Heap..."

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -c heap.c -o heap.o

echo "[+] Linking kernel & modules..."

ld -m elf_i386 -T kernel.ld \
    entry.o kernel.o heap.o shell.o keyboard_stub.o rtc.o pit.o \
    -o kernel.elf

objcopy -O binary kernel.elf kernel.bin

echo "[+] Updating NyteFS image..."

truncate -s 512K nyteos.img

python3 mknytefs.py

echo "[+] Running QEMU.."

qemu-system-i386 -m 3M \
    -drive format=raw,file=nyteos.img \
    -rtc base=localtime \
    -d int,cpu_reset \
    -D qemu.log