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

# ---- Custom / IPI stubs ---------------------------------------------------

ISR_NOERR 64    # 0x40: TLB Shootdown IPI
ISR_NOERR 128   # 0x80: syscall gate (DPL3)

# ---- Device interrupt stubs (vectors 0x50-0x6F, #59) ----------------------
#
# A device's message or I/O APIC line carries one of them
# (kernel/include/kernel/cpu/device_interrupt.h);
# the table below lists them in order, for the IDT.

.irp n, 80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111
ISR_NOERR \n
.endr

.section .rodata
.align 4
.global interrupt_service_routine_device_stubs
interrupt_service_routine_device_stubs:
.irp n, 80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111
    .long isr\n
.endr

.section .text

# ---- FPU / SSE state save area ---------------------------------------------
#
# CR4.OSFXSR is set in boot.S so FXSAVE/FXRSTOR cover all 512 bytes of FPU+SSE
# state (x87 + MMX + XMM0-XMM7 + MXCSR). Each interrupt saves it on its own
# stack, below the frame, aligned to the 16 bytes FXSAVE needs: two CPUs in the
# stub at once, or a fault taken inside a handler, each restore their own (#560).

.set ISR_FPU_SAVE_AREA_SIZE, 512

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
#
# Below the frame, at the call to the dispatcher (high→low):
#
#   0 to 15 bytes      left by aligning the save area down to 16
#   512 bytes          this interrupt's FXSAVE area, 16-aligned
#   12 bytes, then 4   padding, then the frame pointer: ESP is 16-aligned at the call

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

    movl  %esp, %ebx            # the InterruptFrame_t; EBX survives the call (cdecl)
    subl  $ISR_FPU_SAVE_AREA_SIZE, %esp
    andl  $-16, %esp            # this interrupt's save area, below the frame
    fxsave (%esp)               # preserve x87/MMX/XMM0-7/MXCSR across C dispatch

    subl  $12, %esp             # 16-aligned at the call, as the System V i386 ABI asks
    pushl %ebx                  # pass pointer to InterruptFrame_t as argument
    call  interrupt_service_routine_dispatch
    addl  $16, %esp             # discard argument and padding

    fxrstor (%esp)              # restore this interrupt's FPU/SSE state
    movl  %ebx, %esp            # back to the frame

    popl  %eax                  # restore original data segment
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    popa                        # restore EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    addl  $8, %esp              # discard int_no and err_code
    iret                        # restore EIP, CS, EFLAGS (and ESP/SS if ring change)
