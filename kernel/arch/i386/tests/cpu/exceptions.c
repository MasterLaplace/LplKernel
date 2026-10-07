#include <kernel/cpu/exception_trigger.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(exceptions, KERNEL_TEST_STAGE_BOOTED);

/** Why every test of this suite only runs when selected by name. */
#define EXCEPTION_TEST_HALTS "raises a CPU exception, which the kernel reports and halts on"

/**
 * @brief Records that the exception the test raised came back instead of halting the machine.
 */
static void exception_test_came_back(KernelTest_t *test)
{
    kernel_test_check(test, false, "raising the exception reports it and halts the machine");
}

/**
 * @brief Divides by zero: the kernel reports a division error (#DE) and halts.
 */
KERNEL_TEST_MANUAL(division_error, EXCEPTION_TEST_HALTS)
{
    volatile int zero = 0;
    volatile int quotient = 1 / zero;

    (void) quotient;
    exception_test_came_back(test);
}

/**
 * @brief Single-steps one instruction: the kernel reports a debug exception (#DB) and halts.
 */
KERNEL_TEST_MANUAL(debug_exception, EXCEPTION_TEST_HALTS)
{
    exception_trigger_single_step();
    exception_test_came_back(test);
}

/**
 * @brief Executes INT3: the kernel reports a breakpoint (#BP) and halts.
 */
KERNEL_TEST_MANUAL(breakpoint, EXCEPTION_TEST_HALTS)
{
    exception_trigger_breakpoint();
    exception_test_came_back(test);
}

/**
 * @brief Executes UD2: the kernel reports an invalid opcode (#UD) and halts.
 */
KERNEL_TEST_MANUAL(invalid_opcode, EXCEPTION_TEST_HALTS)
{
    exception_trigger_invalid_opcode();
    exception_test_came_back(test);
}

/**
 * @brief Reads memory through a null data segment: the kernel reports a general protection fault
 *        (#GP) and halts.
 */
KERNEL_TEST_MANUAL(general_protection, EXCEPTION_TEST_HALTS)
{
    exception_trigger_general_protection();
    exception_test_came_back(test);
}

/**
 * @brief Reads an address nothing maps: the kernel reports a page fault (#PF) and halts.
 */
KERNEL_TEST_MANUAL(page_fault, EXCEPTION_TEST_HALTS)
{
    volatile uint32_t *const unmapped = (volatile uint32_t *) 0xE0001000u;
    volatile uint32_t value = *unmapped;

    (void) value;
    exception_test_came_back(test);
}

/**
 * @brief Delivers vector 8 with a software interrupt: the kernel reports a double fault (#DF) and
 *        halts.
 */
KERNEL_TEST_MANUAL(double_fault, EXCEPTION_TEST_HALTS)
{
    exception_trigger_double_fault_vector();
    exception_test_came_back(test);
}
