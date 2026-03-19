#include <kernel/cpu/cpu_topology.h>

#include <kernel/lib/asmutils.h>

#define CPU_TOPOLOGY_CPUID_LEAF_FEATURES 0x00000001u
#define CPU_TOPOLOGY_CPUID_EDX_APIC_BIT  (1u << 9u)
#define CPU_TOPOLOGY_MAX_LOGICAL_CPUS    32u
#define CPU_TOPOLOGY_INVALID_APIC_ID     0xFFFFFFFFu

static uint8_t cpu_topology_initialized = 0u;
static uint8_t cpu_topology_forced_slot_enabled = 0u;
static uint32_t cpu_topology_forced_slot = 0u;
static uint8_t cpu_topology_apic_id_valid = 0u;
static uint32_t cpu_topology_local_apic_id = 0u;
static uint32_t cpu_topology_apic_id_to_slot[CPU_TOPOLOGY_MAX_LOGICAL_CPUS];
static uint32_t cpu_topology_discovered_cpu_count = 0u;
static uint8_t cpu_topology_online_by_slot[CPU_TOPOLOGY_MAX_LOGICAL_CPUS];
static uint32_t cpu_topology_online_cpu_count = 0u;
static uint32_t cpu_topology_slot_domain[CPU_TOPOLOGY_MAX_LOGICAL_CPUS];
static const char *cpu_topology_source_name = "topology-uninitialized";

static void cpu_topology_reset_discovery(void)
{
    cpu_topology_discovered_cpu_count = 0u;
    cpu_topology_online_cpu_count = 0u;

    for (uint32_t i = 0u; i < CPU_TOPOLOGY_MAX_LOGICAL_CPUS; ++i)
    {
        cpu_topology_apic_id_to_slot[i] = CPU_TOPOLOGY_INVALID_APIC_ID;
        cpu_topology_online_by_slot[i] = 0u;
        cpu_topology_slot_domain[i] = i;
    }
}

static uint32_t cpu_topology_register_apic_id_internal(uint32_t apic_id)
{
    for (uint32_t slot = 0u; slot < cpu_topology_discovered_cpu_count; ++slot)
    {
        if (cpu_topology_apic_id_to_slot[slot] == apic_id)
            return slot;
    }

    if (cpu_topology_discovered_cpu_count < CPU_TOPOLOGY_MAX_LOGICAL_CPUS)
    {
        uint32_t slot = cpu_topology_discovered_cpu_count;
        cpu_topology_apic_id_to_slot[slot] = apic_id;
        ++cpu_topology_discovered_cpu_count;
        return slot;
    }

    return apic_id;
}

static void cpu_topology_detect_from_cpuid(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    cpu_cpuid(CPU_TOPOLOGY_CPUID_LEAF_FEATURES, 0u, &eax, &ebx, &ecx, &edx);

    if ((edx & CPU_TOPOLOGY_CPUID_EDX_APIC_BIT) == 0u)
    {
        cpu_topology_apic_id_valid = 0u;
        cpu_topology_local_apic_id = 0u;
        cpu_topology_source_name = "topology-cpuid-no-apic";
        return;
    }

    cpu_topology_local_apic_id = (ebx >> 24u) & 0xFFu;
    cpu_topology_apic_id_valid = 1u;
    cpu_topology_source_name = "topology-cpuid-apic-id";
    (void) cpu_topology_register_apic_id_internal(cpu_topology_local_apic_id);
}

void cpu_topology_initialize(void)
{
    cpu_topology_reset_discovery();
    cpu_topology_detect_from_cpuid();

    if (cpu_topology_discovered_cpu_count == 0u)
        cpu_topology_discovered_cpu_count = 1u;

    cpu_topology_initialized = 1u;
    cpu_topology_forced_slot_enabled = 0u;
    cpu_topology_forced_slot = 0u;

    cpu_topology_mark_runtime_cpu_online();
}

uint32_t cpu_topology_get_logical_slot(void)
{
    if (cpu_topology_forced_slot_enabled)
        return cpu_topology_forced_slot;

    if (cpu_topology_apic_id_valid)
        return cpu_topology_register_apic_id_internal(cpu_topology_local_apic_id);

    (void) cpu_topology_initialized;
    return 0u;
}

uint32_t cpu_topology_register_discovered_apic_id(uint32_t apic_id)
{
    return cpu_topology_register_apic_id_internal(apic_id & 0xFFu);
}

uint32_t cpu_topology_get_local_apic_id(void)
{
    return cpu_topology_local_apic_id;
}

void cpu_topology_set_runtime_local_apic_id(uint32_t apic_id)
{
    cpu_topology_local_apic_id = apic_id & 0xFFu;
    cpu_topology_apic_id_valid = 1u;
    (void) cpu_topology_register_apic_id_internal(cpu_topology_local_apic_id);
    if (!cpu_topology_forced_slot_enabled)
        cpu_topology_source_name = "topology-runtime-apic-id";
}

void cpu_topology_mark_apic_id_online(uint32_t apic_id)
{
    uint32_t slot = cpu_topology_register_apic_id_internal(apic_id & 0xFFu);

    if (slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS)
        return;

    if (!cpu_topology_online_by_slot[slot])
    {
        cpu_topology_online_by_slot[slot] = 1u;
        ++cpu_topology_online_cpu_count;
    }
}

void cpu_topology_mark_runtime_cpu_online(void)
{
    if (!cpu_topology_apic_id_valid)
        return;

    cpu_topology_mark_apic_id_online(cpu_topology_local_apic_id);
}

uint32_t cpu_topology_get_online_cpu_count(void)
{
    return cpu_topology_online_cpu_count;
}

bool cpu_topology_is_logical_slot_online(uint32_t slot)
{
    if (slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS)
        return false;

    return cpu_topology_online_by_slot[slot] != 0u;
}

void cpu_topology_bind_slot_to_domain(uint32_t slot, uint32_t domain)
{
    if (slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS)
        cpu_topology_slot_domain[slot] = domain;
}

uint32_t cpu_topology_get_slot_domain(uint32_t slot)
{
    if (slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS)
        return 0u;

    return cpu_topology_slot_domain[slot];
}

uint32_t cpu_topology_get_apic_id_at_slot(uint32_t slot)
{
    if (slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS)
        return 0xFFu;  /* Invalid APIC ID */

    uint32_t apic_id = cpu_topology_apic_id_to_slot[slot];
    if (apic_id == CPU_TOPOLOGY_INVALID_APIC_ID)
        return 0xFFu;

    return apic_id & 0xFFu;
}

uint32_t cpu_topology_get_slot_for_apic_id(uint32_t apic_id)
{
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS; ++slot)
    {
        if (cpu_topology_apic_id_to_slot[slot] == apic_id)
            return slot;
    }
    return CPU_TOPOLOGY_INVALID_APIC_ID;
}

uint32_t cpu_topology_get_discovered_cpu_count(void)
{
    return cpu_topology_discovered_cpu_count;
}

const char *cpu_topology_get_source_name(void)
{
    return cpu_topology_source_name;
}

bool cpu_topology_debug_force_logical_slot(uint32_t slot)
{
    cpu_topology_forced_slot = slot;
    cpu_topology_forced_slot_enabled = 1u;
    cpu_topology_source_name = "topology-forced-slot";
    return true;
}

void cpu_topology_debug_reset_discovery(void)
{
    cpu_topology_reset_discovery();
    cpu_topology_apic_id_valid = 0u;
    cpu_topology_local_apic_id = 0u;
    cpu_topology_source_name = "topology-debug-reset";
}

void cpu_topology_debug_clear_forced_logical_slot(void)
{
    cpu_topology_forced_slot_enabled = 0u;
    cpu_topology_forced_slot = 0u;
    if (cpu_topology_apic_id_valid)
        cpu_topology_source_name = "topology-cpuid-apic-id";
    else
        cpu_topology_source_name = "topology-cpuid-no-apic";
}

uint8_t cpu_topology_is_forced(void)
{
    return cpu_topology_forced_slot_enabled;
}