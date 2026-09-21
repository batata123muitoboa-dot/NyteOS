@echo off

setlocal

set "PYTHON=python"

echo [+] Compiling bootloader...

nasm -f bin boot.asm -o boot.bin

if errorlevel 1 exit /b 1

echo [+] Compiling entry ^& stubs...

nasm -f elf32 entry.asm -o entry.o

if errorlevel 1 exit /b 1

nasm -f elf32 keyboard_stub.asm -o keyboard_stub.o

if errorlevel 1 exit /b 1

echo [+] Compiling kernel...

gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c kernel.c -o kernel.o

if errorlevel 1 exit /b 1

echo [+] Compiling interface...

gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c shell.c -o shell.o

if errorlevel 1 exit /b 1

echo [+] Compiling RTC...

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -c rtc.c -o rtc.o

if errorlevel 1 exit /b 1

echo [+] Compiling PIT...

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -c pit.c -o pit.o

if errorlevel 1 exit /b 1

echo [+] Linking kernel ^& modules...

ld -m elf_i386 -T kernel.ld entry.o kernel.o shell.o keyboard_stub.o rtc.o pit.o -o kernel.elf

if errorlevel 1 exit /b 1

echo [+] Creating kernel binary...

objcopy -O binary kernel.elf kernel.bin

if errorlevel 1 exit /b 1

echo [+] Updating NyteFS image...

powershell -Command "$f = [System.IO.File]::OpenWrite('nyteos.img'); $f.SetLength(512KB); $f.Close()"

if errorlevel 1 exit /b 1

%PYTHON% mknytefs.py

if errorlevel 1 exit /b 1

echo [+] Running QEMU...

qemu-system-i386 -m 3M -drive format=raw,file=nyteos.img -rtc base=localtime -d int,cpu_reset -D qemu.log