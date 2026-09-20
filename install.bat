@echo off

git clone https://github.com/batata123muitoboa-dot/NyteOS.git
cd NyteOS

echo Make sure qemu-desktop, Python, NASM, GCC and LD is installed.
echo To run NyteOS in a virtual machine using QEMU, run "run.bat".

set /p answer=Do you want to run the virtual machine now? [Y/n] 

if "%answer%"=="" goto run
if /I "%answer%"=="Y" goto run

echo Ok.
goto end

:run
call run.bat

:end