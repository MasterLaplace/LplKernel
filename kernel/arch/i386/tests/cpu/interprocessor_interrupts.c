#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/heap.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

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
 * @brief Where a processor found it runs, written by that processor alone when asked.
 */
typedef struct {
    volatile uint32_t logical_slot; /**< What cpu_topology_get_logical_slot gave it. */
    volatile uint32_t heap_domain;  /**< The heap's local domain it got. */
    volatile uint32_t answered;     /**< Non-zero once the fields above are written. */
} InterprocessorInterruptsTestPlace_t;

/** What each slot's processor found, indexed by the slot its own APIC ID register names. */
static InterprocessorInterruptsTestPlace_t interprocessor_interrupts_test_places[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];

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
 * @brief Run on the processor the vector reached: records where it found it runs, in the slot its own register names.
 */
static void interprocessor_interrupts_test_record_place(void *context)
{
    const uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_get_id());

    (void) context;
    if (slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
        return;
    interprocessor_interrupts_test_places[slot].logical_slot = cpu_topology_get_logical_slot();
    interprocessor_interrupts_test_places[slot].heap_domain = kernel_heap_get_server_active_domain();
    interprocessor_interrupts_test_places[slot].answered = 1u;
}

/**
 * @brief Sends @p vector to the processor of @p slot and waits, a bounded while, for @p answer to leave
 *        INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED.
 *
 * @return Whether it did.
 */
static bool interprocessor_interrupts_test_ask(uint32_t slot, uint8_t vector, const volatile uint32_t *answer)
{
    InterruptRequestDeadline_t deadline;

    if (advanced_pic_ipi_send_fixed(cpu_topology_get_apic_id_at_slot(slot), vector, ADVANCED_PIC_IPI_SHORT_NONE) == 0u)
        return false;
    interrupt_request_deadline_start(&deadline, INTERPROCESSOR_INTERRUPTS_TEST_WAIT_MICROSECONDS);
    while (*answer == INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED && !interrupt_request_deadline_passed(&deadline))
        asmutils_pause();
    return *answer != INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED;
}

/**
 * @brief Sends @p vector to the processor of @p slot and waits, a bounded while, for it to record its mode.
 *
 * @return The mode it recorded, or INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED.
 */
static uint32_t interprocessor_interrupts_test_ask_mode(uint32_t slot, uint8_t vector)
{
    interprocessor_interrupts_test_modes[slot] = INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED;
    (void) interprocessor_interrupts_test_ask(slot, vector, &interprocessor_interrupts_test_modes[slot]);
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

/**
 * @brief Every online processor finds the slot its own APIC ID register names, and the heap's local domain follows it.
 *
 * @details Each application processor is sent a vector whose handler asks the topology and the heap where it runs.
 *          The slot used to come from one identifier recorded on the bootstrap processor, so every processor got the
 *          bootstrap processor's slot, and the heap's first domain (#792). The heap gives a slot past its domains the
 *          first one.
 */
KERNEL_TEST(every_processor_finds_its_own_slot)
{
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    const uint32_t online = cpu_topology_get_online_cpu_count();
    const uint32_t domains = kernel_heap_get_server_domain_count();
    uint32_t asked = 0u;
    uint32_t answered = 0u;
    uint32_t in_their_slot = 0u;
    uint32_t in_their_domain = 0u;
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
    if (!device_interrupt_allocate(interprocessor_interrupts_test_record_place, NULL, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        if (slot == here || !cpu_topology_is_logical_slot_online(slot))
            continue;

        const InterprocessorInterruptsTestPlace_t *place = &interprocessor_interrupts_test_places[slot];

        interprocessor_interrupts_test_places[slot].answered = INTERPROCESSOR_INTERRUPTS_TEST_NOT_ANSWERED;
        ++asked;
        if (!interprocessor_interrupts_test_ask(slot, vector, &place->answered))
            continue;
        ++answered;
        in_their_slot += place->logical_slot == slot ? 1u : 0u;
        in_their_domain += place->heap_domain == (slot < domains ? slot : 0u) ? 1u : 0u;
    }
    device_interrupt_release(vector);
    kernel_test_check(test, cpu_topology_get_logical_slot() == here, "the bootstrap processor finds its own slot");
    kernel_test_check(test, asked == online - 1u, "every other online processor was asked");
    kernel_test_check(test, answered == asked, "each answered");
    kernel_test_check(test, in_their_slot == asked, "each found the slot its own register names");
    kernel_test_check(test, in_their_domain == asked, "and the heap's local domain of that slot");
    kernel_test_measure(test, "asked", asked);
    kernel_test_measure(test, "in_their_slot", in_their_slot);
    kernel_test_measure(test, "in_their_domain", in_their_domain);
}

/** Runs of the handler the request test's self-interrupt reaches. */
static volatile uint32_t interprocessor_interrupts_test_requested_runs = 0u;

/**
 * @brief Run on the processor the request test's vector reached: counts the run.
 */
static void interprocessor_interrupts_test_count_run(void *context)
{
    (void) context;
    ++interprocessor_interrupts_test_requested_runs;
}

/**
 * @brief An interrupt sent while interrupts are masked is requested in the local APIC until the processor takes it:
 *        the IRR, not the in-service register, and on a vector past the first 32.
 *
 * @details The parked processors judge a port read by this: an interrupt that ended it is still requested when it
 *          returns, interrupts masked. A self-interrupt stands for the one that would have ended the read.
 */
KERNEL_TEST(a_masked_interrupt_is_requested_until_the_processor_takes_it)
{
    InterruptRequestDeadline_t deadline;
    uint8_t vector = 0u;
    bool requested = false;

    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "the local APIC sends no interprocessor interrupt");
        return;
    }
    if (!device_interrupt_allocate(interprocessor_interrupts_test_count_run, NULL, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }
    interprocessor_interrupts_test_requested_runs = 0u;
    asmutils_disable_interrupts();
    (void) advanced_pic_ipi_send_fixed(0u, vector, ADVANCED_PIC_IPI_SHORT_SELF);
    interrupt_request_deadline_start(&deadline, INTERPROCESSOR_INTERRUPTS_TEST_WAIT_MICROSECONDS);
    while (!(requested = apic_interrupt_requested()) && !interrupt_request_deadline_passed(&deadline))
        asmutils_pause();
    const uint32_t runs_masked = interprocessor_interrupts_test_requested_runs;
    asmutils_enable_interrupts();
    asmutils_no_operation();
    const uint32_t runs_after = interprocessor_interrupts_test_requested_runs;
    const bool requested_after = apic_interrupt_requested();
    device_interrupt_release(vector);

    kernel_test_check(test, requested && runs_masked == 0u, "masked, it is requested and not taken");
    kernel_test_check(test, runs_after == 1u, "unmasked, it is taken once");
    kernel_test_check(test, !requested_after, "and no longer requested");
    kernel_test_measure(test, "vector", vector);
}

/**
 * @brief The fenced write of a model-specific register is MFENCE, LFENCE, WRMSR, right after its arguments are loaded:
 *        the x2APIC interrupt it sends follows every earlier store.
 */
KERNEL_TEST(the_fenced_register_write_fences_before_its_wrmsr)
{
    static const uint8_t fences_then_wrmsr[] = {0x0Fu, 0xAEu, 0xF0u, 0x0Fu, 0xAEu, 0xE8u, 0x0Fu, 0x30u, 0xC3u};
    const uint8_t *routine = (const uint8_t *) (uintptr_t) asmutils_write_model_specific_register_after_a_fence;
    bool found = false;

    for (uint32_t offset = 0u; offset < 16u && !found; ++offset)
        found = memcmp(routine + offset, fences_then_wrmsr, sizeof(fences_then_wrmsr)) == 0;
    kernel_test_check(test, found, "mfence; lfence; wrmsr; ret");
}
