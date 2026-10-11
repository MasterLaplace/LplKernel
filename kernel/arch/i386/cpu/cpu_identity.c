#include <kernel/cpu/cpu_identity.h>

#include <kernel/lib/asmutils.h>

#include <stddef.h>

#define CPU_IDENTITY_HYPERVISOR_BIT  (1u << 31)
#define CPU_IDENTITY_HYPERVISOR_LEAF 0x40000000u

static void cpu_identity_copy_register(char *destination, uint32_t value)
{
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        destination[byte] = (char) ((value >> (byte * 8u)) & 0xFFu);
}

static void cpu_identity_read_signature(uint32_t leaf, bool vendor_order, char *out, uint32_t *out_max_leaf)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(leaf, 0u, &eax, &ebx, &ecx, &edx);
    cpu_identity_copy_register(out, ebx);
    cpu_identity_copy_register(out + 4, vendor_order ? edx : ecx);
    cpu_identity_copy_register(out + 8, vendor_order ? ecx : edx);
    out[12] = '\0';
    if (out_max_leaf)
        *out_max_leaf = eax;
}

void cpu_identity_read(CpuIdentity_t *out)
{
    uint32_t max_leaf = 0u;
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    *out = (CpuIdentity_t){0};
    cpu_identity_read_signature(0u, true, out->vendor, &max_leaf);
    if (max_leaf < 1u)
        return;

    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    const uint32_t base_family = (eax >> 8) & 0xFu;
    const uint32_t base_model = (eax >> 4) & 0xFu;

    out->stepping = eax & 0xFu;
    out->family = base_family == 0xFu ? base_family + ((eax >> 20) & 0xFFu) : base_family;
    out->model = (base_family == 0x6u || base_family == 0xFu) ? base_model + (((eax >> 16) & 0xFu) << 4) : base_model;
    out->hypervisor = (ecx & CPU_IDENTITY_HYPERVISOR_BIT) != 0u;
    if (out->hypervisor)
        cpu_identity_read_signature(CPU_IDENTITY_HYPERVISOR_LEAF, false, out->hypervisor_signature, NULL);
}
