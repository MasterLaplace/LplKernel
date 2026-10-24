#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(interprocessor_interrupts, KERNEL_TEST_STAGE_PUBLISHED);

/**
 * @brief In xAPIC mode, an identifier past 8 bits gets no interrupt: the destination field holds
 *        8 bits, and what fits of a longer identifier names another processor.
 *
 * @details 0x1FE fits as 0xFE, which no processor of a small machine holds. Sent, the INIT reached
 *          whichever processor holds 0xFE.
 */
KERNEL_TEST(an_identifier_past_eight_bits_gets_no_interrupt_in_xapic_mode)
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

    const uint32_t attempts_before = advanced_pic_ipi_get_init_attempt_count();
    const uint8_t sent = advanced_pic_ipi_send_init(past_eight_bits);

    kernel_test_check(test, sent == 0u, "the INIT is refused");
    kernel_test_check(test, advanced_pic_ipi_get_init_attempt_count() == attempts_before, "and never attempted");
}
