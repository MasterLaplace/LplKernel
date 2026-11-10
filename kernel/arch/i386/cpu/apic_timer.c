#include <kernel/cpu/apic_timer.h>

#define APIC_CPUID_LEAF_FEATURES                       0x00000001u
#define APIC_CPUID_EDX_BIT_APIC                        (1u << 9u)
#define APIC_CPUID_ECX_BIT_TIMESTAMP_COUNTER_DEADLINE  (1u << 24u)
#define APIC_CPUID_LEAF_EXTENDED_MAXIMUM               0x80000000u
#define APIC_CPUID_LEAF_ADVANCED_POWER_MANAGEMENT      0x80000007u
#define APIC_CPUID_EDX_BIT_INVARIANT_TIMESTAMP_COUNTER (1u << 8u)
#define APIC_CPUID_LEAF_BASIC_MAXIMUM                  0x00000000u
#define APIC_CPUID_LEAF_THERMAL_AND_POWER              0x00000006u
#define APIC_CPUID_EAX_BIT_ALWAYS_RUNNING_TIMER        (1u << 2u)

#define IA32_APIC_BASE_MSR                0x0000001Bu
#define IA32_APIC_BASE_BSP_BIT            (1ull << 8u)
#define IA32_APIC_BASE_X2APIC_MODE_BIT    (1ull << 10u)
#define IA32_APIC_BASE_ENABLE_BIT         (1ull << 11u)
#define IA32_APIC_BASE_PHYSICAL_BASE_MASK 0xFFFFF000ull
#define IA32_TSC_DEADLINE_MSR             0x000006E0u

#define APIC_TIMER_MMIO_VIRTUAL_BASE 0xFFB00000u

/** @name The local timer's LVT entry (Intel SDM vol. 3A, Figure 13-8 and Table 13-2)
 *  @{ */
#define APIC_TIMER_VECTOR                         32u         /**< The tick's vector, IRQ0 remapped. */
#define APIC_TIMER_LVT_MASKED                     (1u << 16u) /**< Mask bit. */
#define APIC_TIMER_LVT_PERIODIC                   (1u << 17u) /**< Bits 18:17 = 01b. */
#define APIC_TIMER_LVT_TIMESTAMP_COUNTER_DEADLINE (2u << 17u) /**< Bits 18:17 = 10b. */
#define APIC_TIMER_LVT_PARKED                     (APIC_TIMER_LVT_MASKED | 0xFEu) /**< Masked, one-shot. */
#define APIC_TIMER_LVT_UNWRITTEN                  0xFFFFFFFFu /**< Bits 31:19 are reserved: no write is this. */
/** @} */

static const char *advanced_pic_timer_backend_state_name = "apic-probe-only";
static uint32_t advanced_pic_timer_local_apic_physical_base = 0u;
static uint8_t advanced_pic_timer_local_apic_is_bootstrap_processor = 0u;
static uint8_t advanced_pic_timer_local_apic_mmio_mapped = 0u;
static uint32_t advanced_pic_timer_local_apic_version_register = 0u;
static uint32_t advanced_pic_timer_local_apic_calibrated_frequency_hz = 0u;
static uint8_t advanced_pic_timer_local_apic_periodic_mode_enabled = 0u;
static uint32_t advanced_pic_timer_calibration_local_timer_counts = 0u;
static uint64_t advanced_pic_timer_calibration_timestamp_counter_cycles = 0u;
static uint64_t advanced_pic_timer_timestamp_counter_calibrated_frequency_hz = 0u;
static uint8_t advanced_pic_timer_timestamp_counter_deadlines = 0u;
static uint8_t advanced_pic_timer_invariant_timestamp_counter = 0u;
static uint8_t advanced_pic_timer_always_running = 0u;
/**
 * The timer's LVT entry in force, which spares rewriting it on every arming. It holds only because this backend is the
 * one writer of the bootstrap processor's entry: another writer, a resume from sleep or an arming on another core,
 * would make it lie in silence.
 */
static uint32_t advanced_pic_timer_lvt_timer = APIC_TIMER_LVT_UNWRITTEN;
static uint64_t advanced_pic_timer_armed_deadline = 0u;

static uint8_t advanced_pic_timer_is_reachable(void)
{
    return (uint8_t) ((advanced_pic_timer_local_apic_mmio_mapped || apic_is_x2apic_active()) ? 1u : 0u);
}

/**
 * @brief Writes @p value to the timer's LVT entry, and keeps it as the entry in force.
 * @param value The entry.
 */
static void advanced_pic_timer_write_lvt(uint32_t value)
{
    apic_write(LAPIC_REG_LVT_TIMER, value);
    advanced_pic_timer_lvt_timer = value;
}

/**
 * @brief Writes @p value to the timer's LVT entry unless it is already the entry in force.
 * @param value The entry.
 * @return 1 when it was written.
 */
static uint8_t advanced_pic_timer_select_lvt(uint32_t value)
{
    if (advanced_pic_timer_lvt_timer == value)
        return 0u;
    advanced_pic_timer_write_lvt(value);
    return 1u;
}

/**
 * @brief CPUID.80000007H:EDX[8], once CPUID.80000000H says the leaf exists.
 * @return 1 when the invariant timestamp counter is promised.
 */
static uint8_t advanced_pic_timer_read_invariant_timestamp_counter(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(APIC_CPUID_LEAF_EXTENDED_MAXIMUM, 0u, &eax, &ebx, &ecx, &edx);
    if (eax < APIC_CPUID_LEAF_ADVANCED_POWER_MANAGEMENT)
        return 0u;
    asmutils_cpuid(APIC_CPUID_LEAF_ADVANCED_POWER_MANAGEMENT, 0u, &eax, &ebx, &ecx, &edx);
    return (uint8_t) (((edx & APIC_CPUID_EDX_BIT_INVARIANT_TIMESTAMP_COUNTER) != 0u) ? 1u : 0u);
}

/**
 * @brief Reads CPUID.06H:EAX[2], ARAT: the local timer keeps counting in every C-state.
 * @return 1 when the bit is set, 0 when it is clear or leaf 06H is absent.
 */
static uint8_t advanced_pic_timer_read_always_running(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(APIC_CPUID_LEAF_BASIC_MAXIMUM, 0u, &eax, &ebx, &ecx, &edx);
    if (eax < APIC_CPUID_LEAF_THERMAL_AND_POWER)
        return 0u;
    asmutils_cpuid(APIC_CPUID_LEAF_THERMAL_AND_POWER, 0u, &eax, &ebx, &ecx, &edx);
    return (uint8_t) (((eax & APIC_CPUID_EAX_BIT_ALWAYS_RUNNING_TIMER) != 0u) ? 1u : 0u);
}

static uint8_t advanced_pic_timer_backend_map_local_apic_mmio(void)
{
    PageDirectoryEntry_t pde_flags = {0};
    PageTableEntry_t pte_flags = {0};

    if (advanced_pic_timer_local_apic_physical_base == 0u)
        return 0u;

    if (paging_is_mapped(APIC_TIMER_MMIO_VIRTUAL_BASE))
    {
        advanced_pic_timer_local_apic_mmio_mapped = 1u;
        return 1u;
    }

    pde_flags.present = 1u;
    pde_flags.read_write = 1u;
    pde_flags.user_supervisor = 0u;
    pde_flags.write_through = 0u;
    pde_flags.cache_disable = 1u;

    pte_flags.present = 1u;
    pte_flags.read_write = 1u;
    pte_flags.user_supervisor = 0u;
    pte_flags.write_through = 0u;
    pte_flags.cache_disable = 1u;

    if (!paging_map_page(APIC_TIMER_MMIO_VIRTUAL_BASE, advanced_pic_timer_local_apic_physical_base, pde_flags,
                         pte_flags))
        return 0u;

    advanced_pic_timer_local_apic_mmio_mapped = 1u;
    return 1u;
}

/**
 * @brief The timer count that lasts @p cycles of the timestamp counter, by the two counts of the
 *        calibration.
 *
 * @note The product is formed in 64 bits and checked: a wrap would arm the timer for a fraction of
 *       the delay asked for, which is the failure that looks like a spurious wake-up rather than
 *       like a bug, so a delay too long for the product gets the timer's whole range.
 *
 * @param cycles Delay.
 * @return The count, rounded down, at least 1 and at most the 32-bit maximum.
 */
static uint32_t advanced_pic_timer_count_for_cycles(uint64_t cycles)
{
    uint64_t product = 0u;

    if (__builtin_mul_overflow(cycles, (uint64_t) advanced_pic_timer_calibration_local_timer_counts, &product))
        return 0xFFFFFFFFu;

    const uint64_t count = product / advanced_pic_timer_calibration_timestamp_counter_cycles;
    if (count == 0u)
        return 1u;
    if (count > 0xFFFFFFFFu)
        return 0xFFFFFFFFu;
    return (uint32_t) count;
}

/**
 * @brief Arms @p deadline in TSC-deadline mode.
 *
 * @details The fence orders the memory-mapped write of the entry before the WRMSR, which the SDM requires in xAPIC
 *          mode only (§13.5.4.1); it is paid on the first sleep of a session, when the entry changes. A deadline of
 *          zero would disarm the timer, and becomes 1, already passed.
 *
 * @param deadline Cycle of the timestamp counter.
 */
static void advanced_pic_timer_arm_timestamp_counter_deadline(uint64_t deadline)
{
    if (advanced_pic_timer_select_lvt(APIC_TIMER_LVT_TIMESTAMP_COUNTER_DEADLINE | APIC_TIMER_VECTOR) &&
        !apic_is_x2apic_active())
        asmutils_memory_fence();

    advanced_pic_timer_armed_deadline = deadline != 0u ? deadline : 1u;
    asmutils_write_model_specific_register(IA32_TSC_DEADLINE_MSR, advanced_pic_timer_armed_deadline);
}

/**
 * @brief Arms @p deadline in count mode, from the cycles left until it.
 * @param deadline Cycle of the timestamp counter.
 * @return 1 when armed, 0 without a calibration.
 */
static uint8_t advanced_pic_timer_arm_count(uint64_t deadline)
{
    if (advanced_pic_timer_local_apic_calibrated_frequency_hz == 0u ||
        advanced_pic_timer_calibration_timestamp_counter_cycles == 0u)
        return 0u;

    const uint64_t now = asmutils_read_timestamp_counter();
    (void) advanced_pic_timer_select_lvt(APIC_TIMER_VECTOR);
    apic_write(LAPIC_REG_TIMER_INIT, advanced_pic_timer_count_for_cycles(deadline > now ? deadline - now : 0u));
    return 1u;
}

uint8_t advanced_pic_timer_backend_initialize(uint32_t target_frequency_hz)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint64_t local_apic_physical_base_u64;
    uint64_t apic_base_msr;

    (void) target_frequency_hz;

    advanced_pic_timer_local_apic_physical_base = 0u;
    advanced_pic_timer_local_apic_is_bootstrap_processor = 0u;
    advanced_pic_timer_local_apic_mmio_mapped = 0u;
    advanced_pic_timer_local_apic_version_register = 0u;
    advanced_pic_timer_local_apic_calibrated_frequency_hz = 0u;
    advanced_pic_timer_local_apic_periodic_mode_enabled = 0u;
    advanced_pic_timer_lvt_timer = APIC_TIMER_LVT_UNWRITTEN;
    advanced_pic_timer_armed_deadline = 0u;
    advanced_pic_timer_timestamp_counter_deadlines = 0u;
    advanced_pic_timer_invariant_timestamp_counter = advanced_pic_timer_read_invariant_timestamp_counter();
    advanced_pic_timer_always_running = advanced_pic_timer_read_always_running();

    asmutils_cpuid(APIC_CPUID_LEAF_FEATURES, 0u, &eax, &ebx, &ecx, &edx);
    if ((edx & APIC_CPUID_EDX_BIT_APIC) == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-probe-no-local-apic";
        return 0u;
    }

    apic_base_msr = asmutils_read_model_specific_register(IA32_APIC_BASE_MSR);
    if ((apic_base_msr & IA32_APIC_BASE_ENABLE_BIT) == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-probe-local-apic-disabled";
        return 0u;
    }

    local_apic_physical_base_u64 = apic_base_msr & IA32_APIC_BASE_PHYSICAL_BASE_MASK;
    if (local_apic_physical_base_u64 == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-probe-mmio-base-invalid";
        return 0u;
    }

    advanced_pic_timer_local_apic_physical_base = (uint32_t) local_apic_physical_base_u64;
    advanced_pic_timer_timestamp_counter_deadlines =
        (uint8_t) (((ecx & APIC_CPUID_ECX_BIT_TIMESTAMP_COUNTER_DEADLINE) != 0u) ? 1u : 0u);
    advanced_pic_timer_local_apic_is_bootstrap_processor =
        (uint8_t) (((apic_base_msr & IA32_APIC_BASE_BSP_BIT) != 0u) ? 1u : 0u);

    advanced_pic_timer_backend_state_name = ((apic_base_msr & IA32_APIC_BASE_X2APIC_MODE_BIT) != 0u) ?
                                                "apic-probe-x2apic-from-firmware" :
                                                "apic-probe-xapic-mmio-ready";

    /* TODO: APIC MMIO timer programming and IRQ vector routing are not wired yet. */
    return 0u;
}

uint8_t advanced_pic_timer_backend_late_initialize(void)
{
    if (advanced_pic_timer_local_apic_physical_base == 0u)
        return 0u;

    if (!advanced_pic_timer_backend_map_local_apic_mmio())
    {
        advanced_pic_timer_backend_state_name = "apic-late-mmio-map-failed";
        return 0u;
    }

    if (!apic_initialize_on_cpu(APIC_TIMER_MMIO_VIRTUAL_BASE))
    {
        advanced_pic_timer_backend_state_name = "apic-initialize-failed";
        return 0u;
    }

    advanced_pic_timer_local_apic_version_register = apic_read(LAPIC_REG_VERSION);

    advanced_pic_timer_write_lvt(APIC_TIMER_LVT_PARKED);

    advanced_pic_timer_backend_state_name =
        apic_is_x2apic_active() ? "x2apic-ready-lvt-masked" : "xapic-ready-lvt-masked";
    return 1u;
}

uint8_t advanced_pic_timer_backend_calibrate_with_pit(void)
{
    uint32_t pit_frequency_hz;
    uint32_t start_tick;
    uint32_t end_tick;
    uint32_t local_apic_current_count;
    uint32_t local_apic_elapsed_counts;
    uint64_t local_apic_frequency_hz_u64;

    if (!advanced_pic_timer_is_reachable())
        return 0u;

    pit_frequency_hz = interrupt_request_get_timer_frequency_hz();
    if (pit_frequency_hz == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-calibration-pit-frequency-invalid";
        return 0u;
    }

    start_tick = interrupt_request_get_tick_count();
    while (interrupt_request_get_tick_count() == start_tick)
        asmutils_no_operation();

    start_tick = interrupt_request_get_tick_count();

    apic_write(LAPIC_REG_TIMER_DIV, 0x3u);
    advanced_pic_timer_write_lvt(APIC_TIMER_LVT_PARKED);
    apic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);
    const uint64_t start_cycle = asmutils_read_timestamp_counter();

    end_tick = start_tick + 8u;
    while (interrupt_request_get_tick_count() < end_tick)
        asmutils_no_operation();

    local_apic_current_count = apic_read(LAPIC_REG_TIMER_CUR);
    const uint64_t end_cycle = asmutils_read_timestamp_counter();
    local_apic_elapsed_counts = 0xFFFFFFFFu - local_apic_current_count;

    apic_write(LAPIC_REG_TIMER_INIT, 0u);

    if (local_apic_elapsed_counts == 0u || end_cycle <= start_cycle)
    {
        advanced_pic_timer_backend_state_name = "apic-calibration-no-progress";
        return 0u;
    }

    advanced_pic_timer_calibration_local_timer_counts = local_apic_elapsed_counts;
    advanced_pic_timer_calibration_timestamp_counter_cycles = end_cycle - start_cycle;
    advanced_pic_timer_timestamp_counter_calibrated_frequency_hz =
        ((end_cycle - start_cycle) * (uint64_t) pit_frequency_hz) / 8u;

    local_apic_frequency_hz_u64 = (uint64_t) local_apic_elapsed_counts;
    local_apic_frequency_hz_u64 *= (uint64_t) pit_frequency_hz;
    local_apic_frequency_hz_u64 /= (uint64_t) 8u;

    if (local_apic_frequency_hz_u64 > 0xFFFFFFFFull)
        local_apic_frequency_hz_u64 = 0xFFFFFFFFull;

    advanced_pic_timer_local_apic_calibrated_frequency_hz = (uint32_t) local_apic_frequency_hz_u64;
    advanced_pic_timer_backend_state_name = apic_is_x2apic_active() ? "x2apic-calibrated" : "xapic-calibrated";
    return 1u;
}

uint8_t advanced_pic_timer_backend_enable_periodic_mode(uint32_t target_frequency_hz)
{
    uint32_t timer_initial_count;
    uint32_t lapic_timer_hz;

    if (!advanced_pic_timer_is_reachable())
    {
        interrupt_request_set_timer_owner_is_apic(0u);
        advanced_pic_timer_local_apic_periodic_mode_enabled = 0u;
        advanced_pic_timer_backend_state_name = "apic-periodic-not-ready";
        return 0u;
    }

    lapic_timer_hz = advanced_pic_timer_local_apic_calibrated_frequency_hz;
    if (lapic_timer_hz == 0u)
    {
        interrupt_request_set_timer_owner_is_apic(0u);
        advanced_pic_timer_local_apic_periodic_mode_enabled = 0u;
        advanced_pic_timer_backend_state_name = "apic-periodic-missing-calibration";
        return 0u;
    }

    if (target_frequency_hz == 0u)
        target_frequency_hz = 1u;

    timer_initial_count = lapic_timer_hz / target_frequency_hz;
    if (timer_initial_count == 0u)
        timer_initial_count = 1u;

    apic_write(LAPIC_REG_TIMER_DIV, 0x3u);
    advanced_pic_timer_write_lvt(APIC_TIMER_LVT_PERIODIC | APIC_TIMER_VECTOR);
    advanced_pic_timer_armed_deadline = 0u;
    apic_write(LAPIC_REG_TIMER_INIT, timer_initial_count);

    programmable_interrupt_controller_set_mask(0u);
    interrupt_request_set_timer_owner_is_apic(1u);

    advanced_pic_timer_local_apic_periodic_mode_enabled = 1u;
    advanced_pic_timer_backend_state_name =
        apic_is_x2apic_active() ? "x2apic-periodic-active" : "xapic-periodic-active";
    return 1u;
}

const char *advanced_pic_timer_backend_name(void) { return advanced_pic_timer_backend_state_name; }

uint32_t advanced_pic_timer_backend_get_local_apic_physical_base(void)
{
    return advanced_pic_timer_local_apic_physical_base;
}

uint32_t advanced_pic_timer_backend_get_local_apic_virtual_base(void)
{
    if (!advanced_pic_timer_is_reachable())
        return 0u;

    return APIC_TIMER_MMIO_VIRTUAL_BASE;
}

uint8_t advanced_pic_timer_backend_is_local_apic_mmio_mapped(void) { return advanced_pic_timer_local_apic_mmio_mapped; }

uint32_t advanced_pic_timer_backend_get_local_apic_version_register(void)
{
    return advanced_pic_timer_local_apic_version_register;
}

uint32_t advanced_pic_timer_backend_get_calibrated_timer_frequency_hz(void)
{
    return advanced_pic_timer_local_apic_calibrated_frequency_hz;
}

uint64_t advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz(void)
{
    return advanced_pic_timer_timestamp_counter_calibrated_frequency_hz;
}

uint8_t advanced_pic_timer_backend_is_periodic_mode_enabled(void)
{
    return advanced_pic_timer_local_apic_periodic_mode_enabled;
}

void advanced_pic_timer_backend_signal_end_of_interrupt(void) { apic_send_eoi(); }

uint8_t advanced_pic_timer_backend_is_bootstrap_processor(void)
{
    return advanced_pic_timer_local_apic_is_bootstrap_processor;
}

uint8_t advanced_pic_timer_backend_arms_timestamp_counter_deadlines(void)
{
    return advanced_pic_timer_timestamp_counter_deadlines;
}

uint8_t advanced_pic_timer_backend_has_invariant_timestamp_counter(void)
{
    return advanced_pic_timer_invariant_timestamp_counter;
}

uint8_t advanced_pic_timer_backend_always_runs(void) { return advanced_pic_timer_always_running; }

uint8_t advanced_pic_timer_backend_arm_at_cycle(uint64_t deadline)
{
    if (!advanced_pic_timer_is_reachable())
        return 0u;

    if (advanced_pic_timer_timestamp_counter_deadlines)
        advanced_pic_timer_arm_timestamp_counter_deadline(deadline);
    else if (!advanced_pic_timer_arm_count(deadline))
        return 0u;

    advanced_pic_timer_local_apic_periodic_mode_enabled = 0u;
    if (advanced_pic_timer_timestamp_counter_deadlines)
        advanced_pic_timer_backend_state_name = apic_is_x2apic_active() ? "x2apic-tsc-deadline" : "xapic-tsc-deadline";
    else
        advanced_pic_timer_backend_state_name = apic_is_x2apic_active() ? "x2apic-oneshot" : "xapic-oneshot";
    return 1u;
}

uint8_t advanced_pic_timer_backend_disarm(uint64_t now)
{
    if (!advanced_pic_timer_is_reachable())
        return 0u;

    if (advanced_pic_timer_timestamp_counter_deadlines)
    {
        const uint64_t deadline = advanced_pic_timer_armed_deadline;

        advanced_pic_timer_armed_deadline = 0u;
        if (deadline == 0u || now >= deadline)
            return 0u;
        asmutils_write_model_specific_register(IA32_TSC_DEADLINE_MSR, 0u);
        return 1u;
    }

    if (advanced_pic_timer_lvt_timer != APIC_TIMER_VECTOR || apic_read(LAPIC_REG_TIMER_CUR) == 0u)
        return 0u;
    apic_write(LAPIC_REG_TIMER_INIT, 0u);
    return 1u;
}

/**
 * @brief Stops the local timer entirely.
 *
 * Writing zero to the initial count halts a countdown, and leaving TSC-deadline mode disarms a deadline
 * (§13.5.4.1); the vector is left masked so that a stale count cannot deliver an interrupt afterwards.
 */
void advanced_pic_timer_backend_disable(void)
{
    if (!advanced_pic_timer_is_reachable())
        return;

    apic_write(LAPIC_REG_TIMER_INIT, 0u);
    advanced_pic_timer_write_lvt(APIC_TIMER_LVT_PARKED);
    advanced_pic_timer_armed_deadline = 0u;
    advanced_pic_timer_local_apic_periodic_mode_enabled = 0u;
    advanced_pic_timer_backend_state_name = "apic-timer-stopped";
}
