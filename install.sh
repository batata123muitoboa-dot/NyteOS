#!/bin/bash

git clone https://github.com/batata123muitoboa-dot/NyteOS.git
cd NyteOS

echo 'Make sure qemu-desktop, Python, NASM, GCC and LD is installed.'
echo 'To run NyteOS in a virtual machine using QEMU, run "sh run.sh" from the terminal inside the folder.'

read -r -p 'Do you want to run the virtual machine now? [Y/n] ' answer

if [ "$answer" = "Y" ] || [ "$answer" = "y" ] || [ -z "$answer" ]; then
    sh run.sh
else
    echo 'Ok.'
fi