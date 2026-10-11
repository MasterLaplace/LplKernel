#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(shootdowns, KERNEL_TEST_STAGE_PUBLISHED);

/**
 * @brief Every TLB shootdown of the boot was answered by every other processor, and with more than
 *        one online, shootdowns were sent at all.
 *
 * @details A shootdown that waited out its spin limit left a processor that may still map a page the
 *          kernel has given away.
 */
KERNEL_TEST(every_shootdown_was_answered)
{
    const uint32_t online = cpu_topology_get_online_cpu_count();

    kernel_test_check(test, advanced_pic_ipi_get_tlb_shootdown_timeout_count() == 0u, "no shootdown timed out");
    kernel_test_check(test, online < 2u || advanced_pic_ipi_get_tlb_shootdown_broadcast_count() > 0u,
                      "with other processors online, shootdowns reached them");
    kernel_test_measure(test, "online", online);
    kernel_test_measure(test, "shootdowns", advanced_pic_ipi_get_tlb_shootdown_broadcast_count());
}
