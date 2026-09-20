@echo off
setlocal
set "PYTHON=python"

echo [+] A compilar bootloader...

nasm -f bin boot.asm -o boot.bin
if errorlevel 1 exit /b 1

echo [+] A compilar entry e stubs ASM...

nasm -f elf32 entry.asm -o entry.o
if errorlevel 1 exit /b 1

nasm -f elf32 keyboard_stub.asm -o keyboard_stub.o
if errorlevel 1 exit /b 1

echo [+] A compilar kernel em C...

gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c kernel.c -o kernel.o
if errorlevel 1 exit /b 1

echo [+] A compilar interface em C...

gcc -m32 -Wall -Wextra -ffreestanding -fno-pie -fno-stack-protector -c shell.c -o shell.o
if errorlevel 1 exit /b 1

echo [+] A compilar Real-Time Clock em C...

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -c rtc.c -o rtc.o
if errorlevel 1 exit /b 1

echo [+] A fazer link do kernel e modulos...

ld -m elf_i386 -Ttext 0x10000 entry.o kernel.o shell.o keyboard_stub.o rtc.o -o kernel.bin --oformat binary
if errorlevel 1 exit /b 1

echo [+] A atualizar imagem NyteFS...

%PYTHON% mknytefs.py
if errorlevel 1 exit /b 1

echo [+] A criar imagem NyteOS...

powershell -Command "$f = [System.IO.File]::OpenWrite('nyteos.img'); $f.SetLength(512KB); $f.Close()"

echo [+] A arrancar QEMU...

qemu-system-i386 -m 3M -drive format=raw,file=nyteos.img -d int,cpu_reset -D qemu.log