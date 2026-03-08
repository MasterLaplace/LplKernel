#include <kernel/cpu/apic_timer.h>

#include <kernel/cpu/irq.h>

#include <kernel/cpu/paging.h>

#include <kernel/lib/asmutils.h>

#define APIC_CPUID_LEAF_FEATURES 0x00000001u
#define APIC_CPUID_EDX_BIT_APIC  (1u << 9u)

#define IA32_APIC_BASE_MSR                0x0000001Bu
#define IA32_APIC_BASE_BSP_BIT            (1ull << 8u)
#define IA32_APIC_BASE_X2APIC_MODE_BIT    (1ull << 10u)
#define IA32_APIC_BASE_ENABLE_BIT         (1ull << 11u)
#define IA32_APIC_BASE_PHYSICAL_BASE_MASK 0xFFFFF000ull

#define APIC_TIMER_MMIO_VIRTUAL_BASE 0xFFB00000u

#define LOCAL_APIC_REGISTER_VERSION             0x0030u
#define LOCAL_APIC_REGISTER_SPURIOUS            0x00F0u
#define LOCAL_APIC_REGISTER_LVT_TIMER           0x0320u
#define LOCAL_APIC_REGISTER_TIMER_INITIAL_COUNT 0x0380u
#define LOCAL_APIC_REGISTER_TIMER_CURRENT_COUNT 0x0390u
#define LOCAL_APIC_REGISTER_TIMER_DIVIDE_CONFIG 0x03E0u

#define LOCAL_APIC_SPURIOUS_ENABLE_BIT      (1u << 8u)
#define LOCAL_APIC_LVT_MASKED_BIT           (1u << 16u)
#define LOCAL_APIC_TIMER_VECTOR_PLACEHOLDER 0xFEu
#define LOCAL_APIC_TIMER_LVT_MODE_ONE_SHOT  (0u << 17u)

#define LOCAL_APIC_TIMER_DIVIDE_BY_16_ENCODING 0x3u

#define LOCAL_APIC_TIMER_CALIBRATION_TICKS 8u

static const char *advanced_pic_timer_backend_state_name = "apic-probe-only";
static uint32_t advanced_pic_timer_local_apic_physical_base = 0u;
static uint8_t advanced_pic_timer_local_apic_is_bootstrap_processor = 0u;
static uint8_t advanced_pic_timer_local_apic_mmio_mapped = 0u;
static uint32_t advanced_pic_timer_local_apic_version_register = 0u;
static uint32_t advanced_pic_timer_local_apic_calibrated_frequency_hz = 0u;

static inline volatile uint32_t *advanced_pic_timer_local_apic_register_pointer(uint32_t offset)
{
    return (volatile uint32_t *) (APIC_TIMER_MMIO_VIRTUAL_BASE + offset);
}

static inline uint32_t advanced_pic_timer_local_apic_register_read(uint32_t offset)
{
    return *advanced_pic_timer_local_apic_register_pointer(offset);
}

static inline void advanced_pic_timer_local_apic_register_write(uint32_t offset, uint32_t value)
{
    *advanced_pic_timer_local_apic_register_pointer(offset) = value;
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

    advanced_pic_timer_local_apic_version_register =
        advanced_pic_timer_local_apic_register_read(LOCAL_APIC_REGISTER_VERSION);

    spurious_register_value = advanced_pic_timer_local_apic_register_read(LOCAL_APIC_REGISTER_SPURIOUS);
    spurious_register_value |= LOCAL_APIC_SPURIOUS_ENABLE_BIT;
    advanced_pic_timer_local_apic_register_write(LOCAL_APIC_REGISTER_SPURIOUS, spurious_register_value);

    advanced_pic_timer_local_apic_register_write(LOCAL_APIC_REGISTER_LVT_TIMER,
                                                 LOCAL_APIC_LVT_MASKED_BIT | LOCAL_APIC_TIMER_VECTOR_PLACEHOLDER);

    advanced_pic_timer_backend_state_name = "apic-late-mmio-ready-lvt-masked";
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

    if (!advanced_pic_timer_local_apic_mmio_mapped)
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

    advanced_pic_timer_local_apic_register_write(LOCAL_APIC_REGISTER_TIMER_DIVIDE_CONFIG,
                                                 LOCAL_APIC_TIMER_DIVIDE_BY_16_ENCODING);
    advanced_pic_timer_local_apic_register_write(LOCAL_APIC_REGISTER_LVT_TIMER,
                                                 LOCAL_APIC_LVT_MASKED_BIT | LOCAL_APIC_TIMER_LVT_MODE_ONE_SHOT |
                                                     LOCAL_APIC_TIMER_VECTOR_PLACEHOLDER);
    advanced_pic_timer_local_apic_register_write(LOCAL_APIC_REGISTER_TIMER_INITIAL_COUNT, 0xFFFFFFFFu);

    end_tick = start_tick + LOCAL_APIC_TIMER_CALIBRATION_TICKS;
    while (interrupt_request_get_tick_count() < end_tick)
        cpu_no_operation();

    local_apic_current_count = advanced_pic_timer_local_apic_register_read(LOCAL_APIC_REGISTER_TIMER_CURRENT_COUNT);
    local_apic_elapsed_counts = 0xFFFFFFFFu - local_apic_current_count;

    advanced_pic_timer_local_apic_register_write(LOCAL_APIC_REGISTER_TIMER_INITIAL_COUNT, 0u);

    if (local_apic_elapsed_counts == 0u)
    {
        advanced_pic_timer_backend_state_name = "apic-calibration-no-progress";
        return 0u;
    }

    local_apic_frequency_hz_u64 = (uint64_t) local_apic_elapsed_counts;
    local_apic_frequency_hz_u64 *= (uint64_t) pit_frequency_hz;
    local_apic_frequency_hz_u64 /= (uint64_t) LOCAL_APIC_TIMER_CALIBRATION_TICKS;

    if (local_apic_frequency_hz_u64 > 0xFFFFFFFFull)
        local_apic_frequency_hz_u64 = 0xFFFFFFFFull;

    advanced_pic_timer_local_apic_calibrated_frequency_hz = (uint32_t) local_apic_frequency_hz_u64;
    advanced_pic_timer_backend_state_name = "apic-calibrated-pit-fallback";
    return 1u;
}

const char *advanced_pic_timer_backend_name(void) { return advanced_pic_timer_backend_state_name; }

uint32_t advanced_pic_timer_backend_get_local_apic_physical_base(void)
{
    return advanced_pic_timer_local_apic_physical_base;
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

uint8_t advanced_pic_timer_backend_is_bootstrap_processor(void)
{
    return advanced_pic_timer_local_apic_is_bootstrap_processor;
}
