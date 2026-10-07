#include <kernel/cpu/isr.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/cpu/ring3.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(ring3, KERNEL_TEST_STAGE_INITIALIZATION);

/** Where the user code page is mapped, then the user stack page right after it. */
#define RING3_TEST_CODE_ADDRESS  0x00400000u
#define RING3_TEST_STACK_ADDRESS 0x00401000u

/** The user code: `int 0x80` then `jmp $`, so the system call is the only way out. */
static const uint8_t ring3_test_user_code[] = {0xCDu, 0x80u, 0xEBu, 0xFEu};

/**
 * @brief Receives the system call from user mode, reports it and halts: user mode has no way back.
 */
static void ring3_test_system_call_handler(const InterruptFrame_t *frame)
{
    (void) frame;
    serial_write_string(&com1, "    # ring3.system_call_from_user_mode: int 0x80 received from user mode\n");
    asmutils_disable_interrupts();
    for (;;)
        asmutils_halt();
}

/**
 * @brief Enters user mode on a page of its own, and the code there reaches the kernel through int 0x80.
 *
 * @note Manual (#411): user mode never returns, and the handler halts the machine once the call has
 *       been seen. Success is the handler's diagnostic line; the test itself never reports.
 */
KERNEL_TEST_MANUAL(system_call_from_user_mode, "enters user mode and halts once the system call arrives (#411)")
{
    PageDirectoryEntry_t directory_flags = {0};
    PageTableEntry_t table_flags = {0};
    const uint32_t code_page = physical_memory_manager_page_frame_allocate();
    const uint32_t stack_page = physical_memory_manager_page_frame_allocate();

    directory_flags.present = 1u;
    directory_flags.read_write = 1u;
    directory_flags.user_supervisor = 1u;
    table_flags.present = 1u;
    table_flags.read_write = 1u;
    table_flags.user_supervisor = 1u;

    const bool mapped = code_page && stack_page &&
                        paging_map_page(RING3_TEST_CODE_ADDRESS, code_page, directory_flags, table_flags) &&
                        paging_map_page(RING3_TEST_STACK_ADDRESS, stack_page, directory_flags, table_flags);

    if (!kernel_test_check(test, mapped, "a user code page and a user stack page can be mapped"))
        return;

    uint8_t *const user_code = (uint8_t *) RING3_TEST_CODE_ADDRESS;

    for (uint32_t index = 0u; index < sizeof(ring3_test_user_code); ++index)
        user_code[index] = ring3_test_user_code[index];

    interrupt_service_routine_register_handler(0x80u, ring3_test_system_call_handler);
    ring3_enter(RING3_TEST_CODE_ADDRESS, RING3_TEST_STACK_ADDRESS + PAGE_SIZE);
    kernel_test_check(test, false, "user mode does not return to the kernel");
}
