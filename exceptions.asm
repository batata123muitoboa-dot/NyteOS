[bits 32]

global exception0
global exception1
global exception2
global exception3
global exception4
global exception5
global exception6
global exception7
global exception8
global exception9
global exception10
global exception11
global exception12
global exception13
global exception14
global exception15
global exception16
global exception17
global exception18
global exception19
global exception20
global exception21
global exception22
global exception23
global exception24
global exception25
global exception26
global exception27
global exception28
global exception29
global exception30
global exception31

extern exception_handler

%macro EXCEPTION_NOERR 1
exception%1:
    push dword 0
    push dword %1
    jmp exception_common
%endmacro

%macro EXCEPTION_ERR 1
exception%1:
    push dword %1
    jmp exception_common
%endmacro

EXCEPTION_NOERR 0
EXCEPTION_NOERR 1
EXCEPTION_NOERR 2
EXCEPTION_NOERR 3
EXCEPTION_NOERR 4
EXCEPTION_NOERR 5
EXCEPTION_NOERR 6
EXCEPTION_NOERR 7

EXCEPTION_ERR   8

EXCEPTION_NOERR 9

EXCEPTION_ERR   10
EXCEPTION_ERR   11
EXCEPTION_ERR   12
EXCEPTION_ERR   13
EXCEPTION_ERR   14

EXCEPTION_NOERR 15
EXCEPTION_NOERR 16

EXCEPTION_ERR   17

EXCEPTION_NOERR 18
EXCEPTION_NOERR 19
EXCEPTION_NOERR 20
EXCEPTION_NOERR 21
EXCEPTION_NOERR 22
EXCEPTION_NOERR 23
EXCEPTION_NOERR 24
EXCEPTION_NOERR 25
EXCEPTION_NOERR 26
EXCEPTION_NOERR 27
EXCEPTION_NOERR 28
EXCEPTION_NOERR 29

EXCEPTION_ERR   30

EXCEPTION_NOERR 31

exception_common:
    pusha

    push dword [esp + 36]
    push dword [esp + 32]

    call exception_handler

    add esp, 8
    popa

    add esp, 8

    iretd
