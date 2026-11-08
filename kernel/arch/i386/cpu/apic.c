#include <kernel/cpu/apic.h>
#include <kernel/cpu/cpu_identity.h>

extern Serial_t com1;

#define IA32_APIC_BASE_MSR             0x0000001Bu
#define IA32_APIC_BASE_X2APIC_MODE_BIT (1ull << 10u)
#define IA32_APIC_BASE_ENABLE_BIT      (1ull << 11u)

#define CPUID_FEAT_ECX_X2APIC (1u << 21u)

/** Spurious-vector register bit that software-enables the local APIC: without it, no fixed IPI is accepted. */
#define APIC_SPURIOUS_SOFTWARE_ENABLE_BIT (1u << 8u)

static uint32_t g_apic_mmio_base = 0u;
static bool g_x2apic_active = false;
static ApicMode_t g_apic_mode = APIC_MODE_NOT_CHOSEN; /**< Chosen by the bootstrap processor, shared by every one. */

/**
 * @brief What this processor's CPUID and IA32_APIC_BASE report, before the kernel changes its mode.
 */
static ApicPlatform_t apic_read_platform(void)
{
    CpuIdentity_t identity;
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    cpu_identity_read(&identity);
    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    return (ApicPlatform_t){
        .x2apic_offered = (ecx & CPUID_FEAT_ECX_X2APIC) != 0u,
        .hypervisor_present = identity.hypervisor,
        .x2apic_from_firmware =
            (asmutils_read_model_specific_register(IA32_APIC_BASE_MSR) & IA32_APIC_BASE_X2APIC_MODE_BIT) != 0u,
    };
}

/**
 * @brief Whether @p mode is one of the x2APIC modes.
 */
static bool apic_mode_is_x2apic(ApicMode_t mode)
{
    return mode == APIC_MODE_X2APIC_FROM_FIRMWARE || mode == APIC_MODE_X2APIC_UNDER_HYPERVISOR;
}

/**
 * @brief The serial line that names @p mode and its reason.
 */
static const char *apic_mode_line(ApicMode_t mode)
{
    switch (mode)
    {
    case APIC_MODE_NOT_CHOSEN: break;
    case APIC_MODE_XAPIC_NOT_OFFERED:
        return "[Laplace Kernel]: CPU APIC mode=xapic-active, reason=x2apic-not-offered\n";
    case APIC_MODE_XAPIC_WITHOUT_REMAPPING:
        return "[Laplace Kernel]: CPU APIC mode=xapic-active, reason=bare-metal-without-interrupt-remapping\n";
    case APIC_MODE_X2APIC_FROM_FIRMWARE:
        return "[Laplace Kernel]: CPU APIC mode=x2apic-active, reason=handed-over-by-firmware\n";
    case APIC_MODE_X2APIC_UNDER_HYPERVISOR:
        return "[Laplace Kernel]: CPU APIC mode=x2apic-active, reason=under-hypervisor\n";
    }
    return "[Laplace Kernel]: CPU APIC mode=not-chosen\n";
}

/**
 * @brief Puts this processor's local APIC in x2APIC mode, from xAPIC mode or from x2APIC mode itself.
 */
static void apic_enter_x2apic_mode(void)
{
    const uint64_t base = asmutils_read_model_specific_register(IA32_APIC_BASE_MSR);

    asmutils_write_model_specific_register(IA32_APIC_BASE_MSR,
                                           base | IA32_APIC_BASE_X2APIC_MODE_BIT | IA32_APIC_BASE_ENABLE_BIT);
}

/**
 * @brief Puts this processor's local APIC in xAPIC mode, from x2APIC mode through the disabled state: the only way
 *        back, since writing xAPIC mode over x2APIC mode raises a general-protection fault (SDM 325384-093US
 *        §13.12.5.1).
 */
static void apic_enter_xapic_mode(void)
{
    const uint64_t base = asmutils_read_model_specific_register(IA32_APIC_BASE_MSR);
    const uint64_t xapic = (base & ~IA32_APIC_BASE_X2APIC_MODE_BIT) | IA32_APIC_BASE_ENABLE_BIT;

    if ((base & IA32_APIC_BASE_X2APIC_MODE_BIT) != 0u)
        asmutils_write_model_specific_register(IA32_APIC_BASE_MSR, xapic & ~IA32_APIC_BASE_ENABLE_BIT);
    asmutils_write_model_specific_register(IA32_APIC_BASE_MSR, xapic);
}

ApicMode_t apic_choose_mode(const ApicPlatform_t *platform)
{
    return platform->x2apic_offered ? APIC_MODE_X2APIC_UNDER_HYPERVISOR : APIC_MODE_XAPIC_NOT_OFFERED;
}

ApicMode_t apic_get_mode(void) { return g_apic_mode; }

bool apic_initialize_on_cpu(uint32_t mmio_virtual_base)
{
    g_apic_mmio_base = mmio_virtual_base;
    if (g_apic_mode == APIC_MODE_NOT_CHOSEN)
    {
        const ApicPlatform_t platform = apic_read_platform();

        g_apic_mode = apic_choose_mode(&platform);
    }
    g_x2apic_active = apic_mode_is_x2apic(g_apic_mode);
    if (g_x2apic_active)
        apic_enter_x2apic_mode();
    else
        apic_enter_xapic_mode();
    serial_write_string(&com1, apic_mode_line(g_apic_mode));

    apic_write(LAPIC_REG_SPURIOUS, apic_read(LAPIC_REG_SPURIOUS) | APIC_SPURIOUS_SOFTWARE_ENABLE_BIT);
    return true;
}

uint32_t apic_read(uint32_t reg_offset)
{
    if (g_x2apic_active)
    {
        uint32_t msr = X2APIC_MSR_BASE + (reg_offset >> 4);
        return (uint32_t) asmutils_read_model_specific_register(msr);
    }
    else if (g_apic_mmio_base)
    {
        return *(volatile uint32_t *) (uintptr_t) (g_apic_mmio_base + reg_offset);
    }
    return 0xFFFFFFFFu;
}

void apic_write(uint32_t reg_offset, uint32_t value)
{
    if (g_x2apic_active)
    {
        uint32_t msr = X2APIC_MSR_BASE + (reg_offset >> 4);
        asmutils_write_model_specific_register(msr, (uint64_t) value);
    }
    else if (g_apic_mmio_base)
    {
        *(volatile uint32_t *) (uintptr_t) (g_apic_mmio_base + reg_offset) = value;
    }
}

void apic_write_icr(uint32_t high, uint32_t low)
{
    if (g_x2apic_active)
    {
        uint64_t msr_val = ((uint64_t) high << 32u) | (uint64_t) low;
        asmutils_write_model_specific_register(X2APIC_MSR_BASE + (LAPIC_REG_ICR_LOW >> 4u), msr_val);
    }
    else if (g_apic_mmio_base)
    {
        apic_write(LAPIC_REG_ICR_HIGH, high);
        apic_write(LAPIC_REG_ICR_LOW, low);
    }
}

void apic_send_eoi(void) { apic_write(LAPIC_REG_EOI, 0u); }

bool apic_is_x2apic_active(void) { return g_x2apic_active; }

uint32_t apic_get_id(void)
{
    const uint32_t identifier = apic_read(LAPIC_REG_ID);

    return g_x2apic_active ? identifier : identifier >> 24u;
}
