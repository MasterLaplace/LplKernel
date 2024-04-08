[bits 32]

section .multiboot
    dd 0x1BADB002            ; magic
    dd 0x00                  ; flags
    dd - (0x1BADB002 + 0x00) ; checksum. magic + flags + checksum should be 0

section .bss
global bootstrap_stack
bootstrap_stack:
    resb 8192 ; 8KB for stack

section .text
global start
extern kernel_main

start:
    ; set up the stack before we call into C
    mov esp, bootstrap_stack + 8192

    call kernel_main

    ; if kernel_main returns, halt the CPU
.hang:
    cli
    hlt
    jmp .hang
