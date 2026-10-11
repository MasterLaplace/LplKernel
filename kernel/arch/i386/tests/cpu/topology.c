#include <kernel/cpu/acpi.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(cpu_topology, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Whether the topology is back as the test found it: forcing, counts and local identifier.
 */
static bool topology_test_is_restored(const CpuTopologySnapshot_t *found, uint8_t forced_before,
                                      uint32_t local_apic_before)
{
    return cpu_topology_is_forced() == forced_before &&
           cpu_topology_get_discovered_cpu_count() == found->discovered_cpu_count &&
           cpu_topology_get_online_cpu_count() == found->online_cpu_count &&
           cpu_topology_get_local_apic_id() == local_apic_before;
}

/**
 * @brief Sparse controller identifiers get compact logical slots, and a repeated one keeps its slot.
 */
KERNEL_TEST(sparse_identifiers_get_compact_slots)
{
    CpuTopologySnapshot_t found;
    const uint32_t local_apic_before = cpu_topology_get_local_apic_id();
    const uint8_t forced_before = cpu_topology_is_forced();

    cpu_topology_debug_save(&found);
    if (forced_before)
        cpu_topology_debug_clear_forced_logical_slot();
    cpu_topology_debug_reset_discovery();

    const uint32_t slot_of_0 = cpu_topology_register_discovered_apic_id(0u);
    const uint32_t slot_of_2 = cpu_topology_register_discovered_apic_id(2u);
    const uint32_t slot_of_4 = cpu_topology_register_discovered_apic_id(4u);
    const uint32_t slot_of_2_again = cpu_topology_register_discovered_apic_id(2u);
    const uint32_t discovered = cpu_topology_get_discovered_cpu_count();

    cpu_topology_debug_restore(&found);

    kernel_test_check(test, slot_of_0 == 0u && slot_of_2 == 1u && slot_of_4 == 2u,
                      "identifiers 0, 2 and 4 get slots 0, 1 and 2");
    kernel_test_check(test, slot_of_2_again == 1u, "an identifier registered twice keeps its slot");
    kernel_test_check(test, discovered == 3u, "three distinct identifiers count three processors");
    kernel_test_check(test, topology_test_is_restored(&found, forced_before, local_apic_before),
                      "the topology is put back as it was found");
}

/**
 * @brief Every processor the firmware's interrupt table enables is discovered.
 */
KERNEL_TEST(firmware_processors_are_discovered)
{
    if (!advanced_configuration_and_power_interface_madt_is_available())
    {
        kernel_test_skip(test, "the firmware has no interrupt controller table");
        return;
    }

    const uint32_t enabled = advanced_configuration_and_power_interface_madt_get_enabled_local_apic_count();
    const uint32_t local_slot = cpu_topology_register_discovered_apic_id(cpu_topology_get_local_apic_id());
    const uint32_t discovered = cpu_topology_get_discovered_cpu_count();

    kernel_test_check(test, discovered >= enabled, "every processor the firmware enables is discovered");
    kernel_test_check(test, local_slot < discovered, "the running processor has a slot among them");
    kernel_test_measure(test, "enabled", enabled);
}

/**
 * @brief Changing the running processor's identifier at run time moves it to that identifier's slot.
 */
KERNEL_TEST(runtime_identifier_selects_its_slot)
{
    CpuTopologySnapshot_t found;
    const uint32_t local_apic_before = cpu_topology_get_local_apic_id();
    const uint8_t forced_before = cpu_topology_is_forced();
    const uint32_t other_identifier = (local_apic_before + 2u) & 0xFFu;

    cpu_topology_debug_save(&found);
    if (forced_before)
        cpu_topology_debug_clear_forced_logical_slot();

    const uint32_t current_slot = cpu_topology_register_discovered_apic_id(local_apic_before);
    const uint32_t other_slot = cpu_topology_register_discovered_apic_id(other_identifier);

    cpu_topology_set_runtime_local_apic_id(other_identifier);
    const uint32_t slot_while_other = cpu_topology_get_logical_slot();
    cpu_topology_set_runtime_local_apic_id(local_apic_before);
    const uint32_t slot_when_back = cpu_topology_get_logical_slot();

    cpu_topology_debug_restore(&found);

    kernel_test_check(test, slot_while_other == other_slot, "the slot follows the identifier set at run time");
    kernel_test_check(test, slot_when_back == current_slot, "setting the identifier back restores the slot");
    kernel_test_check(test,
                      cpu_topology_get_local_apic_id() == local_apic_before &&
                          cpu_topology_get_discovered_cpu_count() == found.discovered_cpu_count,
                      "the topology is put back as it was found");
}

/**
 * @brief Marking a processor online counts it once, by slot or by identifier.
 *
 * @note The topology is restored before returning: the synthetic processor would otherwise stay
 *       online, inflate the online count, and make every later TLB shootdown spin waiting for an
 *       acknowledgement from a processor that does not exist.
 */
KERNEL_TEST(online_processors_are_counted_once)
{
    CpuTopologySnapshot_t found;

    cpu_topology_debug_save(&found);

    const uint32_t local_identifier = cpu_topology_get_local_apic_id();
    const uint32_t local_slot = cpu_topology_register_discovered_apic_id(local_identifier);
    const uint32_t online_before = cpu_topology_get_online_cpu_count();
    const bool local_was_online = cpu_topology_is_logical_slot_online(local_slot);

    cpu_topology_mark_runtime_cpu_online();

    const uint32_t online_after_local = cpu_topology_get_online_cpu_count();
    const bool local_is_online = cpu_topology_is_logical_slot_online(local_slot);
    const uint32_t neighbour_identifier = (local_identifier + 4u) & 0xFFu;
    const uint32_t neighbour_slot = cpu_topology_register_discovered_apic_id(neighbour_identifier);
    const bool neighbour_was_online = cpu_topology_is_logical_slot_online(neighbour_slot);

    cpu_topology_mark_apic_id_online(neighbour_identifier);

    const uint32_t online_after_neighbour = cpu_topology_get_online_cpu_count();
    const bool neighbour_is_online = cpu_topology_is_logical_slot_online(neighbour_slot);

    cpu_topology_debug_restore(&found);

    kernel_test_check(test, online_after_local == online_before + (local_was_online ? 0u : 1u),
                      "marking the running processor online counts it once");
    kernel_test_check(test, local_is_online, "its slot reads online");
    kernel_test_check(test, online_after_neighbour == online_after_local + (neighbour_was_online ? 0u : 1u),
                      "marking a neighbour online by identifier counts it once");
    kernel_test_check(test, neighbour_is_online, "the neighbour's slot reads online");
    kernel_test_check(test, cpu_topology_get_online_cpu_count() == found.online_cpu_count,
                      "the online count is put back as it was found");
}

/**
 * @brief A slot belongs to its own domain until bound elsewhere, and binding it back restores it.
 */
KERNEL_TEST(slots_bind_to_domains)
{
    const uint32_t local_slot = cpu_topology_get_logical_slot();
    const uint32_t other_slot = (local_slot + 1u) % CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC;

    kernel_test_check(test, cpu_topology_get_slot_domain(local_slot) == local_slot,
                      "a slot is its own domain by default");
    cpu_topology_bind_slot_to_domain(local_slot, 99u);
    kernel_test_check(test, cpu_topology_get_slot_domain(local_slot) == 99u, "binding moves it to that domain");
    cpu_topology_bind_slot_to_domain(local_slot, local_slot);
    kernel_test_check(test, cpu_topology_get_slot_domain(local_slot) == local_slot, "binding it back restores it");
    kernel_test_check(test, cpu_topology_get_slot_domain(other_slot) == other_slot,
                      "binding one slot leaves its neighbour alone");
}

/**
 * @brief Every processor the firmware enables comes online, whichever mode the local APIC runs in.
 *
 * @details In x2APIC mode the start-up interrupts were addressed as in xAPIC mode, and no application
 *          processor answered; nothing but the smp record said so.
 */
KERNEL_TEST(every_processor_the_firmware_enables_comes_online)
{
    if (!advanced_configuration_and_power_interface_madt_is_available())
    {
        kernel_test_skip(test, "no MADT names the processors");
        return;
    }

    const uint32_t enabled = advanced_configuration_and_power_interface_madt_get_enabled_local_apic_count();
    const uint32_t online = cpu_topology_get_online_cpu_count();

    kernel_test_check(test, online == enabled, "every processor the MADT enables is online");
    kernel_test_measure(test, "enabled", enabled);
    kernel_test_measure(test, "online", online);
    kernel_test_measure(test, "x2apic", apic_is_x2apic_active() ? 1u : 0u);
}
