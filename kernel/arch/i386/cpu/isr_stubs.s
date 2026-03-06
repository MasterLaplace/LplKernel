.section .text

.include "arch/i386/cpu/segment_selectors.inc"

# ---- Macros for ISR entry stubs ------------------------------------------
#
# ISR_NOERR n : CPU does NOT push an error code for this vector.
#               We push a dummy 0 so the stack layout is always the same.
#
# ISR_ERR   n : CPU already pushed an error code before transferring control.
#               We only push the vector number.

.macro ISR_NOERR n
.global isr\n
.type   isr\n, @function
isr\n:
    pushl $0        # dummy error code
    pushl $\n       # interrupt vector number
    jmp   isr_common_stub
.endm

.macro ISR_ERR n
.global isr\n
.type   isr\n, @function
isr\n:
    pushl $\n       # interrupt vector number (error code already on stack)
    jmp   isr_common_stub
.endm

# ---- 32 CPU exception stubs -----------------------------------------------
#
# Exceptions with an error code pushed by the CPU (Intel SDM Vol.3A §6.3.1):
#   #8  Double Fault
#   #10 Invalid TSS
#   #11 Segment Not Present
#   #12 Stack-Segment Fault
#   #13 General Protection Fault
#   #14 Page Fault
#   #17 Alignment Check
#   #21 Control Protection Exception

ISR_NOERR  0    # #DE  Division Error
ISR_NOERR  1    # #DB  Debug
ISR_NOERR  2    #      Non-Maskable Interrupt
ISR_NOERR  3    # #BP  Breakpoint
ISR_NOERR  4    # #OF  Overflow
ISR_NOERR  5    # #BR  Bound Range Exceeded
ISR_NOERR  6    # #UD  Invalid Opcode
ISR_NOERR  7    # #NM  Device Not Available
ISR_ERR    8    # #DF  Double Fault        (error code = 0, but CPU pushes it)
ISR_NOERR  9    #      Coprocessor Segment Overrun (legacy, reserved)
ISR_ERR   10    # #TS  Invalid TSS
ISR_ERR   11    # #NP  Segment Not Present
ISR_ERR   12    # #SS  Stack-Segment Fault
ISR_ERR   13    # #GP  General Protection Fault
ISR_ERR   14    # #PF  Page Fault
ISR_NOERR 15    #      Reserved
ISR_NOERR 16    # #MF  x87 Floating-Point Exception
ISR_ERR   17    # #AC  Alignment Check
ISR_NOERR 18    # #MC  Machine Check
ISR_NOERR 19    # #XF  SIMD Floating-Point Exception
ISR_NOERR 20    # #VE  Virtualization Exception
ISR_ERR   21    # #CP  Control Protection Exception
ISR_NOERR 22    #      Reserved
ISR_NOERR 23    #      Reserved
ISR_NOERR 24    #      Reserved
ISR_NOERR 25    #      Reserved
ISR_NOERR 26    #      Reserved
ISR_NOERR 27    #      Reserved
ISR_NOERR 28    #      Reserved
ISR_NOERR 29    #      Reserved
ISR_NOERR 30    #      Reserved
ISR_NOERR 31    #      Reserved

# ---- PIC IRQ stubs (after remap: vectors 32-47) --------------------------

ISR_NOERR 32    # IRQ0  PIT timer
ISR_NOERR 33    # IRQ1  Keyboard
ISR_NOERR 34    # IRQ2  Cascade (slave PIC)
ISR_NOERR 35    # IRQ3  COM2/COM4
ISR_NOERR 36    # IRQ4  COM1/COM3
ISR_NOERR 37    # IRQ5  LPT2 / sound card
ISR_NOERR 38    # IRQ6  Floppy
ISR_NOERR 39    # IRQ7  LPT1 / spurious
ISR_NOERR 40    # IRQ8  RTC
ISR_NOERR 41    # IRQ9  ACPI / legacy
ISR_NOERR 42    # IRQ10 Available
ISR_NOERR 43    # IRQ11 Available
ISR_NOERR 44    # IRQ12 PS/2 mouse
ISR_NOERR 45    # IRQ13 FPU / coprocessor
ISR_NOERR 46    # IRQ14 Primary ATA
ISR_NOERR 47    # IRQ15 Secondary ATA / spurious

# ---- Common ISR stub ------------------------------------------------------
#
# Stack layout just before pusha (at entry to this label):
#
#   [esp+ 0]  int_no      <- pushed by macro
#   [esp+ 4]  err_code    <- pushed by CPU or dummy 0
#   [esp+ 8]  eip         <- CPU
#   [esp+12]  cs          <- CPU
#   [esp+16]  eflags      <- CPU
#   (+ esp_user / ss_user if privilege change from ring 3)
#
# After `pusha` + `push ds` the stack (low→high) ==  InterruptFrame_t layout:
#
#   [esp+ 0]  ds
#   [esp+ 4]  edi, esi, ebp, esp_before_pusha, ebx, edx, ecx, eax
#   [esp+36]  int_no
#   [esp+40]  err_code
#   [esp+44]  eip, cs, eflags  (CPU pushed)

.type isr_common_stub, @function
isr_common_stub:
    pusha                       # save EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI

    movl %ds, %eax
    pushl %eax                  # save data segment register

    movw $KERNEL_DS_SELECTOR, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushl %esp                  # pass pointer to InterruptFrame_t as argument
    call  interrupt_service_routine_dispatch
    addl  $4, %esp              # discard argument

    popl  %eax                  # restore original data segment
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    popa                        # restore EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    addl  $8, %esp              # discard int_no and err_code
    iret                        # restore EIP, CS, EFLAGS (and ESP/SS if ring change)
