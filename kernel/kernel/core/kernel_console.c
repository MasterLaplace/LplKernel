#include <kernel/core/kernel_console.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/ap_trampoline.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/drivers/helpers/keyboard_helper.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/memory/heap.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/tty.h>
#include <kernel/config.h>

static uint8_t kernel_string_equals(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs)
        return 0u;

    while (*lhs && *rhs)
    {
        if (*lhs != *rhs)
            return 0u;
        ++lhs;
        ++rhs;
    }

    return (uint8_t) (*lhs == '\0' && *rhs == '\0');
}

static void kernel_console_print_prompt(void) { terminal_write_string("\n> "); }

static void kernel_console_execute_command(Serial_t *com1, const char *command)
{
    if (!command || !*command)
        return;

    if (kernel_string_equals(command, "help"))
    {
        terminal_write_string("\ncommands: help, stats, ap, kbd, exit\n");
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: cmd help\n");
        return;
    }

    if (kernel_string_equals(command, "stats"))
    {
        terminal_write_string("\n[stats] heap strategy=");
        terminal_write_string(kernel_heap_get_strategy_name());
        terminal_write_string(", pmm free pages=");
        terminal_write_number((long) physical_memory_manager_get_free_page_count(), 10u);
        terminal_write_string("\n");
        return;
    }

    if (kernel_string_equals(command, "ap"))
    {
        terminal_write_string("\n[ap] discovered=");
        terminal_write_number((long) cpu_topology_get_discovered_cpu_count(), 10u);
        terminal_write_string(", online=");
        terminal_write_number((long) cpu_topology_get_online_cpu_count(), 10u);
        terminal_write_string(", reported=");
        terminal_write_number((long) application_processor_startup_get_reported_online_count(), 10u);
        terminal_write_string(", startup_attempts=");
        terminal_write_number((long) advanced_pic_ipi_get_startup_sequence_attempt_count(), 10u);
        terminal_write_string(", startup_success=");
        terminal_write_number((long) advanced_pic_ipi_get_startup_sequence_success_count(), 10u);
        terminal_write_string(", tramp_ack_ok=");
        terminal_write_number((long) application_processor_trampoline_get_ack_success_count(), 10u);
        terminal_write_string(", tramp_ack_to=");
        terminal_write_number((long) application_processor_trampoline_get_ack_timeout_count(), 10u);
        terminal_write_string("\n");
        return;
    }

    if (kernel_string_equals(command, "kbd"))
    {
        terminal_write_string("\n[kbd] irq=");
        terminal_write_number((long) keyboard_get_irq_count(), 10u);
        terminal_write_string(", printable=");
        terminal_write_number((long) keyboard_get_printable_count(), 10u);
        terminal_write_string(", pending=");
        terminal_write_number((long) keyboard_get_pending_char_count(), 10u);
        terminal_write_string(", dropped=");
        terminal_write_number((long) keyboard_get_dropped_char_count(), 10u);
        terminal_write_string("\n");
        return;
    }

    if (kernel_string_equals(command, "exit"))
        return;

    terminal_write_string("\nunknown command: ");
    terminal_write_string(command);
    terminal_write_string("\n");
}

void kernel_console_run_interactive_loop(Serial_t *com1)
{
    static const uint8_t KEY_ECHAP = 27u;
    static const uint32_t KERNEL_CONSOLE_COMMAND_MAX = 63u;
    char command_buffer[64] = {0};
    uint32_t command_length = 0u;
    uint8_t done = 0u;

    if (!framebuffer_available())
        kernel_console_print_prompt();

    while (!done)
    {
        char key_char = 0;
        uint8_t serial_char = 0u;
        uint8_t incoming = 0u;

        if (keyboard_try_pop_char(&key_char))
            incoming = (uint8_t) key_char;
        else if (serial_try_read_char(com1, &serial_char))
            incoming = serial_char;

        if (incoming)
        {
            if (incoming == KEY_ECHAP)
            {
                done = 1u;
                continue;
            }

            if (!framebuffer_available())
            {
                if (incoming == '\r' || incoming == '\n')
                {
                    terminal_putchar('\n');
                    command_buffer[command_length] = '\0';
                    if (command_length > 0u)
                    {
                        kernel_console_execute_command(com1, command_buffer);
                        if (kernel_string_equals(command_buffer, "exit"))
                        {
                            done = 1u;
                            continue;
                        }
                    }

                    command_length = 0u;
                    command_buffer[0] = '\0';
                    kernel_console_print_prompt();
                    continue;
                }

                if (incoming == '\b' || incoming == 127u)
                {
                    if (command_length > 0u)
                    {
                        --command_length;
                        command_buffer[command_length] = '\0';
                        terminal_putchar('\b');
                    }
                    continue;
                }

                if (incoming >= 32u && incoming <= 126u)
                {
                    if (command_length < KERNEL_CONSOLE_COMMAND_MAX)
                    {
                        command_buffer[command_length++] = (char) incoming;
                        terminal_putchar((char) incoming);
                    }
                    continue;
                }
            }

            continue;
        }

        asm volatile("hlt");
    }
}
