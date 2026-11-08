#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(interprocessor_interrupts, KERNEL_TEST_STAGE_PUBLISHED);

#define INTERPROCESSOR_INTERRUPTS_TEST_WAIT_MICROSECONDS  100000u /**< How long a processor may take to answer. */
#define INTERPROCESSOR_INTERRUPTS_TEST_APIC_BASE          0x1Bu   /**< The IA32_APIC_BASE model-specific register. */
#define INTERPROCESSOR_INTERRUPTS_TEST_APIC_BASE_EXTENDED (1ull << 10u) /**< Its EXTD bit: x2APIC mode. */
#define INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED       0u            /**< A slot whose processor has not answered. */
#define INTERPROCESSOR_INTERRUPTS_TEST_XAPIC              1u            /**< It answered from xAPIC mode. */
#define INTERPROCESSOR_INTERRUPTS_TEST_X2APIC             2u            /**< It answered from x2APIC mode. */

/** The mode each slot's processor read in its own IA32_APIC_BASE, written by that processor alone. */
static volatile uint32_t interprocessor_interrupts_test_modes[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC] = {0u};

/**
 * @brief The mode this processor's IA32_APIC_BASE holds.
 */
static uint32_t interprocessor_interrupts_test_own_mode(void)
{
    return (asmutils_read_model_specific_register(INTERPROCESSOR_INTERRUPTS_TEST_APIC_BASE) &
            INTERPROCESSOR_INTERRUPTS_TEST_APIC_BASE_EXTENDED) != 0u ?
               INTERPROCESSOR_INTERRUPTS_TEST_X2APIC :
               INTERPROCESSOR_INTERRUPTS_TEST_XAPIC;
}

/**
 * @brief Run on the processor the vector reached: records its mode in its own slot.
 */
static void interprocessor_interrupts_test_record_mode(void *context)
{
    const uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_get_id());

    (void) context;
    if (slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
        interprocessor_interrupts_test_modes[slot] = interprocessor_interrupts_test_own_mode();
}

/**
 * @brief Sends @p vector to the processor of @p slot and waits, a bounded while, for it to record its mode.
 *
 * @return The mode it recorded, or INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED.
 */
static uint32_t interprocessor_interrupts_test_ask_mode(uint32_t slot, uint8_t vector)
{
    InterruptRequestDeadline_t deadline;

    interprocessor_interrupts_test_modes[slot] = INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED;
    if (advanced_pic_ipi_send_fixed(cpu_topology_get_apic_id_at_slot(slot), vector, ADVANCED_PIC_IPI_SHORT_NONE) == 0u)
        return INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED;
    interrupt_request_deadline_start(&deadline, INTERPROCESSOR_INTERRUPTS_TEST_WAIT_MICROSECONDS);
    while (interprocessor_interrupts_test_modes[slot] == INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED &&
           !interrupt_request_deadline_passed(&deadline))
        asmutils_pause();
    return interprocessor_interrupts_test_modes[slot];
}

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

/**
 * @brief Every online processor runs its local APIC in the mode the bootstrap processor chose, read by each in its
 *        own IA32_APIC_BASE: the kernel addresses them all in one mode.
 *
 * @details Each application processor is sent a vector whose handler reads its register. One left in another mode
 *          reads its identifier wrong, or takes no fixed interrupt at all, and answers nothing.
 */
KERNEL_TEST(every_processor_runs_the_mode_the_bootstrap_processor_chose)
{
    const uint32_t chosen =
        apic_is_x2apic_active() ? INTERPROCESSOR_INTERRUPTS_TEST_X2APIC : INTERPROCESSOR_INTERRUPTS_TEST_XAPIC;
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    const uint32_t online = cpu_topology_get_online_cpu_count();
    uint32_t asked = 0u;
    uint32_t answered = 0u;
    uint32_t agreeing = 0u;
    uint8_t vector = 0u;

    if (online < 2u)
    {
        kernel_test_skip(test, "no other processor is online: boot with -smp 2");
        return;
    }
    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "the local APIC sends no interprocessor interrupt");
        return;
    }
    if (!device_interrupt_allocate(interprocessor_interrupts_test_record_mode, NULL, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        if (slot == here || !cpu_topology_is_logical_slot_online(slot))
            continue;

        const uint32_t mode = interprocessor_interrupts_test_ask_mode(slot, vector);

        ++asked;
        answered += mode != INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED ? 1u : 0u;
        agreeing += mode == chosen ? 1u : 0u;
    }
    device_interrupt_release(vector);
    kernel_test_check(test, interprocessor_interrupts_test_own_mode() == chosen,
                      "the bootstrap processor runs the mode it chose");
    kernel_test_check(test, asked == online - 1u, "every other online processor was asked");
    kernel_test_check(test, answered == asked, "each answered");
    kernel_test_check(test, agreeing == asked, "each runs the bootstrap processor's mode");
    kernel_test_measure(test, "asked", asked);
    kernel_test_measure(test, "agreeing", agreeing);
    kernel_test_measure(test, "x2apic", chosen == INTERPROCESSOR_INTERRUPTS_TEST_X2APIC ? 1u : 0u);
}
