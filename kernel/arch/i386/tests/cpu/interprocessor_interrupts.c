#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(interprocessor_interrupts, KERNEL_TEST_STAGE_PUBLISHED);

/**
 * @brief In xAPIC mode, an identifier the destination field cannot hold alone gets no interrupt:
 *        past 8 bits, what fits names another processor, and 0xFF names every one.
 *
 * @details 0x1FE fits as 0xFE, which no processor of a small machine holds. Sent, an INIT or a
 *          STARTUP reached whichever processor holds 0xFE. Only 0x1FE is sent: a broadcast INIT would
 *          stop every processor, the sender included, rather than fail a check.
 */
KERNEL_TEST(an_identifier_xapic_cannot_address_gets_no_interrupt)
{
    const uint32_t past_eight_bits = 0x1FEu;

    if (apic_is_x2apic_active())
    {
        kernel_test_skip(test, "x2APIC mode addresses 32-bit identifiers");
        return;
    }
    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "no local APIC sends interrupts");
        return;
    }

    const uint32_t init_attempts_before = advanced_pic_ipi_get_init_attempt_count();
    const uint32_t startup_attempts_before = advanced_pic_ipi_get_sipi_attempt_count();

    kernel_test_check(test, advanced_pic_ipi_can_address(0xFEu), "0xFE is addressed");
    kernel_test_check(test, !advanced_pic_ipi_can_address(0xFFu), "0xFF, the broadcast, is not");
    kernel_test_check(test, !advanced_pic_ipi_can_address(past_eight_bits), "nor is 0x1FE");
    kernel_test_check(test, advanced_pic_ipi_send_init(past_eight_bits) == 0u, "an INIT for 0x1FE is refused");
    kernel_test_check(test, advanced_pic_ipi_send_sipi(past_eight_bits, 0x08u) == 0u, "a STARTUP for 0x1FE is refused");
    kernel_test_check(test, advanced_pic_ipi_send_fixed(past_eight_bits, 0xF0u, ADVANCED_PIC_IPI_SHORT_NONE) == 0u,
                      "a fixed interrupt for 0x1FE is refused");
    kernel_test_check(test,
                      advanced_pic_ipi_get_init_attempt_count() == init_attempts_before &&
                          advanced_pic_ipi_get_sipi_attempt_count() == startup_attempts_before,
                      "none was attempted");
}

/**
 * @brief In x2APIC mode, every identifier but the broadcast is addressed alone.
 *
 * @details 0xFFFFFFFF is also what an empty slot of the topology holds: sent, an INIT reached every
 *          processor, the sender included, so it is never sent here.
 */
KERNEL_TEST(the_x2apic_broadcast_is_not_addressed)
{
    if (!apic_is_x2apic_active())
    {
        kernel_test_skip(test, "the local APIC is in xAPIC mode");
        return;
    }

    kernel_test_check(test, advanced_pic_ipi_can_address(0x100u), "0x100 is addressed");
    kernel_test_check(test, advanced_pic_ipi_can_address(0xFFFFFFFEu), "so is 0xFFFFFFFE");
    kernel_test_check(test, !advanced_pic_ipi_can_address(0xFFFFFFFFu), "0xFFFFFFFF, the broadcast, is not");
}
