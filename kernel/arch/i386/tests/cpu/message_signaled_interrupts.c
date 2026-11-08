#include <kernel/cpu/msi.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(message_signaled_interrupts, KERNEL_TEST_STAGE_PUBLISHED);

/**
 * @brief A message names its local APIC in bits 19:12 of 0xFEExxxxx and carries its vector as data, edge-triggered
 *        and fixed; the broadcast 0xFF, an identifier past it, which needs interrupt remapping, and the vectors
 *        outside 0x10-0xFE are refused.
 */
KERNEL_TEST(a_message_names_its_core_and_carries_its_vector)
{
    uint32_t address = 0u;
    uint32_t data = 0u;

    kernel_test_check(test, message_signaled_interrupt_compose(0x12u, 0x51u, &address, &data),
                      "APIC 0x12, vector 0x51 composes");
    kernel_test_check(test, address == 0xFEE12000u, "its address is 0xFEE12000");
    kernel_test_check(test, data == 0x51u, "its data is the vector alone: fixed, edge, asserted");
    kernel_test_check(test, message_signaled_interrupt_compose(0xFEu, 0x10u, &address, &data) && address == 0xFEEFE000u,
                      "APIC 0xFE and vector 0x10, the last destination and the first vector, compose");
    kernel_test_check(test, message_signaled_interrupt_compose(0u, 0xFEu, &address, &data) && data == 0xFEu,
                      "vector 0xFE, the last one, composes");
    address = 0u;
    kernel_test_check(test, !message_signaled_interrupt_compose(0xFFu, 0x51u, &address, &data) && address == 0u,
                      "APIC 0xFF, the broadcast, is refused, nothing written");
    kernel_test_check(test, !message_signaled_interrupt_compose(0x100u, 0x51u, &address, &data),
                      "APIC 0x100 is refused");
    kernel_test_check(test, !message_signaled_interrupt_compose(0u, 0x0Fu, &address, &data),
                      "vector 0x0F, a reserved one, is refused");
    kernel_test_check(test, !message_signaled_interrupt_compose(0u, 0xFFu, &address, &data), "vector 0xFF is refused");
}
