#!/bin/bash

set -e

echo "[+] Compiling bootloader..."

nasm -f bin boot.asm -o boot.bin

nasm -f bin stage2.asm -o stage2.bin

echo "[+] Compiling entry & stubs..."

nasm -f elf32 entry.asm -o entry.o
nasm -f elf32 keyboard_stub.asm -o keyboard_stub.o

echo "[+] Compiling exceptions..."

nasm -f elf32 exceptions.asm -o exceptions.o

echo "[+] Compiling kernel..."

gcc -m32 -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-sign-compare \
    -ffreestanding -fno-pie -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c kernel.c -o kernel.o

echo "[+] Compiling interface..."

gcc -m32 -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-sign-compare \
    -ffreestanding -fno-pie -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c shell.c -o shell.o

gcc -m32 -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-sign-compare \
    -ffreestanding -fno-pie -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c graphics.c -o graphics.o

echo "[+] Compiling RTC..."

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c rtc.c -o rtc.o

echo "[+] Compiling PIT..."

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c pit.c -o pit.o

echo "[+] Compiling Heap..."

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c heap.c -o heap.o

echo "[+] Compiling Speaker..."

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c speaker.c -o speaker.o

echo "[+] Linking kernel & modules..."

ld -m elf_i386 -T kernel.ld \
    entry.o \
    kernel.o \
    heap.o \
    shell.o \
    exceptions.o \
    keyboard_stub.o \
    rtc.o \
    pit.o \
    speaker.o \
    graphics.o \
    -o kernel.elf

objcopy -O binary kernel.elf kernel.bin

echo "[+] Updating NyteFS image..."

truncate -s 512K nyteos.img

python3 mknytefs.py

echo "[+] Running QEMU..."

qemu-system-i386 -m 16M \
    -drive format=raw,file=nyteos.img \
    -rtc base=localtime \
    -audiodev pipewire,id=audio0 \
    -machine pcspk-audiodev=audio0 \
    -serial stdio \
    -d int,cpu_reset \
    -D qemu.log &

QEMU_PID=$!

for i in {1..50}; do
    QEMU_AUDIO_ID=$(wpctl status 2>/dev/null | awk '
        /Streams:/ { streams=1; next }
        streams && /qemu-system-i386/ {
            gsub(/\./, "", $1)
            print $1
            exit
        }
    ')

    if [ -n "$QEMU_AUDIO_ID" ]; then
        if wpctl set-mute "$QEMU_AUDIO_ID" 0 2>/dev/null &&
           wpctl set-volume "$QEMU_AUDIO_ID" 0.5 2>/dev/null; then
            break
        fi
    fi

    sleep 0.1
done

wait "$QEMU_PID"