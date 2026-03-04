/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** isr — Interrupt Service Routines
*/

#ifndef ISR_H_
#define ISR_H_

#include <stdint.h>

/**
 * @brief CPU interrupt frame — layout of the stack when isr_dispatch() is called.
 *
 * The `isr_common_stub` (isr.s) builds this frame in two phases:
 *   1. The stub macro pushes: err_code (or dummy 0) + int_no
 *   2. `isr_common_stub` does: pusha + push ds
 *
 * The CPU itself pushed eip/cs/eflags (and esp/ss on privilege change).
 *
 * Memory layout (low → high address, i.e. struct offset 0 = lowest address):
 *
 *   [+00]  ds            — saved segment register
 *   [+04]  edi           ─┐
 *   [+08]  esi            │
 *   [+12]  ebp            │ pusha (order: EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI pushed
 *   [+16]  esp (pre-pusha)│       top-first, so EDI ends up at lowest address)
 *   [+20]  ebx            │
 *   [+24]  edx            │
 *   [+28]  ecx            │
 *   [+32]  eax           ─┘
 *   [+36]  int_no        — vector number (0-255)
 *   [+40]  err_code      — CPU error code or 0 for exceptions without one
 *   [+44]  eip           ─┐
 *   [+48]  cs             │ pushed by CPU on exception/interrupt
 *   [+52]  eflags        ─┘
 */
typedef struct __attribute__((packed)) {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // pusha, reversed order on stack
    uint32_t int_no;                                  // interrupt vector number
    uint32_t err_code;                                // error code (0 if none)
    uint32_t eip;                                     // CPU-pushed
    uint32_t cs;                                      // CPU-pushed
    uint32_t eflags;                                  // CPU-pushed
} InterruptFrame_t;

/// Type for an ISR handler registered via isr_register_handler().
typedef void (*isr_handler_t)(const InterruptFrame_t *frame);

////////////////////////////////////////////////////////////
// Assembly stubs — isr0..isr31 (isr.s)
////////////////////////////////////////////////////////////

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

////////////////////////////////////////////////////////////
// Public API
////////////////////////////////////////////////////////////

/**
 * @brief Register a handler for a given interrupt vector.
 *
 * Replaces the default panic handler for that vector.  Use vector 0-31 for
 * CPU exceptions, 32-47 for remapped PIC IRQs (after PIC initialization).
 *
 * @param vec  Interrupt vector number (0-255).
 * @param fn   Handler function, or NULL to restore the default panic handler.
 */
void isr_register_handler(uint8_t vec, isr_handler_t fn);

/**
 * @brief Dispatch an interrupt frame to its registered handler.
 *
 * Called from the assembly `isr_common_stub`.  Should not be called directly
 * from C code.
 *
 * @param frame Pointer to the interrupt frame on the kernel stack.
 */
void isr_dispatch(InterruptFrame_t *frame);

#endif /* !ISR_H_ */
