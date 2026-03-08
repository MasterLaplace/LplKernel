#include <kernel/cpu/exception.h>

#include <kernel/cpu/isr.h>
#include <kernel/lib/asmutils.h>

#include <stdint.h>

#define EXCEPTION_VECTOR_DOUBLE_FAULT             8u
#define EXCEPTION_VECTOR_DEBUG_EXCEPTION          1u
#define EXCEPTION_VECTOR_BREAKPOINT               3u
#define EXCEPTION_VECTOR_INVALID_OPCODE           6u
#define EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT 13u
#define EXCEPTION_VECTOR_PAGE_FAULT               14u

#define COM1_PORT     0x3F8u
#define COM1_LSR_THRE 0x20u

static void exception_write_char(char c)
{
    while (!(inb((short) (COM1_PORT + 5u)) & COM1_LSR_THRE))
    {
    }
    outb((short) COM1_PORT, (unsigned char) c);
}

static void exception_write_string(const char *s)
{
    while (*s)
        exception_write_char(*s++);
}

static void exception_write_hex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    exception_write_string("0x");
    for (int i = 28; i >= 0; i -= 4)
        exception_write_char(hex[(value >> i) & 0xFu]);
}

static void exception_halt_forever(void)
{
    cpu_disable_interrupts();
    for (;;)
        cpu_halt();
}

static void exception_handle_double_fault(const InterruptFrame_t *frame)
{
    (void) frame;
    exception_write_string("\r\n\r\n[KERNEL PANIC] #DF Double Fault\r\n");
    exception_write_string("  err_code= ");
    exception_write_hex32(frame->err_code);
    exception_write_string("\r\n");
    exception_halt_forever();
}

static void exception_handle_debug_exception(const InterruptFrame_t *frame)
{
    exception_write_string("\r\n\r\n[KERNEL PANIC] #DB Debug Exception\r\n");
    exception_write_string("  err_code= ");
    exception_write_hex32(frame->err_code);
    exception_write_string("\r\n");
    exception_write_string("  eip     = ");
    exception_write_hex32(frame->eip);
    exception_write_string("\r\n");
    exception_halt_forever();
}

static void exception_handle_breakpoint(const InterruptFrame_t *frame)
{
    exception_write_string("\r\n\r\n[KERNEL PANIC] #BP Breakpoint\r\n");
    exception_write_string("  err_code= ");
    exception_write_hex32(frame->err_code);
    exception_write_string("\r\n");
    exception_write_string("  eip     = ");
    exception_write_hex32(frame->eip);
    exception_write_string("\r\n");
    exception_halt_forever();
}

static void exception_handle_invalid_opcode(const InterruptFrame_t *frame)
{
    exception_write_string("\r\n\r\n[KERNEL PANIC] #UD Invalid Opcode\r\n");
    exception_write_string("  err_code= ");
    exception_write_hex32(frame->err_code);
    exception_write_string("\r\n");
    exception_write_string("  eip     = ");
    exception_write_hex32(frame->eip);
    exception_write_string("\r\n");
    exception_halt_forever();
}

static void exception_handle_general_protection_fault(const InterruptFrame_t *frame)
{
    const uint32_t error_code = frame->err_code;
    const uint32_t selector_index = error_code >> 3u;
    const uint32_t is_external = error_code & 0x1u;
    const uint32_t references_idt = (error_code >> 1u) & 0x1u;
    const uint32_t references_ldt = (error_code >> 2u) & 0x1u;

    exception_write_string("\r\n\r\n[KERNEL PANIC] #GP General Protection Fault\r\n");
    exception_write_string("  err_code       = ");
    exception_write_hex32(error_code);
    exception_write_string("\r\n");
    exception_write_string("  selector_index = ");
    exception_write_hex32(selector_index);
    exception_write_string("\r\n");
    exception_write_string("  external       = ");
    exception_write_string(is_external ? "yes" : "no");
    exception_write_string("\r\n");
    exception_write_string("  table          = ");
    if (references_idt)
        exception_write_string("IDT");
    else if (references_ldt)
        exception_write_string("LDT");
    else
        exception_write_string("GDT");
    exception_write_string("\r\n");
    exception_write_string("  eip            = ");
    exception_write_hex32(frame->eip);
    exception_write_string("\r\n");
    exception_halt_forever();
}

static void exception_handle_page_fault(const InterruptFrame_t *frame)
{
    const uint32_t error_code = frame->err_code;
    const uint32_t fault_address = cpu_get_page_fault_linear_address();

    exception_write_string("\r\n\r\n[KERNEL PANIC] #PF Page Fault\r\n");
    exception_write_string("  cr2            = ");
    exception_write_hex32(fault_address);
    exception_write_string("\r\n");
    exception_write_string("  err_code       = ");
    exception_write_hex32(error_code);
    exception_write_string("\r\n");
    exception_write_string("  reason         = ");
    exception_write_string((error_code & 0x1u) ? "protection violation" : "non-present page");
    exception_write_string("\r\n");
    exception_write_string("  access         = ");
    exception_write_string((error_code & 0x2u) ? "write" : "read");
    exception_write_string("\r\n");
    exception_write_string("  privilege      = ");
    exception_write_string((error_code & 0x4u) ? "user" : "supervisor");
    exception_write_string("\r\n");
    exception_write_string("  reserved_write = ");
    exception_write_string((error_code & 0x8u) ? "yes" : "no");
    exception_write_string("\r\n");
    exception_write_string("  instruction    = ");
    exception_write_string((error_code & 0x10u) ? "fetch" : "data");
    exception_write_string("\r\n");
    exception_write_string("  eip            = ");
    exception_write_hex32(frame->eip);
    exception_write_string("\r\n");
    exception_halt_forever();
}

void interrupt_exception_initialize(void)
{
    interrupt_service_routine_register_handler(EXCEPTION_VECTOR_DEBUG_EXCEPTION, exception_handle_debug_exception);
    interrupt_service_routine_register_handler(EXCEPTION_VECTOR_BREAKPOINT, exception_handle_breakpoint);
    interrupt_service_routine_register_handler(EXCEPTION_VECTOR_INVALID_OPCODE, exception_handle_invalid_opcode);
    interrupt_service_routine_register_handler(EXCEPTION_VECTOR_DOUBLE_FAULT, exception_handle_double_fault);
    interrupt_service_routine_register_handler(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT,
                                               exception_handle_general_protection_fault);
    interrupt_service_routine_register_handler(EXCEPTION_VECTOR_PAGE_FAULT, exception_handle_page_fault);
}
