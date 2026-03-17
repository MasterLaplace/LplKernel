#include <kernel/cpu/apic_timer.h>

#include <kernel/cpu/irq.h>
#include <kernel/cpu/pic.h>

#include <kernel/cpu/paging.h>

#include <kernel/lib/asmutils.h>

#define APIC_CPUID_LEAF_FEATURES 0x00000001u
#define APIC_CPUID_EDX_BIT_APIC  (1u << 9u)

#define IA32_APIC_BASE_MSR                0x0000001Bu
#define IA32_APIC_BASE_BSP_BIT            (1ull << 8u)
#define IA32_APIC_BASE_X2APIC_MODE_BIT    (1ull << 10u)
#define IA32_APIC_BASE_ENABLE_BIT         (1ull << 11u)
#define IA32_APIC_BASE_PHYSICAL_BASE_MASK 0xFFFFF000ull

#include <kernel/cpu/apic.h>

#define APIC_TIMER_MMIO_VIRTUAL_BASE 0xFFB00000u

static const char *advanced_pic_timer_backend_state_name = "apic-probe-only";
static uint32_t advanced_pic_timer_local_apic_physical_base = 0u;
static uint8_t advanced_pic_timer_local_apic_is_bootstrap_processor = 0u;
static uint8_t advanced_pic_timer_local_apic_mmio_mapped = 0u;
static uint32_t advanced_pic_timer_local_apic_version_register = 0u;
static uint32_t advanced_pic_timer_local_apic_calibrated_frequency_hz = 0u;
static uint8_t advanced_pic_timer_local_apic_periodic_mode_enabled = 0u;

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

    cpu_cpuid(APIC_CPUID_LEAF_FEATURES, 0u, &eax, &ebx, &ecx, &edx);
    if ((edx & APIC_CPUID_EDX_BIT_APIC) == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-probe-no-local-apic";
        return 0u;
    }

    apic_base_msr = cpu_read_msr(IA32_APIC_BASE_MSR);
    if ((apic_base_msr & IA32_APIC_BASE_ENABLE_BIT) == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-probe-local-apic-disabled";
        return 0u;
    }

    if ((apic_base_msr & IA32_APIC_BASE_X2APIC_MODE_BIT) != 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-probe-x2apic-mode-unsupported";
        return 0u;
    }

    local_apic_physical_base_u64 = apic_base_msr & IA32_APIC_BASE_PHYSICAL_BASE_MASK;
    if (local_apic_physical_base_u64 == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-probe-mmio-base-invalid";
        return 0u;
    }

    advanced_pic_timer_local_apic_physical_base = (uint32_t) local_apic_physical_base_u64;
    advanced_pic_timer_local_apic_is_bootstrap_processor =
        (uint8_t) (((apic_base_msr & IA32_APIC_BASE_BSP_BIT) != 0u) ? 1u : 0u);

    advanced_pic_timer_backend_state_name = "apic-probe-xapic-mmio-ready";

    /*
     * @todo:
     * APIC MMIO timer programming and IRQ vector routing are not wired yet.
     * Keep runtime ownership on PIT until full local APIC timer bring-up lands.
     */
    return 0u;
}

uint8_t advanced_pic_timer_backend_late_initialize(void)
{
    uint32_t spurious_register_value;

    if (advanced_pic_timer_local_apic_physical_base == 0u)
        return 0u;

    if (!advanced_pic_timer_backend_map_local_apic_mmio())
    {
        advanced_pic_timer_backend_state_name = "apic-late-mmio-map-failed";
        return 0u;
    }

    /* Initialize Local APIC on this CPU (handles x2APIC transition if supported) */
    if (!apic_initialize_on_cpu(APIC_TIMER_MMIO_VIRTUAL_BASE))
    {
        advanced_pic_timer_backend_state_name = "apic-initialize-failed";
        return 0u;
    }

    advanced_pic_timer_local_apic_version_register = apic_read(LAPIC_REG_VERSION);

    /* APIC already enabled by apic_initialize_on_cpu (SVR bit 8) */

    apic_write(LAPIC_REG_LVT_TIMER, (1u << 16u) | 0xFEu);

    advanced_pic_timer_backend_state_name = apic_is_x2apic_active() ? "x2apic-ready-lvt-masked" : "xapic-ready-lvt-masked";
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

    if (!advanced_pic_timer_local_apic_mmio_mapped && !apic_is_x2apic_active())
        return 0u;

    pit_frequency_hz = interrupt_request_get_timer_frequency_hz();
    if (pit_frequency_hz == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-calibration-pit-frequency-invalid";
        return 0u;
    }

    start_tick = interrupt_request_get_tick_count();
    while (interrupt_request_get_tick_count() == start_tick)
        cpu_no_operation();

    start_tick = interrupt_request_get_tick_count();

    apic_write(LAPIC_REG_TIMER_DIV, 0x3u); /* Divide by 16 */
    apic_write(LAPIC_REG_LVT_TIMER, (1u << 16u) | 0xFEu); /* One-shot mode, masked */
    apic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);

    end_tick = start_tick + 8u;
    while (interrupt_request_get_tick_count() < end_tick)
        cpu_no_operation();

    local_apic_current_count = apic_read(LAPIC_REG_TIMER_CUR);
    local_apic_elapsed_counts = 0xFFFFFFFFu - local_apic_current_count;

    apic_write(LAPIC_REG_TIMER_INIT, 0u);

    if (local_apic_elapsed_counts == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-calibration-no-progress";
        return 0u;
    }

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

    if (!advanced_pic_timer_local_apic_mmio_mapped && !apic_is_x2apic_active())
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
    apic_write(LAPIC_REG_LVT_TIMER, (1u << 17u) | (32u + 0u)); /* Periodic mode + Vector 32 */
    apic_write(LAPIC_REG_TIMER_INIT, timer_initial_count);

    programmable_interrupt_controller_set_mask(0u);
    interrupt_request_set_timer_owner_is_apic(1u);

    advanced_pic_timer_local_apic_periodic_mode_enabled = 1u;
    advanced_pic_timer_backend_state_name = apic_is_x2apic_active() ? "x2apic-periodic-active" : "xapic-periodic-active";
    return 1u;
}

const char *advanced_pic_timer_backend_name(void) { return advanced_pic_timer_backend_state_name; }

uint32_t advanced_pic_timer_backend_get_local_apic_physical_base(void)
{
    return advanced_pic_timer_local_apic_physical_base;
}

uint32_t advanced_pic_timer_backend_get_local_apic_virtual_base(void)
{
    if (!advanced_pic_timer_local_apic_mmio_mapped && !apic_is_x2apic_active())
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

uint8_t advanced_pic_timer_backend_is_periodic_mode_enabled(void)
{
    return advanced_pic_timer_local_apic_periodic_mode_enabled;
}

void advanced_pic_timer_backend_signal_end_of_interrupt(void)
{
    apic_send_eoi();
}

uint8_t advanced_pic_timer_backend_is_bootstrap_processor(void)
{
    return advanced_pic_timer_local_apic_is_bootstrap_processor;
}
