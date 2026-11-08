#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/extended_state.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

KERNEL_TEST_SUITE(extended_state, KERNEL_TEST_STAGE_PUBLISHED);

#define EXTENDED_STATE_TEST_WAIT_MICROSECONDS 100000u     /**< How long a processor may take to answer. */
#define EXTENDED_STATE_TEST_CR0_MP            (1u << 1u)  /**< CR0.MP: WAIT and FWAIT honour TS. */
#define EXTENDED_STATE_TEST_CR0_EM            (1u << 2u)  /**< CR0.EM: x87 instructions fault. */
#define EXTENDED_STATE_TEST_CR0_TS            (1u << 3u)  /**< CR0.TS: the next x87 or SSE instruction faults. */
#define EXTENDED_STATE_TEST_CR0_NE            (1u << 5u)  /**< CR0.NE: x87 errors raise #MF rather than FERR#. */
#define EXTENDED_STATE_TEST_CR4_OSXSAVE       (1u << 18u) /**< CR4.OSXSAVE: XSAVE, XGETBV and XSETBV are enabled. */
#define EXTENDED_STATE_TEST_CPUID_XSAVE       (1u << 26u) /**< CPUID.1:ECX.XSAVE. */
#define EXTENDED_STATE_TEST_X87               0x01u       /**< XCR0 bit 0, which is always set. */
#define EXTENDED_STATE_TEST_SSE               0x02u       /**< XCR0 bit 1. */
#define EXTENDED_STATE_TEST_AVX               0x04u       /**< XCR0 bit 2. */
#define EXTENDED_STATE_TEST_AVX_512           0xE0u       /**< XCR0 bits 5 to 7: opmask, ZMM_Hi256, Hi16_ZMM. */
#define EXTENDED_STATE_TEST_FCW_AFTER_FNINIT                                                                           \
    0x037Fu /**< The x87 control word FNINIT leaves: every exception masked, double extended precision, round to       \
                 nearest (Intel SDM vol. 1, 253665-087US, Table 8-4). */
#define EXTENDED_STATE_TEST_MXCSR_DEFAULT                                                                              \
    0x1F80u /**< The MXCSR after a reset: every exception masked, round to nearest (Intel SDM vol. 1, §10.2.3). */
#define EXTENDED_STATE_TEST_MXCSR_FLAGS                                                                                \
    0x3Fu /**< The six exception flags of the MXCSR, which SSE arithmetic sets and only software clears. */
#define EXTENDED_STATE_TEST_FXSAVE_BYTES 512u    /**< The FXSAVE image. */
#define EXTENDED_STATE_TEST_FXSAVE_FCW   0u      /**< Where the image holds the x87 control word. */
#define EXTENDED_STATE_TEST_FXSAVE_MXCSR 24u     /**< Where it holds the MXCSR. */
#define EXTENDED_STATE_TEST_OTHER_FCW    0x0040u /**< The x87 control word after a power-up (SDM vol. 3, Table 12-1). */
#define EXTENDED_STATE_TEST_OTHER_MXCSR  0x7F80u /**< An MXCSR that rounds toward zero, every exception masked. */

/**
 * @brief The state a processor read in its own registers.
 *
 * @details The FXSAVE image is held here, 16-aligned as FXSAVE needs, rather than on the stack: the kernel's C code
 *          runs on stacks it does not align to 16 bytes.
 */
typedef struct {
    uint8_t image[EXTENDED_STATE_TEST_FXSAVE_BYTES] __attribute__((aligned(16))); /**< The FXSAVE image read. */
    uint32_t control_register_0;                                                  /**< CR0. */
    uint32_t control_register_4;                                                  /**< CR4. */
    uint64_t extended_control_register_0; /**< XCR0, or 0 when CR4.OSXSAVE is clear. */
    uint32_t floating_point_control_word; /**< The x87 control word. */
    uint32_t simd_control_status;         /**< MXCSR. */
    volatile uint32_t answered;           /**< Non-zero once the fields above are written. */
} ExtendedStateTestReading_t;

/** What each slot's processor read, indexed by the slot its own APIC ID register names. */
static ExtendedStateTestReading_t extended_state_test_readings[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];

/**
 * @brief XCR0, which XGETBV reads only once CR4.OSXSAVE is set.
 */
__attribute__((target("xsave"))) static uint64_t extended_state_test_read_xcr0(void)
{
    return __builtin_ia32_xgetbv(0u);
}

/**
 * @brief Reads this processor's CR0, CR4, XCR0, x87 control word and MXCSR into @p reading, the last two through an
 *        FXSAVE image.
 */
__attribute__((target("fxsr"))) static void extended_state_test_read(ExtendedStateTestReading_t *reading)
{
    const uint8_t *const image = reading->image;

    __builtin_ia32_fxsave(reading->image);
    reading->control_register_0 = asmutils_read_control_register_0();
    reading->control_register_4 = asmutils_read_control_register_4();
    reading->extended_control_register_0 =
        (reading->control_register_4 & EXTENDED_STATE_TEST_CR4_OSXSAVE) != 0u ? extended_state_test_read_xcr0() : 0u;
    reading->floating_point_control_word = (uint32_t) image[EXTENDED_STATE_TEST_FXSAVE_FCW] |
                                           ((uint32_t) image[EXTENDED_STATE_TEST_FXSAVE_FCW + 1u] << 8u);
    reading->simd_control_status = (uint32_t) image[EXTENDED_STATE_TEST_FXSAVE_MXCSR] |
                                   ((uint32_t) image[EXTENDED_STATE_TEST_FXSAVE_MXCSR + 1u] << 8u) |
                                   ((uint32_t) image[EXTENDED_STATE_TEST_FXSAVE_MXCSR + 2u] << 16u) |
                                   ((uint32_t) image[EXTENDED_STATE_TEST_FXSAVE_MXCSR + 3u] << 24u);
    reading->answered = 1u;
}

/**
 * @brief Loads @p control_word into the x87 control word and @p simd_control_status into the MXCSR, through an FXSAVE
 *        image of the current state.
 */
__attribute__((target("fxsr"))) static void extended_state_test_load_controls(uint32_t control_word,
                                                                              uint32_t simd_control_status)
{
    static uint8_t image[EXTENDED_STATE_TEST_FXSAVE_BYTES] __attribute__((aligned(16)));

    __builtin_ia32_fxsave(image);
    image[EXTENDED_STATE_TEST_FXSAVE_FCW] = (uint8_t) control_word;
    image[EXTENDED_STATE_TEST_FXSAVE_FCW + 1u] = (uint8_t) (control_word >> 8u);
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        image[EXTENDED_STATE_TEST_FXSAVE_MXCSR + byte] = (uint8_t) (simd_control_status >> (8u * byte));
    __builtin_ia32_fxrstor(image);
}

/**
 * @brief Run on the processor the vector reached: reads its state into the slot its own register names.
 */
static void extended_state_test_record(void *context)
{
    const uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_get_id());

    (void) context;
    if (slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
        extended_state_test_read(&extended_state_test_readings[slot]);
}

/**
 * @brief Whether the processor supports XSAVE, from CPUID leaf 1.
 */
static bool extended_state_test_offers_xsave(void)
{
    uint32_t features_ecx = 0u;

    asmutils_cpuid(0x1u, 0u, NULL, NULL, &features_ecx, NULL);
    return (features_ecx & EXTENDED_STATE_TEST_CPUID_XSAVE) != 0u;
}

/**
 * @brief The XCR0 the kernel enables: of the components CPUID leaf 0xD offers, x87, SSE, AVX and the three AVX-512
 *        ones, each only with those XSETBV requires it with (Intel SDM vol. 1, §13.3).
 */
static uint64_t extended_state_test_expected_xcr0(void)
{
    uint32_t offered = 0u;
    uint32_t components;

    asmutils_cpuid(0xDu, 0u, &offered, NULL, NULL, NULL);
    components = offered & (EXTENDED_STATE_TEST_X87 | EXTENDED_STATE_TEST_SSE | EXTENDED_STATE_TEST_AVX |
                            EXTENDED_STATE_TEST_AVX_512);
    if ((components & EXTENDED_STATE_TEST_SSE) == 0u)
        components &= ~(EXTENDED_STATE_TEST_AVX | EXTENDED_STATE_TEST_AVX_512);
    if ((components & EXTENDED_STATE_TEST_AVX) == 0u ||
        (components & EXTENDED_STATE_TEST_AVX_512) != EXTENDED_STATE_TEST_AVX_512)
        components &= ~EXTENDED_STATE_TEST_AVX_512;
    return (uint64_t) (components | EXTENDED_STATE_TEST_X87);
}

/**
 * @brief Whether @p reading holds the known state: CR0.NE and CR0.MP set, CR0.EM and CR0.TS clear, CR4.OSXSAVE set
 *        exactly when the processor offers XSAVE, XCR0 at @p expected_xcr0, and the x87 control word and the MXCSR's
 *        controls at their initial values: the MXCSR's exception flags record the SSE arithmetic run since.
 */
static bool extended_state_test_is_known(const ExtendedStateTestReading_t *reading, bool xsave, uint64_t expected_xcr0)
{
    const uint32_t floating_point_bits = EXTENDED_STATE_TEST_CR0_MP | EXTENDED_STATE_TEST_CR0_EM |
                                         EXTENDED_STATE_TEST_CR0_TS | EXTENDED_STATE_TEST_CR0_NE;

    return (reading->control_register_0 & floating_point_bits) ==
               (EXTENDED_STATE_TEST_CR0_MP | EXTENDED_STATE_TEST_CR0_NE) &&
           ((reading->control_register_4 & EXTENDED_STATE_TEST_CR4_OSXSAVE) != 0u) == xsave &&
           (!xsave || reading->extended_control_register_0 == expected_xcr0) &&
           reading->floating_point_control_word == EXTENDED_STATE_TEST_FCW_AFTER_FNINIT &&
           (reading->simd_control_status & ~EXTENDED_STATE_TEST_MXCSR_FLAGS) == EXTENDED_STATE_TEST_MXCSR_DEFAULT;
}

/**
 * @brief Sends @p vector to the processor of @p slot and waits, a bounded while, for it to read its state.
 *
 * @return Whether it answered.
 */
static bool extended_state_test_ask(uint32_t slot, uint8_t vector)
{
    InterruptRequestDeadline_t deadline;

    extended_state_test_readings[slot].answered = 0u;
    if (advanced_pic_ipi_send_fixed(cpu_topology_get_apic_id_at_slot(slot), vector, ADVANCED_PIC_IPI_SHORT_NONE) == 0u)
        return false;
    interrupt_request_deadline_start(&deadline, EXTENDED_STATE_TEST_WAIT_MICROSECONDS);
    while (extended_state_test_readings[slot].answered == 0u && !interrupt_request_deadline_passed(&deadline))
        asmutils_pause();
    return extended_state_test_readings[slot].answered != 0u;
}

/**
 * @brief Every online processor runs from the known x87, SSE and AVX state: CR0.NE set, the extended state components
 *        CPUID offers enabled in XCR0, the x87 control word FNINIT leaves and the MXCSR of a reset.
 *
 * @details The bootstrap processor reads its own registers; each application processor is sent a vector whose
 *          handler reads its own. The kernel used to leave CR0.NE clear, so x87 errors went through FERR#, never set
 *          CR4.OSXSAVE, and kept whatever x87 and SSE state the firmware left (#85).
 */
KERNEL_TEST(every_processor_runs_from_the_known_extended_state)
{
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    const bool xsave = extended_state_test_offers_xsave();
    const uint64_t expected_xcr0 = extended_state_test_expected_xcr0();
    const uint32_t online = cpu_topology_get_online_cpu_count();
    uint32_t asked = 0u;
    uint32_t answered = 0u;
    uint32_t known = 0u;
    uint8_t vector = 0u;

    if (here >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
    {
        kernel_test_check(test, false, "the bootstrap processor has a slot");
        return;
    }
    extended_state_test_read(&extended_state_test_readings[here]);
    kernel_test_check(test, extended_state_test_is_known(&extended_state_test_readings[here], xsave, expected_xcr0),
                      "the bootstrap processor runs from the known state");
    kernel_test_measure(test, "cr0", extended_state_test_readings[here].control_register_0);
    kernel_test_measure(test, "xcr0", (uint32_t) extended_state_test_readings[here].extended_control_register_0);
    kernel_test_measure(test, "expected_xcr0", (uint32_t) expected_xcr0);
    kernel_test_measure(test, "xsave", xsave ? 1u : 0u);
    kernel_test_measure(test, "fcw", extended_state_test_readings[here].floating_point_control_word);
    kernel_test_measure(test, "mxcsr", extended_state_test_readings[here].simd_control_status);
    if (online < 2u || !advanced_pic_ipi_is_ready())
        return;
    if (!device_interrupt_allocate(extended_state_test_record, NULL, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        if (slot == here || !cpu_topology_is_logical_slot_online(slot))
            continue;
        ++asked;
        if (!extended_state_test_ask(slot, vector))
            continue;
        ++answered;
        known += extended_state_test_is_known(&extended_state_test_readings[slot], xsave, expected_xcr0) ? 1u : 0u;
    }
    device_interrupt_release(vector);
    kernel_test_check(test, asked == online - 1u, "every other online processor was asked");
    kernel_test_check(test, answered == asked, "each answered");
    kernel_test_check(test, known == asked, "each runs from the known state");
    kernel_test_measure(test, "asked", asked);
    kernel_test_measure(test, "known", known);
}

/**
 * @brief Initialising a processor that holds another x87 control word and another MXCSR gives it the known ones: an
 *        application processor keeps across INIT whatever it held, and the firmware's state is not the known one.
 *
 * @details The control word of a power-up, 0x0040, unmasks every x87 exception and rounds to single precision. The
 *          bootstrap processor is initialised once more, then given back the controls it held before.
 */
KERNEL_TEST(initialising_a_processor_resets_its_x87_and_simd_controls)
{
    static ExtendedStateTestReading_t before;
    static ExtendedStateTestReading_t perturbed;
    static ExtendedStateTestReading_t after;

    extended_state_test_read(&before);
    extended_state_test_load_controls(EXTENDED_STATE_TEST_OTHER_FCW, EXTENDED_STATE_TEST_OTHER_MXCSR);
    extended_state_test_read(&perturbed);
    extended_state_initialize_on_this_processor();
    extended_state_test_read(&after);
    extended_state_test_load_controls(before.floating_point_control_word, before.simd_control_status);

    kernel_test_check(test,
                      perturbed.floating_point_control_word == EXTENDED_STATE_TEST_OTHER_FCW &&
                          perturbed.simd_control_status == EXTENDED_STATE_TEST_OTHER_MXCSR,
                      "the processor held the other controls");
    kernel_test_check(test, after.floating_point_control_word == EXTENDED_STATE_TEST_FCW_AFTER_FNINIT,
                      "it gets the control word FNINIT leaves");
    kernel_test_check(test, after.simd_control_status == EXTENDED_STATE_TEST_MXCSR_DEFAULT, "and the MXCSR of a reset");
    kernel_test_check(test,
                      after.control_register_0 == before.control_register_0 &&
                          after.control_register_4 == before.control_register_4 &&
                          after.extended_control_register_0 == before.extended_control_register_0,
                      "its control registers and XCR0 stay as they were");
}
