#include <kernel/config.h>
#include <kernel/core/console.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/ap_trampoline.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/helpers/pci_helper.h>
#include <kernel/cpu/pci.h>
#include <kernel/cpu/pmm.h>
#include <kernel/diag/telemetry.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/drivers/helpers/keyboard_helper.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/ps2_keyboard.h>
#include <kernel/drivers/tty.h>
#include <kernel/memory/heap.h>

#if defined(LPL_KERNEL_ENABLE_CONSOLE)

/**
 * Every input the console accepts, in one place.
 *
 * `help` used to print its own hand-written list, which is a second answer to
 * "what commands exist" — and it had already drifted: it advertised seven names
 * and omitted `layout us` and `layout fr`, the two that actually change kernel
 * state. The list below is what `help` prints AND what the surface count
 * reports, so the three cannot disagree again.
 */
static const char *const KERNEL_CONSOLE_COMMANDS[] = {
    "help", "stats", "ap", "kbd", "pci", "layout", "layout us", "layout fr", "exit",
};

#define KERNEL_CONSOLE_COMMAND_COUNT (sizeof(KERNEL_CONSOLE_COMMANDS) / sizeof(KERNEL_CONSOLE_COMMANDS[0]))

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

static const char *kernel_console_layout_name(void)
{
    switch (personal_system_2_keyboard_get_layout())
    {
    case PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_FRENCH_AZERTY: return "fr (AZERTY)";
    case PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_UNITED_STATES_QWERTY:
    default: return "us (QWERTY)";
    }
}

static void kernel_console_execute_command(Serial_t *com1, const char *command)
{
    if (!command || !*command)
        return;

    if (kernel_string_equals(command, "help"))
    {
        terminal_write_string("\ncommands:");
        for (uint32_t index = 0u; index < KERNEL_CONSOLE_COMMAND_COUNT; ++index)
        {
            terminal_write_string(index == 0u ? " " : ", ");
            terminal_write_string(KERNEL_CONSOLE_COMMANDS[index]);
        }
        terminal_write_string("\n");
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

    if (kernel_string_equals(command, "pci"))
    {
        peripheral_component_interconnect_scan();
        terminal_write_string("\n[pci] ");
        terminal_write_number((long) peripheral_component_interconnect_get_device_count(), 10u);
        terminal_write_string(" device(s) (full list on serial)\n");
        write_peripheral_component_interconnect_info(com1);
        return;
    }

    if (kernel_string_equals(command, "layout"))
    {
        terminal_write_string("\n[layout] current=");
        terminal_write_string(kernel_console_layout_name());
        terminal_write_string(" (use: layout us | layout fr)\n");
        return;
    }

    if (kernel_string_equals(command, "layout us"))
    {
        personal_system_2_keyboard_set_layout(PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_UNITED_STATES_QWERTY);
        terminal_write_string("\n[layout] switched to us (QWERTY)\n");
        return;
    }

    if (kernel_string_equals(command, "layout fr"))
    {
        personal_system_2_keyboard_set_layout(PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_FRENCH_AZERTY);
        terminal_write_string("\n[layout] switched to fr (AZERTY)\n");
        return;
    }

    if (kernel_string_equals(command, "exit"))
        return;

    terminal_write_string("\nunknown command: ");
    terminal_write_string(command);
    terminal_write_string("\n");
}

#else /* !LPL_KERNEL_ENABLE_CONSOLE */

#    define KERNEL_CONSOLE_COMMAND_COUNT 0u

#endif /* LPL_KERNEL_ENABLE_CONSOLE */

void kernel_console_report_surface(Serial_t *com1)
{
    kernel_telemetry_begin_record(com1, "console");
    kernel_telemetry_write_boolean("present", KERNEL_CONSOLE_IS_COMPILED_IN);
    kernel_telemetry_write_unsigned("commands", (uint32_t) KERNEL_CONSOLE_COMMAND_COUNT);
    kernel_telemetry_end_record();
}

void kernel_console_run_interactive_loop(Serial_t *com1)
{
#if !defined(LPL_KERNEL_ENABLE_CONSOLE)
    for (;;)
        asm volatile("hlt");
#else
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
#endif
}
