/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** isr — Interrupt Service Routine dispatcher
*/

#include <kernel/cpu/isr.h>
#include <stddef.h>
#include <stdint.h>

////////////////////////////////////////////////////////////
// Self-contained panic output via direct COM1 port I/O
//
// We deliberately avoid any dependency on the Serial driver module so that
// this code can print a panic message even before (or after) the higher-level
// driver is torn down.
////////////////////////////////////////////////////////////

#define COM1_PORT     0x3F8u
#define COM1_LSR_THRE 0x20u // Transmitter Holding Register Empty

static inline void _isr_outb(uint16_t port, uint8_t val) { __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }

static inline uint8_t _isr_inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void _isr_write_char(char c)
{
    while (!(_isr_inb(COM1_PORT + 5u) & COM1_LSR_THRE))
    {
    }
    _isr_outb(COM1_PORT, (uint8_t) c);
}

static void _isr_write_str(const char *s)
{
    while (*s)
        _isr_write_char(*s++);
}

static void _isr_write_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    _isr_write_str("0x");
    for (int i = 28; i >= 0; i -= 4)
        _isr_write_char(hex[(v >> i) & 0xF]);
}

////////////////////////////////////////////////////////////
// CPU exception names (vectors 0-31)
////////////////////////////////////////////////////////////

static const char *const g_exception_names[32] = {
    "#DE Division Error",
    "#DB Debug",
    "Non-Maskable Interrupt",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reserved (15)",
    "#MF x87 Floating-Point",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XF SIMD Floating-Point",
    "#VE Virtualization",
    "#CP Control Protection",
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

void isr_register_handler(uint8_t vec, isr_handler_t fn) { g_isr_table[vec] = fn; }

////////////////////////////////////////////////////////////
// Default (panic) handler
////////////////////////////////////////////////////////////

static void isr_default_handler(const InterruptFrame_t *frame)
{
    const char *name = (frame->int_no < 32) ? g_exception_names[frame->int_no] : "Unknown interrupt";

    _isr_write_str("\r\n\r\n[KERNEL PANIC] Unhandled exception: ");
    _isr_write_str(name);
    _isr_write_str("\r\n");
    _isr_write_str("  int_no  = ");
    _isr_write_hex32(frame->int_no);
    _isr_write_str("\r\n");
    _isr_write_str("  err_code= ");
    _isr_write_hex32(frame->err_code);
    _isr_write_str("\r\n");
    _isr_write_str("  eip     = ");
    _isr_write_hex32(frame->eip);
    _isr_write_str("\r\n");
    _isr_write_str("  cs      = ");
    _isr_write_hex32(frame->cs);
    _isr_write_str("\r\n");
    _isr_write_str("  eflags  = ");
    _isr_write_hex32(frame->eflags);
    _isr_write_str("\r\n");
    _isr_write_str("  eax     = ");
    _isr_write_hex32(frame->eax);
    _isr_write_str("\r\n");
    _isr_write_str("  ecx     = ");
    _isr_write_hex32(frame->ecx);
    _isr_write_str("\r\n");
    _isr_write_str("  edx     = ");
    _isr_write_hex32(frame->edx);
    _isr_write_str("\r\n");
    _isr_write_str("  ebx     = ");
    _isr_write_hex32(frame->ebx);
    _isr_write_str("\r\n");
    _isr_write_str("  esp     = ");
    _isr_write_hex32(frame->esp);
    _isr_write_str("\r\n");
    _isr_write_str("  ebp     = ");
    _isr_write_hex32(frame->ebp);
    _isr_write_str("\r\n");
    _isr_write_str("  esi     = ");
    _isr_write_hex32(frame->esi);
    _isr_write_str("\r\n");
    _isr_write_str("  edi     = ");
    _isr_write_hex32(frame->edi);
    _isr_write_str("\r\n");
    _isr_write_str("  ds      = ");
    _isr_write_hex32(frame->ds);
    _isr_write_str("\r\n");
    _isr_write_str("--- HALTED ---\r\n");

    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}

////////////////////////////////////////////////////////////
// Dispatcher — called from isr_common_stub (isr.s)
////////////////////////////////////////////////////////////

void isr_dispatch(InterruptFrame_t *frame)
{
    isr_handler_t handler = g_isr_table[frame->int_no];
    if (handler)
        handler(frame);
    else
        isr_default_handler(frame);
}
