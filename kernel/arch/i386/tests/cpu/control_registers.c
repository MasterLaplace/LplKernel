#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/section_protection.h>
#include <kernel/testing/test.h>

#define CONTROL_REGISTERS_TEST_CACHE_DISABLED 0x60000000u /**< CR0.CD and CR0.NW, both clear when caching is on. */
#define CONTROL_REGISTERS_TEST_SSE_ENABLED    0x00000600u /**< CR4.OSFXSR and CR4.OSXMMEXCPT, set by boot.S. */

KERNEL_TEST_SUITE(control_registers, KERNEL_TEST_STAGE_PUBLISHED);

/**
 * @brief The bootstrap processor runs with write protection, caching and SSE, whatever the number of
 *        processors.
 *
 * @details The other processors copy its CR0 and CR4, so a fault here would reach them all and leave
 *          them equal to it.
 */
KERNEL_TEST(the_bootstrap_processor_protects_caches_and_enables_sse)
{
    const uint32_t control_register_0 = asmutils_read_control_register_0();
    const uint32_t control_register_4 = asmutils_read_control_register_4();

    kernel_test_check(test, kernel_section_protection_write_protect_is_enabled(), "CR0.WP is set");
    kernel_test_check(test, (control_register_0 & CONTROL_REGISTERS_TEST_CACHE_DISABLED) == 0u,
                      "CR0.CD and CR0.NW are clear");
    kernel_test_check(test,
                      (control_register_4 & CONTROL_REGISTERS_TEST_SSE_ENABLED) == CONTROL_REGISTERS_TEST_SSE_ENABLED,
                      "CR4.OSFXSR and CR4.OSXMMEXCPT are set");
    kernel_test_measure(test, "control_register_0", control_register_0);
    kernel_test_measure(test, "control_register_4", control_register_4);
}

/**
 * @brief Every other processor runs with the bootstrap processor's CR0 and CR4.
 *
 * @details Each AP read both registers on itself as it entered C. INIT leaves CR0 at 60000010H with
 *          CD and NW unchanged, and CR4 at 0 (Intel SDM vol. 3, Table 12-1): an AP that kept them ran
 *          with WP clear, CD and NW set and SSE disabled, and nothing said so.
 */
KERNEL_TEST(every_processor_runs_with_the_bootstrap_processors_control_registers)
{
    const uint32_t online = cpu_topology_get_online_cpu_count();

    if (online < 2u)
    {
        kernel_test_skip(test, "no other processor is online");
        return;
    }

    const uint32_t differ = application_processor_startup_count_control_register_mismatches();

    kernel_test_check(test, application_processor_startup_count_control_register_records() == online - 1u,
                      "every other online processor read its control registers");
    kernel_test_check(test, differ == 0u, "none differs from the bootstrap processor's CR0 and CR4");
    kernel_test_measure(test, "online", online);
    kernel_test_measure(test, "differ", differ);
}
