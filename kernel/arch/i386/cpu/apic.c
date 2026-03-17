/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** apic — Universal Local APIC Management
*/

#include <kernel/cpu/apic.h>
#include <kernel/lib/asmutils.h>
#include <kernel/drivers/serial.h>
#include <stdbool.h>
#include <stddef.h>

extern Serial_t com1;

#define IA32_APIC_BASE_MSR                0x0000001Bu
#define IA32_APIC_BASE_X2APIC_MODE_BIT    (1ull << 10u)
#define IA32_APIC_BASE_ENABLE_BIT         (1ull << 11u)

#define CPUID_FEAT_ECX_X2APIC             (1u << 21u)

static uint32_t g_apic_mmio_base = 0u;
static bool g_x2apic_active = false;

bool apic_initialize_on_cpu(uint32_t mmio_virtual_base)
{
    uint32_t eax, ebx, ecx, edx;
    
    g_apic_mmio_base = mmio_virtual_base;

    /* Check x2APIC support via CPUID */
    cpu_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    bool x2apic_supported = (ecx & CPUID_FEAT_ECX_X2APIC) != 0;

    uint64_t apic_base = cpu_read_msr(IA32_APIC_BASE_MSR);

    if (x2apic_supported)
    {
        /* Enable x2APIC mode (set bits 10 and 11) */
        apic_base |= IA32_APIC_BASE_X2APIC_MODE_BIT | IA32_APIC_BASE_ENABLE_BIT;
        cpu_write_msr(IA32_APIC_BASE_MSR, apic_base);
        g_x2apic_active = true;
        serial_write_string(&com1, "[Laplace Kernel]: CPU APIC mode=x2apic-active\n");
    }
    else
    {
        /* Ensure normal APIC is enabled */
        apic_base |= IA32_APIC_BASE_ENABLE_BIT;
        cpu_write_msr(IA32_APIC_BASE_MSR, apic_base);
        g_x2apic_active = false;
        
        /* Enable LAPIC via Spurious Vector Register */
        if (g_apic_mmio_base) {
            uint32_t svr = apic_read(LAPIC_REG_SPURIOUS);
            apic_write(LAPIC_REG_SPURIOUS, svr | (1u << 8u));
        }
        serial_write_string(&com1, "[Laplace Kernel]: CPU APIC mode=xapic-active (fallback)\n");
    }

    return true;
}

uint32_t apic_read(uint32_t reg_offset)
{
    if (g_x2apic_active)
    {
        /* x2APIC: read from MSR 0x800 + (reg_offset >> 4) */
        uint32_t msr = X2APIC_MSR_BASE + (reg_offset >> 4);
        return (uint32_t)cpu_read_msr(msr);
    }
    else if (g_apic_mmio_base)
    {
        /* xAPIC: read from MMIO */
        return *(volatile uint32_t *)(uintptr_t)(g_apic_mmio_base + reg_offset);
    }
    return 0xFFFFFFFFu;
}

void apic_write(uint32_t reg_offset, uint32_t value)
{
    if (g_x2apic_active)
    {
        /* x2APIC: write to MSR 0x800 + (reg_offset >> 4) */
        uint32_t msr = X2APIC_MSR_BASE + (reg_offset >> 4);
        cpu_write_msr(msr, (uint64_t)value);
    }
    else if (g_apic_mmio_base)
    {
        /* xAPIC: write to MMIO */
        *(volatile uint32_t *)(uintptr_t)(g_apic_mmio_base + reg_offset) = value;
    }
}

void apic_write_icr(uint32_t high, uint32_t low)
{
    if (g_x2apic_active)
    {
        /* x2APIC: single 64-bit MSR write */
        uint64_t msr_val = ((uint64_t) high << 32u) | (uint64_t) low;
        cpu_write_msr(X2APIC_MSR_BASE + (LAPIC_REG_ICR_LOW >> 4u), msr_val);
    }
    else if (g_apic_mmio_base)
    {
        /* xAPIC: high then low (low triggers delivery) */
        apic_write(LAPIC_REG_ICR_HIGH, high);
        apic_write(LAPIC_REG_ICR_LOW, low);
    }
}

void apic_send_eoi(void)
{
    apic_write(LAPIC_REG_EOI, 0u);
}

bool apic_is_x2apic_active(void)
{
    return g_x2apic_active;
}
