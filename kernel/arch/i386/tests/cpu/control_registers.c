#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/section_protection.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(control_registers, KERNEL_TEST_STAGE_PUBLISHED);

/**
 * @brief Every other processor runs with the bootstrap processor's CR0 and CR4, so write protection
 *        holds on all of them.
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

    kernel_test_check(test, kernel_section_protection_write_protect_is_enabled(),
                      "the bootstrap processor sets CR0.WP");
    kernel_test_check(test, application_processor_startup_count_control_register_records() == online - 1u,
                      "every other online processor read its control registers");
    kernel_test_check(test, differ == 0u, "none differs from the bootstrap processor's CR0 and CR4");
    kernel_test_measure(test, "online", online);
    kernel_test_measure(test, "differ", differ);
    kernel_test_measure(test, "control_register_0", asmutils_read_control_register_0());
    kernel_test_measure(test, "control_register_4", asmutils_read_control_register_4());
}
