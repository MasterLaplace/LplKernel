/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** isr — Interrupt Service Routine dispatcher
*/

#include <kernel/cpu/isr.h>
#include <kernel/lib/asmutils.h>
#include <stddef.h>
#include <stdint.h>

////////////////////////////////////////////////////////////
// Private helpers for panic serial output
////////////////////////////////////////////////////////////

#define COM1_PORT     0x3F8u
#define COM1_LSR_THRE 0x20u

static void isr_write_char(char c)
{
    while (!(inb((short) (COM1_PORT + 5u)) & COM1_LSR_THRE))
    {
    }
    outb((short) COM1_PORT, (unsigned char) c);
}

static void isr_write_string(const char *s)
{
    while (*s)
        isr_write_char(*s++);
}

static void isr_write_hex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    isr_write_string("0x");
    for (int i = 28; i >= 0; i -= 4)
        isr_write_char(hex[(value >> i) & 0xF]);
}

////////////////////////////////////////////////////////////
// CPU exception names (vectors 0-31)
////////////////////////////////////////////////////////////

static const char *const isr_exception_names[32] = {
    "#DE Divide Error Fault",
    "#DB Debug Exception Trap",
    "#NMI Non-Maskable Interrupt",
    "#BP Breakpoint Trap",
    "#OF Overflow Trap",
    "#BR BOUND Range Exceeded Fault",
    "#UD Invalid Opcode (Undefined Opcode) Fault",
    "#NM Device Not Available (No Math Coprocessor) Fault",
    "#DF Double Fault Abort",
    "Coprocessor Segment Overrun (reserved) Fault",
    "#TS Invalid TSS Fault",
    "#NP Segment Not Present Fault",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reserved (15)",
    "#MF x87 Floating-Point Error (Math Fault)",
    "#AC Alignment Check Fault",
    "#MC Machine Check Abort",
    "#XF SIMD Floating-Point Exception Fault",
    "#VE Virtualization Exception Fault",
    "#CP Control Protection Exception Fault",
    "Reserved (22)",
    "Reserved (23)",
    "Reserved (24)",
    "Reserved (25)",
    "Reserved (26)",
    "Reserved (27)",
    "Reserved (28)",
    "Reserved (29)",
    "Reserved (30)",
    "Reserved (31)",
};

////////////////////////////////////////////////////////////
// Dispatch table
////////////////////////////////////////////////////////////

static isr_handler_t g_isr_table[256] = {NULL};

void interrupt_service_routine_register_handler(uint8_t interrupt_vector, isr_handler_t handler)
{
    g_isr_table[interrupt_vector] = handler;
}

////////////////////////////////////////////////////////////
// Default (panic) handler
////////////////////////////////////////////////////////////

static void isr_default_handler(const InterruptFrame_t *frame)
{
    const char *name = (frame->int_no < 32) ? isr_exception_names[frame->int_no] : "Unknown interrupt";

    isr_write_string("\r\n\r\n[KERNEL PANIC] Unhandled exception: ");
    isr_write_string(name);
    isr_write_string("\r\n");
    isr_write_string("  int_no  = ");
    isr_write_hex32(frame->int_no);
    isr_write_string("\r\n");
    isr_write_string("  err_code= ");
    isr_write_hex32(frame->err_code);
    isr_write_string("\r\n");
    isr_write_string("  eip     = ");
    isr_write_hex32(frame->eip);
    isr_write_string("\r\n");
    isr_write_string("  cs      = ");
    isr_write_hex32(frame->cs);
    isr_write_string("\r\n");
    isr_write_string("  eflags  = ");
    isr_write_hex32(frame->eflags);
    isr_write_string("\r\n");
    isr_write_string("  eax     = ");
    isr_write_hex32(frame->eax);
    isr_write_string("\r\n");
    isr_write_string("  ecx     = ");
    isr_write_hex32(frame->ecx);
    isr_write_string("\r\n");
    isr_write_string("  edx     = ");
    isr_write_hex32(frame->edx);
    isr_write_string("\r\n");
    isr_write_string("  ebx     = ");
    isr_write_hex32(frame->ebx);
    isr_write_string("\r\n");
    isr_write_string("  esp     = ");
    isr_write_hex32(frame->esp);
    isr_write_string("\r\n");
    isr_write_string("  ebp     = ");
    isr_write_hex32(frame->ebp);
    isr_write_string("\r\n");
    isr_write_string("  esi     = ");
    isr_write_hex32(frame->esi);
    isr_write_string("\r\n");
    isr_write_string("  edi     = ");
    isr_write_hex32(frame->edi);
    isr_write_string("\r\n");
    isr_write_string("  ds      = ");
    isr_write_hex32(frame->ds);
    isr_write_string("\r\n");
    isr_write_string("--- HALTED ---\r\n");

    cpu_disable_interrupts();
    for (;;)
        cpu_halt();
}

////////////////////////////////////////////////////////////
// Dispatcher — called from isr_common_stub (isr.s)
////////////////////////////////////////////////////////////

void interrupt_service_routine_dispatch(InterruptFrame_t *frame)
{
    isr_handler_t handler = g_isr_table[frame->int_no];
    if (handler)
        handler(frame);
    else
        isr_default_handler(frame);
}
