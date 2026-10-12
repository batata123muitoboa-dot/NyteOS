@echo off
setlocal

set "PYTHON=python"

echo [+] Compiling bootloader...

nasm -f bin boot.asm -o boot.bin
if errorlevel 1 exit /b 1

nasm -f bin stage2.asm -o stage2.bin
if errorlevel 1 exit /b 1

echo [+] Compiling entry ^& stubs...

nasm -f elf32 entry.asm -o entry.o
if errorlevel 1 exit /b 1

nasm -f elf32 keyboard_stub.asm -o keyboard_stub.o
if errorlevel 1 exit /b 1

echo [+] Compiling exceptions...

nasm -f elf32 exceptions.asm -o exceptions.o
if errorlevel 1 exit /b 1

echo [+] Compiling kernel...

gcc -m32 -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-sign-compare ^
    -ffreestanding -fno-pie -fno-stack-protector ^
    -mno-sse -mno-sse2 -mno-mmx -msoft-float ^
    -c kernel.c -o kernel.o
if errorlevel 1 exit /b 1

echo [+] Compiling interface...

gcc -m32 -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-sign-compare ^
    -ffreestanding -fno-pie -fno-stack-protector ^
    -mno-sse -mno-sse2 -mno-mmx -msoft-float ^
    -c shell.c -o shell.o
if errorlevel 1 exit /b 1

gcc -m32 -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-sign-compare ^
    -ffreestanding -fno-pie -fno-stack-protector ^
    -mno-sse -mno-sse2 -mno-mmx -msoft-float ^
    -c graphics.c -o graphics.o
if errorlevel 1 exit /b 1

echo [+] Compiling RTC...

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector ^
    -mno-sse -mno-sse2 -mno-mmx -msoft-float ^
    -c rtc.c -o rtc.o
if errorlevel 1 exit /b 1

echo [+] Compiling PIT...

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector ^
    -mno-sse -mno-sse2 -mno-mmx -msoft-float ^
    -c pit.c -o pit.o
if errorlevel 1 exit /b 1

echo [+] Compiling Heap...

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector ^
    -mno-sse -mno-sse2 -mno-mmx -msoft-float ^
    -c heap.c -o heap.o
if errorlevel 1 exit /b 1

echo [+] Compiling Speaker...

gcc -m32 -ffreestanding -fno-pie -fno-stack-protector ^
    -mno-sse -mno-sse2 -mno-mmx -msoft-float ^
    -c speaker.c -o speaker.o
if errorlevel 1 exit /b 1

echo [+] Linking kernel ^& modules...

ld -m elf_i386 -T kernel.ld ^
    entry.o ^
    kernel.o ^
    heap.o ^
    shell.o ^
    exceptions.o ^
    keyboard_stub.o ^
    rtc.o ^
    pit.o ^
    speaker.o ^
    graphics.o ^
    -o kernel.elf
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

qemu-system-i386 -m 16M ^
    -drive format=raw,file=nyteos.img ^
    -rtc base=localtime ^
    -audiodev sdl,id=audio0 ^
    -machine pcspk-audiodev=audio0 ^
    -serial stdio ^
    -d int,cpu_reset ^
    -D qemu.log

endlocal