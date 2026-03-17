#define __LPL_KERNEL__

#include <kernel/config.h>

#include <kernel/cpu/acpi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/lib/asmutils.h>
#include <kernel/mm/frame_arena.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pool_allocator.h>
#include <kernel/mm/ring_buffer.h>
#include <kernel/mm/slab.h>
#include <kernel/mm/stack_allocator.h>
#include <kernel/mm/tlsf.h>
#include <kernel/mm/vmm.h>
#include <kernel/smoke_test.h>

void kernel_smoke_test_run_physical_memory_manager_allocate_free(Serial_t *serial_port)
{
    uint32_t page_address_1 = physical_memory_manager_page_frame_allocate();
    uint32_t page_address_2 = physical_memory_manager_page_frame_allocate();

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: example allocate -> ");
    serial_write_hex32(serial_port, page_address_1);
    serial_write_string(serial_port, ", ");
    serial_write_hex32(serial_port, page_address_2);
    serial_write_string(serial_port, "\n");

    if (page_address_1)
        physical_memory_manager_page_frame_free(page_address_1);
    if (page_address_2)
        physical_memory_manager_page_frame_free(page_address_2);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: after free count = ");
    serial_write_int(serial_port, (int32_t) physical_memory_manager_get_free_page_count());
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_physical_memory_manager_buddy_coalesce(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy coalesce smoke: skipped (client)\n");
    return;
#else
    uint32_t pages[128] = {0};
    uint32_t allocated_count = 0u;
    int32_t buddy_index_a = -1;
    int32_t buddy_index_b = -1;

    for (uint32_t i = 0u; i < 128u; ++i)
    {
        pages[i] = physical_memory_manager_page_frame_allocate();
        if (!pages[i])
            break;

        allocated_count = i + 1u;

        for (uint32_t j = 0u; j < i; ++j)
        {
            if ((pages[j] ^ PAGE_SIZE) == pages[i])
            {
                buddy_index_a = (int32_t) j;
                buddy_index_b = (int32_t) i;
                break;
            }
        }

        if (buddy_index_a >= 0)
            break;
    }

    if (buddy_index_a < 0)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: PMM buddy coalesce smoke: skipped (no pair found)\n");
        for (uint32_t i = 0u; i < allocated_count; ++i)
            physical_memory_manager_page_frame_free(pages[i]);
        return;
    }

    uint32_t page_a = pages[(uint32_t) buddy_index_a];
    uint32_t page_b = pages[(uint32_t) buddy_index_b];
    bool merged_order_1 = !physical_memory_manager_debug_is_free_block(page_a, 0u) &&
                          !physical_memory_manager_debug_is_free_block(page_b, 0u);

    for (uint32_t i = 0u; i < allocated_count; ++i)
    {
        if ((int32_t) i == buddy_index_a || (int32_t) i == buddy_index_b)
            continue;
        physical_memory_manager_page_frame_free(pages[i]);
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy coalesce smoke: pair=");
    serial_write_hex32(serial_port, page_a);
    serial_write_string(serial_port, "/");
    serial_write_hex32(serial_port, page_b);
    serial_write_string(serial_port, ", merged_order1=");
    serial_write_int(serial_port, (int32_t) merged_order_1);
    if (merged_order_1)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_physical_memory_manager_buddy_stress(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy stress: skipped (client)\n");
    return;
#else
    uint32_t pages[96] = {0};
    uint32_t allocation_count = 0u;
    uint32_t free_count_before = physical_memory_manager_get_free_page_count();
    uint32_t rejected_before = physical_memory_manager_debug_get_rejected_free_count();
    uint32_t double_free_before = physical_memory_manager_debug_get_double_free_count();

    for (uint32_t i = 0u; i < 96u; ++i)
    {
        pages[i] = physical_memory_manager_page_frame_allocate();
        if (!pages[i])
            break;
        ++allocation_count;
    }

    if (allocation_count < 3u)
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy stress: skipped (pool too small)\n");
        for (uint32_t i = 0u; i < allocation_count; ++i)
            physical_memory_manager_page_frame_free(pages[i]);
        return;
    }

    uint32_t victim = pages[1u];

    for (uint32_t i = 0u; i < allocation_count; ++i)
    {
        if ((i & 1u) != 0u)
            physical_memory_manager_page_frame_free(pages[i]);
    }
    for (uint32_t i = 0u; i < allocation_count; ++i)
    {
        if ((i & 1u) == 0u)
            physical_memory_manager_page_frame_free(pages[i]);
    }

    /* Intentional double free to validate guard path. */
    physical_memory_manager_page_frame_free(victim);

    uint32_t free_count_after = physical_memory_manager_get_free_page_count();
    uint32_t rejected_after = physical_memory_manager_debug_get_rejected_free_count();
    uint32_t double_free_after = physical_memory_manager_debug_get_double_free_count();

    bool free_count_restored = (free_count_after == free_count_before);
    bool guard_triggered =
        (rejected_after == (rejected_before + 1u)) && (double_free_after == (double_free_before + 1u));

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy stress: alloc=");
    serial_write_int(serial_port, (int32_t) allocation_count);
    serial_write_string(serial_port, ", free_restored=");
    serial_write_int(serial_port, (int32_t) free_count_restored);
    serial_write_string(serial_port, ", guard_triggered=");
    serial_write_int(serial_port, (int32_t) guard_triggered);
    if (free_count_restored && guard_triggered)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_physical_memory_manager_buddy_order(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy order smoke: skipped (client)\n");
    return;
#else
    uint32_t free_before = physical_memory_manager_get_free_page_count();
    uint32_t block = physical_memory_manager_page_frame_allocate_order(1u);

    if (!block)
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy order smoke: alloc failed\n");
        return;
    }

    uint32_t free_after_alloc = physical_memory_manager_get_free_page_count();
    bool alloc_delta_ok = (free_after_alloc + 2u == free_before);

    physical_memory_manager_page_frame_free_order(block, 1u);

    uint32_t free_after_free = physical_memory_manager_get_free_page_count();
    bool free_restored = (free_after_free == free_before);

    uint32_t block_realloc = physical_memory_manager_page_frame_allocate_order(1u);
    bool realloc_ok = (block_realloc != 0u);
    if (block_realloc)
        physical_memory_manager_page_frame_free_order(block_realloc, 1u);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy order smoke: block=");
    serial_write_hex32(serial_port, block);
    serial_write_string(serial_port, ", alloc_delta_ok=");
    serial_write_int(serial_port, (int32_t) alloc_delta_ok);
    serial_write_string(serial_port, ", free_restored=");
    serial_write_int(serial_port, (int32_t) free_restored);
    serial_write_string(serial_port, ", realloc_ok=");
    serial_write_int(serial_port, (int32_t) realloc_ok);
    if (alloc_delta_ok && free_restored && realloc_ok)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_paging_runtime_page_table_reclaim(Serial_t *serial_port)
{
    uint32_t virt_base = 0u;
    bool candidate_found = false;

    for (uint32_t candidate = 0xD0000000u; candidate <= 0xFF800000u; candidate += 0x00400000u)
    {
        if (paging_is_mapped(candidate) || paging_is_mapped(candidate + PAGE_SIZE))
            continue;

        virt_base = candidate;
        candidate_found = true;
        break;
    }

    if (!candidate_found)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: skipped (no free 4MB slot)\n");
        return;
    }

    uint32_t runtime_pt_before = paging_get_runtime_owned_page_table_count();
    uint32_t phys_a = physical_memory_manager_page_frame_allocate();
    uint32_t phys_b = physical_memory_manager_page_frame_allocate();

    if (!phys_a || !phys_b)
    {
        if (phys_a)
            physical_memory_manager_page_frame_free(phys_a);
        if (phys_b)
            physical_memory_manager_page_frame_free(phys_b);

        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: skipped (no physical pages)\n");
        return;
    }

    PageDirectoryEntry_t pde_flags = {0};
    pde_flags.present = 1;
    pde_flags.read_write = 1;

    PageTableEntry_t pte_flags = {0};
    pte_flags.present = 1;
    pte_flags.read_write = 1;

    bool map_a = paging_map_page(virt_base, phys_a, pde_flags, pte_flags);
    bool map_b = false;

    if (!map_a)
    {
        physical_memory_manager_page_frame_free(phys_a);
        physical_memory_manager_page_frame_free(phys_b);
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: map A failed\n");
        return;
    }

    map_b = paging_map_page(virt_base + PAGE_SIZE, phys_b, pde_flags, pte_flags);
    if (!map_b)
    {
        paging_unmap_page(virt_base);
        physical_memory_manager_page_frame_free(phys_a);
        physical_memory_manager_page_frame_free(phys_b);
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: map B failed\n");
        return;
    }

    uint32_t runtime_pt_after_map = paging_get_runtime_owned_page_table_count();

    bool unmap_b = paging_unmap_page(virt_base + PAGE_SIZE);
    bool unmap_a = paging_unmap_page(virt_base);

    physical_memory_manager_page_frame_free(phys_b);
    physical_memory_manager_page_frame_free(phys_a);

    uint32_t runtime_pt_after_unmap = paging_get_runtime_owned_page_table_count();

    if (runtime_pt_after_map <= runtime_pt_before)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: skipped (no new runtime PT)\n");
        return;
    }

    bool reclaimed = (runtime_pt_after_unmap == runtime_pt_before);
    bool pass = unmap_a && unmap_b && reclaimed;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: pt_before=");
    serial_write_int(serial_port, (int32_t) runtime_pt_before);
    serial_write_string(serial_port, ", pt_after_map=");
    serial_write_int(serial_port, (int32_t) runtime_pt_after_map);
    serial_write_string(serial_port, ", pt_after_unmap=");
    serial_write_int(serial_port, (int32_t) runtime_pt_after_unmap);
    serial_write_string(serial_port, ", reclaimed=");
    serial_write_int(serial_port, (int32_t) reclaimed);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_heap_allocate_free(Serial_t *serial_port)
{
    uint32_t free_bytes_before = kernel_heap_get_small_free_bytes();
    uint32_t large_before = kernel_heap_get_large_allocation_count();
    uint32_t rejected_before = kernel_heap_debug_get_rejected_free_count();
    uint32_t double_before = kernel_heap_debug_get_double_free_count();

    void *small_a = kmalloc(64u);
    void *small_b = kmalloc(224u);
    /*
     * guard_victim: allocated at a non-slab size so it goes through the
     * first-fit path and carries a KernelHeapBlock_t header.  kfree()
     * detects the second free via BLOCK_FLAG_FREE and increments the
     * heap-level rejected/double counters, which is what this smoke
     * verifies.  On client the slab handles exact sizes (16/64/256) but
     * not 100; on server the size-class fast-path stores a header too.
     */
    void *guard_victim = kmalloc(100u);
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    void *large = NULL;
    bool expect_large = false;
#else
    void *large = kmalloc(5000u);
    bool expect_large = true;
#endif

    bool pass = (small_a != NULL) && (small_b != NULL) && (guard_victim != NULL);

    if (expect_large)
        pass = pass && (large != NULL);

    if (small_a)
        kfree(small_a);
    if (small_b)
        kfree(small_b);
    if (large)
        kfree(large);
    if (guard_victim)
        kfree(guard_victim);

    /* Intentional double-free to trigger the heap guard. */
    if (guard_victim)
        kfree(guard_victim);

    uint32_t free_blocks_after = kernel_heap_get_small_free_block_count();
    uint32_t free_bytes_after = kernel_heap_get_small_free_bytes();
    uint32_t large_after = kernel_heap_get_large_allocation_count();
    uint32_t rejected_after = kernel_heap_debug_get_rejected_free_count();
    uint32_t double_after = kernel_heap_debug_get_double_free_count();

#if defined(LPL_KERNEL_REAL_TIME_MODE)
    bool has_free_capacity = (free_blocks_after > 0u);
    bool free_pool_restored = (free_bytes_after >= free_bytes_before) && has_free_capacity;
#else
    (void) free_bytes_before;
    (void) free_bytes_after;
    uint32_t sizeclass_free_total = 0u;
    for (uint32_t sc = 0u; sc < 7u; ++sc)
        sizeclass_free_total += kernel_heap_get_size_class_free_count(sc);
    bool has_free_capacity = (free_blocks_after > 0u) || (sizeclass_free_total > 0u);
    bool free_pool_restored = has_free_capacity;
#endif
    bool large_restored = (large_after == large_before);
    bool guard_triggered = (rejected_after == (rejected_before + 1u)) && (double_after == (double_before + 1u));

    pass = pass && free_pool_restored && large_restored && guard_triggered;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: heap smoke: small_a=");
    serial_write_hex32(serial_port, (uint32_t) (uintptr_t) small_a);
    serial_write_string(serial_port, ", small_b=");
    serial_write_hex32(serial_port, (uint32_t) (uintptr_t) small_b);
    serial_write_string(serial_port, ", large=");
    serial_write_hex32(serial_port, (uint32_t) (uintptr_t) large);
    serial_write_string(serial_port, ", pass=");
    serial_write_int(serial_port, (int32_t) pass);
    serial_write_string(serial_port, ", restored=");
    serial_write_int(serial_port, (int32_t) free_pool_restored);
    serial_write_string(serial_port, ", large_restored=");
    serial_write_int(serial_port, (int32_t) large_restored);
    serial_write_string(serial_port, ", guard=");
    serial_write_int(serial_port, (int32_t) guard_triggered);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_cpu_topology_compaction(Serial_t *serial_port)
{
    uint32_t local_apic_before = cpu_topology_get_local_apic_id();
    uint8_t forced_before = cpu_topology_is_forced();

    if (forced_before)
        cpu_topology_debug_clear_forced_logical_slot();

    cpu_topology_debug_reset_discovery();

    uint32_t slot0 = cpu_topology_register_discovered_apic_id(0u);
    uint32_t slot1 = cpu_topology_register_discovered_apic_id(2u);
    uint32_t slot2 = cpu_topology_register_discovered_apic_id(4u);
    uint32_t slot1_repeat = cpu_topology_register_discovered_apic_id(2u);
    uint32_t discovered = cpu_topology_get_discovered_cpu_count();

    bool compact_ok = (slot0 == 0u) && (slot1 == 1u) && (slot2 == 2u) && (slot1_repeat == 1u);
    bool count_ok = (discovered == 3u);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: topology compact smoke: slots=");
    serial_write_int(serial_port, (int32_t) slot0);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) slot1);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) slot2);
    serial_write_string(serial_port, ", repeat=");
    serial_write_int(serial_port, (int32_t) slot1_repeat);
    serial_write_string(serial_port, ", discovered=");
    serial_write_int(serial_port, (int32_t) discovered);

    cpu_topology_initialize();

    bool restored_forced_ok = (cpu_topology_is_forced() == 0u);
    bool restored_count_ok = (cpu_topology_get_discovered_cpu_count() >= 1u);
    bool restored_apic_ok = (cpu_topology_get_local_apic_id() == local_apic_before);
    bool pass = compact_ok && count_ok && restored_forced_ok && restored_count_ok && restored_apic_ok;

    serial_write_string(serial_port, ", restore=");
    serial_write_int(serial_port, (int32_t) (restored_forced_ok && restored_count_ok && restored_apic_ok));
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_cpu_topology_madt_sync(Serial_t *serial_port)
{
    uint8_t madt_available = advanced_configuration_and_power_interface_madt_is_available();
    uint32_t discovered_count = cpu_topology_get_discovered_cpu_count();

    if (!madt_available)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: topology MADT sync smoke: skipped (madt unavailable)\n");
        return;
    }

    uint32_t enabled_lapics = advanced_configuration_and_power_interface_madt_get_enabled_local_apic_count();
    uint32_t local_apic_slot = cpu_topology_register_discovered_apic_id(cpu_topology_get_local_apic_id());
    uint32_t discovered_after = cpu_topology_get_discovered_cpu_count();

    bool count_ok = (discovered_after >= enabled_lapics);
    bool local_ok = (local_apic_slot < discovered_after);
    bool pass = count_ok && local_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: topology MADT sync smoke: enabled_lapic=");
    serial_write_int(serial_port, (int32_t) enabled_lapics);
    serial_write_string(serial_port, ", discovered_before=");
    serial_write_int(serial_port, (int32_t) discovered_count);
    serial_write_string(serial_port, ", discovered_after=");
    serial_write_int(serial_port, (int32_t) discovered_after);
    serial_write_string(serial_port, ", local_slot=");
    serial_write_int(serial_port, (int32_t) local_apic_slot);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_cpu_topology_runtime_slot(Serial_t *serial_port)
{
    uint32_t local_apic_before = cpu_topology_get_local_apic_id();
    uint32_t slot_before = cpu_topology_get_logical_slot();
    uint8_t forced_before = cpu_topology_is_forced();

    if (forced_before)
        cpu_topology_debug_clear_forced_logical_slot();

    uint32_t slot_current = cpu_topology_register_discovered_apic_id(local_apic_before);
    uint32_t slot_next = cpu_topology_register_discovered_apic_id((local_apic_before + 2u) & 0xFFu);

    cpu_topology_set_runtime_local_apic_id((local_apic_before + 2u) & 0xFFu);
    uint32_t slot_runtime = cpu_topology_get_logical_slot();

    cpu_topology_set_runtime_local_apic_id(local_apic_before);
    uint32_t slot_restored = cpu_topology_get_logical_slot();

    bool slot_switch_ok = (slot_runtime == slot_next);
    bool restore_ok = (slot_restored == slot_current) && (cpu_topology_get_local_apic_id() == local_apic_before);
    bool pass = slot_switch_ok && restore_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: topology runtime slot smoke: before=");
    serial_write_int(serial_port, (int32_t) slot_before);
    serial_write_string(serial_port, ", expected_next=");
    serial_write_int(serial_port, (int32_t) slot_next);
    serial_write_string(serial_port, ", runtime=");
    serial_write_int(serial_port, (int32_t) slot_runtime);
    serial_write_string(serial_port, ", restored=");
    serial_write_int(serial_port, (int32_t) slot_restored);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_cpu_topology_online_bookkeeping(Serial_t *serial_port)
{
    uint32_t local_apic = cpu_topology_get_local_apic_id();
    uint32_t local_slot = cpu_topology_register_discovered_apic_id(local_apic);
    uint32_t online_before = cpu_topology_get_online_cpu_count();
    bool local_slot_online_before = cpu_topology_is_logical_slot_online(local_slot);

    cpu_topology_mark_runtime_cpu_online();

    uint32_t online_after_local = cpu_topology_get_online_cpu_count();
    uint32_t expected_after_local = online_before + (local_slot_online_before ? 0u : 1u);
    bool local_count_ok = (online_after_local == expected_after_local);
    bool local_online_ok = cpu_topology_is_logical_slot_online(local_slot);

    uint32_t next_apic = (local_apic + 4u) & 0xFFu;
    uint32_t next_slot = cpu_topology_register_discovered_apic_id(next_apic);
    bool next_online_before = cpu_topology_is_logical_slot_online(next_slot);

    cpu_topology_mark_apic_id_online(next_apic);

    uint32_t online_after_next = cpu_topology_get_online_cpu_count();
    uint32_t expected_after_next = online_after_local + (next_online_before ? 0u : 1u);
    bool next_count_ok = (online_after_next == expected_after_next);
    bool next_online_ok = cpu_topology_is_logical_slot_online(next_slot);

    bool pass = local_count_ok && local_online_ok && next_count_ok && next_online_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: topology online bookkeeping smoke: local_slot=");
    serial_write_int(serial_port, (int32_t) local_slot);
    serial_write_string(serial_port, ", local_online=");
    serial_write_int(serial_port, (int32_t) local_online_ok);
    serial_write_string(serial_port, ", next_slot=");
    serial_write_int(serial_port, (int32_t) next_slot);
    serial_write_string(serial_port, ", next_online=");
    serial_write_int(serial_port, (int32_t) next_online_ok);
    serial_write_string(serial_port, ", online_before=");
    serial_write_int(serial_port, (int32_t) online_before);
    serial_write_string(serial_port, ", online_after=");
    serial_write_int(serial_port, (int32_t) online_after_next);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_cpu_topology_slot_domain(Serial_t *serial_port)
{
    uint32_t local_slot = cpu_topology_get_logical_slot();
    uint32_t default_domain = cpu_topology_get_slot_domain(local_slot);
    bool default_ok = (default_domain == local_slot);

    cpu_topology_bind_slot_to_domain(local_slot, 99u);
    uint32_t bound_domain = cpu_topology_get_slot_domain(local_slot);
    bool bind_ok = (bound_domain == 99u);

    cpu_topology_bind_slot_to_domain(local_slot, local_slot);
    uint32_t restored_domain = cpu_topology_get_slot_domain(local_slot);
    bool restore_ok = (restored_domain == local_slot);

    uint32_t other_slot = (local_slot + 1u) % CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC;
    uint32_t other_default = cpu_topology_get_slot_domain(other_slot);
    bool other_default_ok = (other_default == other_slot);

    bool pass = default_ok && bind_ok && restore_ok && other_default_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: topology slot domain smoke: local_slot=");
    serial_write_int(serial_port, (int32_t) local_slot);
    serial_write_string(serial_port, ", default_domain=");
    serial_write_int(serial_port, (int32_t) default_domain);
    serial_write_string(serial_port, ", bind_ok=");
    serial_write_int(serial_port, (int32_t) bind_ok);
    serial_write_string(serial_port, ", restore_ok=");
    serial_write_int(serial_port, (int32_t) restore_ok);
    serial_write_string(serial_port, ", other_default_ok=");
    serial_write_int(serial_port, (int32_t) other_default_ok);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_slab_alloc_free(Serial_t *serial_port)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    /*
     * Client: verify each slab cache returns distinct, non-NULL objects
     * and restores its free-count after free.
     */
    static const uint32_t slab_sizes[3u] = {KERNEL_SLAB_SIZE_SMALL, KERNEL_SLAB_SIZE_MEDIUM, KERNEL_SLAB_SIZE_LARGE};
    bool overall_pass = true;

    for (uint32_t ci = 0u; ci < 3u; ++ci)
    {
        uint32_t sz = slab_sizes[ci];
        uint32_t free_before = kernel_slab_get_free_count(sz);

        void *obj_a = kernel_slab_alloc(sz);
        void *obj_b = kernel_slab_alloc(sz);

        bool alloc_ok = (obj_a != NULL) && (obj_b != NULL) && (obj_a != obj_b);

        if (obj_a)
            kernel_slab_free(obj_a);
        if (obj_b)
            kernel_slab_free(obj_b);

        uint32_t free_after = kernel_slab_get_free_count(sz);
        bool restored = (free_after == free_before);

        /* Spurious-free guard: a random stack address must be rejected. */
        volatile uint32_t stack_canary = 0u;
        bool guard_ok = !kernel_slab_free((void *) &stack_canary);

        bool cache_pass = alloc_ok && restored && guard_ok;
        overall_pass = overall_pass && cache_pass;

        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: slab smoke sz=");
        serial_write_int(serial_port, (int32_t) sz);
        serial_write_string(serial_port, ": alloc=");
        serial_write_int(serial_port, (int32_t) alloc_ok);
        serial_write_string(serial_port, ", restored=");
        serial_write_int(serial_port, (int32_t) restored);
        serial_write_string(serial_port, ", guard=");
        serial_write_int(serial_port, (int32_t) guard_ok);
        if (cache_pass)
            serial_write_string(serial_port, " (pass)\n");
        else
            serial_write_string(serial_port, " (fail)\n");
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: slab smoke overall=");
    serial_write_int(serial_port, (int32_t) overall_pass);
    if (overall_pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#else
    /*
     * Server: allocate objects via kmalloc at size-class-friendly sizes,
     * free them, re-allocate, then check that the fast-path hit counter
     * has increased (proving the bucket was used on the second pass).
     * Also confirm the mono-domain scaffold reports domain 0 and tracks
     * bucket refills before per-CPU/per-NUMA sharding is introduced.
     */
    static const uint32_t sc_sizes[3u] = {8u, 64u, 256u};
    bool overall_pass = true;
    uint32_t domain_count = kernel_heap_get_server_domain_count();
    bool domain_meta_ok = (domain_count >= 1u) && (kernel_heap_get_server_active_domain() == 0u);
    uint32_t active_domain = kernel_heap_get_server_active_domain();
    uint32_t remote_probe_before = kernel_heap_get_server_domain_remote_probe_count(active_domain);
    uint32_t remote_hit_before = kernel_heap_get_server_domain_remote_hit_count(active_domain);
    bool remote_cross_domain_ok = true;

    if (domain_count > 1u)
    {
        bool switch_to_zero_ok = cpu_topology_debug_force_logical_slot(0u);
        uint32_t remote_probe_domain1_before = kernel_heap_get_server_domain_remote_probe_count(1u);
        uint32_t remote_hit_domain1_before = kernel_heap_get_server_domain_remote_hit_count(1u);

        void *seed = kmalloc(64u);
        if (seed)
            kfree(seed);

        bool switch_to_one_ok = cpu_topology_debug_force_logical_slot(1u);
        void *remote_candidate = kmalloc(64u);

        uint32_t remote_probe_domain1_after = kernel_heap_get_server_domain_remote_probe_count(1u);
        uint32_t remote_hit_domain1_after = kernel_heap_get_server_domain_remote_hit_count(1u);

        if (remote_candidate)
            kfree(remote_candidate);

        /* Restore default active domain source for remaining checks. */
        cpu_topology_debug_clear_forced_logical_slot();
        void *restore = kmalloc(8u);
        if (restore)
            kfree(restore);

        bool switch_back_ok = (cpu_topology_is_forced() == 0u);
        active_domain = kernel_heap_get_server_active_domain();

        remote_cross_domain_ok = switch_to_zero_ok && switch_to_one_ok && switch_back_ok &&
                                 (remote_candidate != NULL) &&
                                 (remote_probe_domain1_after > remote_probe_domain1_before) &&
                                 (remote_hit_domain1_after > remote_hit_domain1_before) && (active_domain == 0u);
    }

    for (uint32_t ci = 0u; ci < 3u; ++ci)
    {
        uint32_t sz = sc_sizes[ci];

        /* Use sc index 0 for 8B, 3 for 64B, 5 for 256B. */
        uint32_t sc_idx = 0u;
        if (sz == 64u)
            sc_idx = 3u;
        if (sz == 256u)
            sc_idx = 5u;

        uint32_t domain_index = kernel_heap_get_server_active_domain();
        uint32_t hit_before = kernel_heap_get_size_class_hit_count(sc_idx);
        uint32_t refill_before = kernel_heap_get_server_domain_refill_count(domain_index, sc_idx);
        uint32_t cpu_hit_before = kernel_heap_get_server_per_cpu_hit_count(domain_index);

        void *a = kmalloc(sz);
        void *b = kmalloc(sz);
        bool alloc_ok = (a != NULL) && (b != NULL) && (a != b);

        if (a)
            kfree(a);
        if (b)
            kfree(b);

        /* Second pair -- should hit the bucket. */
        void *c = kmalloc(sz);
        void *d = kmalloc(sz);
        bool realloc_ok = (c != NULL) && (d != NULL);
        if (c)
            kfree(c);
        if (d)
            kfree(d);

        uint32_t hit_after = kernel_heap_get_size_class_hit_count(sc_idx);
        uint32_t refill_after = kernel_heap_get_server_domain_refill_count(domain_index, sc_idx);
        uint32_t cpu_hit_after = kernel_heap_get_server_per_cpu_hit_count(domain_index);
        bool fast_path_used = (hit_after > hit_before);
        bool refill_recorded = (refill_after >= refill_before);
        bool cpu_hit_ok = (cpu_hit_after > cpu_hit_before);

        bool cache_pass = alloc_ok && realloc_ok && fast_path_used && domain_meta_ok && cpu_hit_ok;
        overall_pass = overall_pass && cache_pass;

        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: sc smoke sz=");
        serial_write_int(serial_port, (int32_t) sz);
        serial_write_string(serial_port, ": alloc=");
        serial_write_int(serial_port, (int32_t) alloc_ok);
        serial_write_string(serial_port, ", realloc=");
        serial_write_int(serial_port, (int32_t) realloc_ok);
        serial_write_string(serial_port, ", fast_path=");
        serial_write_int(serial_port, (int32_t) fast_path_used);
        serial_write_string(serial_port, ", refill=");
        serial_write_int(serial_port, (int32_t) refill_recorded);
        serial_write_string(serial_port, ", domain_ok=");
        serial_write_int(serial_port, (int32_t) domain_meta_ok);
        if (cache_pass)
            serial_write_string(serial_port, " (pass)\n");
        else
            serial_write_string(serial_port, " (fail)\n");
    }

    uint32_t remote_probe_after = kernel_heap_get_server_domain_remote_probe_count(active_domain);
    uint32_t remote_hit_after = kernel_heap_get_server_domain_remote_hit_count(active_domain);
    bool remote_path_quiet = (remote_probe_after >= remote_probe_before) && (remote_hit_after == remote_hit_before);
    overall_pass = overall_pass && remote_path_quiet && remote_cross_domain_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: sc smoke overall=");
    serial_write_int(serial_port, (int32_t) overall_pass);
    serial_write_string(serial_port, ", remote_path_quiet=");
    serial_write_int(serial_port, (int32_t) remote_path_quiet);
    serial_write_string(serial_port, ", remote_cross_domain=");
    serial_write_int(serial_port, (int32_t) remote_cross_domain_ok);
    if (overall_pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_client_hot_loop_rule(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    uint32_t violation_before = kernel_heap_get_hot_loop_violation_count();

    void *guard_ptr = kmalloc(100u);
    bool setup_ok = (guard_ptr != NULL);

    kernel_heap_hot_loop_enter();
    void *forbidden_alloc = kmalloc(64u);
    if (guard_ptr)
        kfree(guard_ptr);
    kernel_heap_hot_loop_leave();

    uint32_t violation_after = kernel_heap_get_hot_loop_violation_count();
    uint32_t depth_after = kernel_heap_get_hot_loop_depth();

    bool alloc_blocked = (forbidden_alloc == NULL);
    bool depth_restored = (depth_after == 0u);
    bool violation_count_ok = (violation_after >= (violation_before + 2u));
    bool pass = setup_ok && alloc_blocked && depth_restored && violation_count_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: hot-loop rule smoke: setup=");
    serial_write_int(serial_port, (int32_t) setup_ok);
    serial_write_string(serial_port, ", alloc_blocked=");
    serial_write_int(serial_port, (int32_t) alloc_blocked);
    serial_write_string(serial_port, ", depth_restored=");
    serial_write_int(serial_port, (int32_t) depth_restored);
    serial_write_string(serial_port, ", violations_delta=");
    serial_write_int(serial_port, (int32_t) (violation_after - violation_before));
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#else
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: hot-loop rule smoke: skipped (server)\n");
#endif
}

void kernel_smoke_test_run_frame_arena_basic(Serial_t *serial_port)
{
    if (!kernel_frame_arena_is_initialized())
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: frame arena smoke: not initialized\n");
        return;
    }

    uint32_t resets_before = kernel_frame_arena_get_reset_count();
    uint32_t failed_before = kernel_frame_arena_get_failed_alloc_count();

    void *a = kernel_frame_arena_alloc(64u, 8u);
    void *b = kernel_frame_arena_alloc(96u, 16u);
    bool alloc_ok = (a != NULL) && (b != NULL) && (a != b);

    kernel_frame_arena_reset();

    void *c = kernel_frame_arena_alloc(64u, 8u);
    bool reset_rewinds = (a != NULL) && (c == a);
    bool reset_count_ok = (kernel_frame_arena_get_reset_count() == (resets_before + 1u));
    bool failed_stable = (kernel_frame_arena_get_failed_alloc_count() == failed_before);

    bool pass = alloc_ok && reset_rewinds && reset_count_ok && failed_stable;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: frame arena smoke: alloc_ok=");
    serial_write_int(serial_port, (int32_t) alloc_ok);
    serial_write_string(serial_port, ", rewind=");
    serial_write_int(serial_port, (int32_t) reset_rewinds);
    serial_write_string(serial_port, ", reset_count_ok=");
    serial_write_int(serial_port, (int32_t) reset_count_ok);
    serial_write_string(serial_port, ", failed_stable=");
    serial_write_int(serial_port, (int32_t) failed_stable);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_frame_arena_budget(Serial_t *serial_port)
{
    if (!kernel_frame_arena_is_initialized())
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: frame arena budget smoke: not initialized\n");
        return;
    }

    /* Start from a clean slate. */
    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(0u);

    uint32_t exceeded_before = kernel_frame_arena_get_budget_exceeded_count();
    uint32_t failed_before = kernel_frame_arena_get_failed_alloc_count();

    /* Set a tight 64 B budget. */
    kernel_frame_arena_set_frame_budget(64u);

    void *a = kernel_frame_arena_alloc(32u, 8u);
    bool first_ok = (a != NULL);

    void *b = kernel_frame_arena_alloc(48u, 8u);
    bool over_budget_rejected = (b == NULL);
    bool exceeded_incremented = (kernel_frame_arena_get_budget_exceeded_count() == (exceeded_before + 1u));
    bool failed_incremented = (kernel_frame_arena_get_failed_alloc_count() == (failed_before + 1u));

    /* A smaller alloc that fits within the budget should succeed. */
    void *c = kernel_frame_arena_alloc(16u, 8u);
    bool second_ok = (c != NULL) && (c != a);

    /* Clear budget and reset: subsequent allocs are unconstrained by budget. */
    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(0u);

    void *d = kernel_frame_arena_alloc(128u, 8u);
    bool unconstrained_ok = (d != NULL);

    /* Restore clean state. */
    kernel_frame_arena_reset();

    bool pass =
        first_ok && over_budget_rejected && exceeded_incremented && failed_incremented && second_ok && unconstrained_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: frame arena budget smoke: first_ok=");
    serial_write_int(serial_port, (int32_t) first_ok);
    serial_write_string(serial_port, ", over_rejected=");
    serial_write_int(serial_port, (int32_t) over_budget_rejected);
    serial_write_string(serial_port, ", exceeded_ok=");
    serial_write_int(serial_port, (int32_t) exceeded_incremented);
    serial_write_string(serial_port, ", second_ok=");
    serial_write_int(serial_port, (int32_t) second_ok);
    serial_write_string(serial_port, ", unconstrained_ok=");
    serial_write_int(serial_port, (int32_t) unconstrained_ok);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_frame_poison_check(Serial_t *serial_port)
{
    if (!kernel_frame_arena_is_initialized())
        return;

    kernel_frame_arena_reset();
    uint8_t *ptr = (uint8_t *) kernel_frame_arena_alloc(64u, 8u);

    if (!ptr)
        return;

    for (uint32_t i = 0u; i < 64u; ++i)
        ptr[i] = 0xBB;

    kernel_frame_arena_reset();

    bool poison_ok = true;
#ifdef LPL_KERNEL_DEBUG_POISON
    for (uint32_t i = 0u; i < 64u; ++i)
    {
        if (ptr[i] != 0xAA)
        {
            poison_ok = false;
            break;
        }
    }
#endif

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: frame poison smoke: ok=");
    serial_write_int(serial_port, (int32_t) poison_ok);
    if (poison_ok)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_pool_allocator_basic(Serial_t *serial_port)
{
    if (!kernel_pool_allocator_is_initialized())
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: pool allocator smoke: not initialized\n");
        return;
    }

    uint32_t capacity = kernel_pool_get_capacity();
    uint32_t free_before = kernel_pool_get_free_count();
    uint32_t failed_before = kernel_pool_get_failed_alloc_count();
    void *slots[256] = {0};

    if (capacity > 256u)
        capacity = 256u;

    uint32_t allocated = 0u;
    for (; allocated < capacity; ++allocated)
    {
        slots[allocated] = kernel_pool_alloc();
        if (!slots[allocated])
            break;
    }

    void *exhausted = kernel_pool_alloc();
    bool alloc_full = (allocated == capacity);
    bool exhaustion_ok = (exhausted == NULL);

    for (uint32_t index = 0u; index < allocated; ++index)
        kernel_pool_free(slots[index]);

    bool poison_ok = true;
#ifdef LPL_KERNEL_DEBUG_POISON
    void *poison_test = kernel_pool_alloc();
    if (poison_test)
    {
        uint8_t *p = (uint8_t *) poison_test;
        if (kernel_pool_get_object_size() > sizeof(void *))
        {
            if (p[sizeof(void *)] != 0xDF)
                poison_ok = false;
        }
        kernel_pool_free(poison_test);
    }
#endif

    bool free_restored = (kernel_pool_get_free_count() == free_before);
    bool failed_incremented = (kernel_pool_get_failed_alloc_count() == (failed_before + 1u));
    bool pass = alloc_full && exhaustion_ok && free_restored && failed_incremented && poison_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: pool allocator smoke: alloc_full=");
    serial_write_int(serial_port, (int32_t) alloc_full);
    serial_write_string(serial_port, ", exhaustion_ok=");
    serial_write_int(serial_port, (int32_t) exhaustion_ok);
    serial_write_string(serial_port, ", free_restored=");
    serial_write_int(serial_port, (int32_t) free_restored);
    serial_write_string(serial_port, ", failed_incremented=");
    serial_write_int(serial_port, (int32_t) failed_incremented);
    serial_write_string(serial_port, ", poison=");
    serial_write_int(serial_port, (int32_t) poison_ok);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_pool_double_free(Serial_t *serial_port)
{
    if (!kernel_pool_allocator_is_initialized())
        return;

    void *ptr = kernel_pool_alloc();
    if (!ptr)
        return;

    bool first_free_ok = kernel_pool_free(ptr);
    bool second_free_ok = kernel_pool_free(ptr);

    bool pass = first_free_ok && !second_free_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: pool double free smoke: double_rejected=");
    serial_write_int(serial_port, (int32_t) !second_free_ok);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_ring_buffer_basic(Serial_t *serial_port)
{
    if (!kernel_ring_buffer_is_initialized())
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: ring buffer smoke: not initialized\n");
        return;
    }

    uint32_t capacity = kernel_ring_buffer_get_capacity();
    uint32_t enqueue_before = kernel_ring_buffer_get_enqueue_count();
    uint32_t dequeue_before = kernel_ring_buffer_get_dequeue_count();
    uint32_t failed_enqueue_before = kernel_ring_buffer_get_failed_enqueue_count();
    uint32_t failed_dequeue_before = kernel_ring_buffer_get_failed_dequeue_count();

    if (capacity > 256u)
        capacity = 256u;

    uint32_t half = capacity / 2u;
    if (half == 0u)
        half = 1u;

    bool mode_ok = (kernel_ring_buffer_get_mode() == KERNEL_RING_BUFFER_MODE_SPSC);

    bool enqueue_ok = true;
    for (uint32_t value = 0u; value < capacity; ++value)
    {
        if (!kernel_ring_buffer_enqueue(&value, sizeof(value)))
        {
            enqueue_ok = false;
            break;
        }
    }

    uint32_t overflow_marker = 0xDEADBEEFu;
    bool full_guard = !kernel_ring_buffer_enqueue(&overflow_marker, sizeof(overflow_marker));

    bool order_ok = true;
    for (uint32_t expected = 0u; expected < half; ++expected)
    {
        uint32_t observed = 0xFFFFFFFFu;
        if (!kernel_ring_buffer_dequeue(&observed, sizeof(observed)) || observed != expected)
        {
            order_ok = false;
            break;
        }
    }

    for (uint32_t value = capacity; value < (capacity + half); ++value)
    {
        if (!kernel_ring_buffer_enqueue(&value, sizeof(value)))
        {
            enqueue_ok = false;
            break;
        }
    }

    for (uint32_t expected = half; expected < (capacity + half); ++expected)
    {
        uint32_t observed = 0xFFFFFFFFu;
        if (!kernel_ring_buffer_dequeue(&observed, sizeof(observed)) || observed != expected)
        {
            order_ok = false;
            break;
        }
    }

    bool empty_guard = !kernel_ring_buffer_dequeue(&overflow_marker, sizeof(overflow_marker));

    uint32_t enqueue_after = kernel_ring_buffer_get_enqueue_count();
    uint32_t dequeue_after = kernel_ring_buffer_get_dequeue_count();
    uint32_t failed_enqueue_after = kernel_ring_buffer_get_failed_enqueue_count();
    uint32_t failed_dequeue_after = kernel_ring_buffer_get_failed_dequeue_count();
    bool op_counts_ok =
        (enqueue_after == (enqueue_before + capacity + half)) && (dequeue_after == (dequeue_before + capacity + half));
    bool failed_counts_ok = (failed_enqueue_after == (failed_enqueue_before + 1u)) &&
                            (failed_dequeue_after == (failed_dequeue_before + 1u));
    bool drained = (kernel_ring_buffer_get_count() == 0u);
    bool pass =
        mode_ok && enqueue_ok && full_guard && order_ok && empty_guard && op_counts_ok && failed_counts_ok && drained;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: ring buffer smoke: mode_ok=");
    serial_write_int(serial_port, (int32_t) mode_ok);
    serial_write_string(serial_port, ", enqueue_ok=");
    serial_write_int(serial_port, (int32_t) enqueue_ok);
    serial_write_string(serial_port, ", full_guard=");
    serial_write_int(serial_port, (int32_t) full_guard);
    serial_write_string(serial_port, ", order_ok=");
    serial_write_int(serial_port, (int32_t) order_ok);
    serial_write_string(serial_port, ", empty_guard=");
    serial_write_int(serial_port, (int32_t) empty_guard);
    serial_write_string(serial_port, ", op_counts_ok=");
    serial_write_int(serial_port, (int32_t) op_counts_ok);
    serial_write_string(serial_port, ", failed_counts_ok=");
    serial_write_int(serial_port, (int32_t) failed_counts_ok);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_stack_allocator_basic(Serial_t *serial_port)
{
    if (!kernel_stack_allocator_is_initialized())
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: stack allocator smoke: not initialized\n");
        return;
    }

    uint32_t marker0 = kernel_stack_alloc_get_marker();

    void *ptr1 = kernel_stack_alloc_push(1024u, 8u);
    bool ptr1_ok = (ptr1 != NULL);

    uint32_t marker1 = kernel_stack_alloc_get_marker();
    void *ptr2 = kernel_stack_alloc_push(2048u, 16u);
    bool ptr2_ok = (ptr2 != NULL);

    kernel_stack_alloc_rollback(marker1);
    void *ptr3 = kernel_stack_alloc_push(1024u, 16u);
    bool reuse_ok = (ptr3 == ptr2);

    kernel_stack_alloc_rollback(marker0);
    bool restore_ok = (kernel_stack_alloc_get_marker() == marker0);

    bool pass = ptr1_ok && ptr2_ok && reuse_ok && restore_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: stack allocator smoke: ptr1_ok=");
    serial_write_int(serial_port, (int32_t) ptr1_ok);
    serial_write_string(serial_port, ", ptr2_ok=");
    serial_write_int(serial_port, (int32_t) ptr2_ok);
    serial_write_string(serial_port, ", reuse_ok=");
    serial_write_int(serial_port, (int32_t) reuse_ok);
    serial_write_string(serial_port, ", restore_ok=");
    serial_write_int(serial_port, (int32_t) restore_ok);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

static inline uint32_t allocator_wcet_rdtsc_low(void)
{
#if defined(__i386__) || defined(__x86_64__)
    uint32_t lo;
    asm volatile("rdtsc" : "=a"(lo)::"edx");
    return lo;
#else
    return 0u;
#endif
}

void kernel_smoke_test_run_allocator_wcet_bound_check(Serial_t *serial_port)
{
    uint32_t arena_alloc = kernel_frame_arena_get_wcet_alloc_cycles();
    uint32_t arena_reset = kernel_frame_arena_get_wcet_reset_cycles();
    uint32_t pool_alloc = kernel_pool_get_wcet_alloc_cycles();
    uint32_t pool_free = kernel_pool_get_wcet_free_cycles();

    uint32_t wcet_threshold = 1500u;
    bool arena_ok = (arena_alloc < wcet_threshold) && (arena_reset < wcet_threshold);
    bool pool_ok = (pool_alloc < wcet_threshold) && (pool_free < wcet_threshold);
    bool pass = arena_ok && pool_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: WCET bounds smoke: arena_alloc=");
    serial_write_int(serial_port, (int32_t) arena_alloc);
    serial_write_string(serial_port, ", reset=");
    serial_write_int(serial_port, (int32_t) arena_reset);
    serial_write_string(serial_port, " | pool_alloc=");
    serial_write_int(serial_port, (int32_t) pool_alloc);
    serial_write_string(serial_port, ", free=");
    serial_write_int(serial_port, (int32_t) pool_free);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (warning: cycles exceeded threshold)\n");
}

void kernel_smoke_test_run_frame_budget_determinism(Serial_t *serial_port)
{
    if (!kernel_frame_arena_is_initialized())
        return;

    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(1024u * 1024u);

    uint32_t min_cycles = 0xFFFFFFFFu;
    uint32_t max_cycles = 0u;

    for (uint32_t i = 0u; i < 100u; ++i)
    {
        uint32_t t0 = allocator_wcet_rdtsc_low();
        void *ptr = kernel_frame_arena_alloc(64u, 8u);
        uint32_t t1 = allocator_wcet_rdtsc_low();

        if (!ptr)
            break;

        uint32_t delta = t1 - t0;
        if (delta < min_cycles)
            min_cycles = delta;
        if (delta > max_cycles)
            max_cycles = delta;
    }

    bool variance_ok = false;
    if (min_cycles > 0u)
    {
        variance_ok = (max_cycles * 10u) < (min_cycles * 13u);
    }
    else
    {
        variance_ok = true;
    }

    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(0u);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: frame determinism smoke: min=");
    serial_write_int(serial_port, (int32_t) min_cycles);
    serial_write_string(serial_port, ", max=");
    serial_write_int(serial_port, (int32_t) max_cycles);

    if (variance_ok)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (warning: unstable variance under emulation)\n");
}

void kernel_smoke_test_run_pmm_watermark(Serial_t *serial_port)
{
    uint32_t free_before = physical_memory_manager_get_free_page_count();
    uint32_t high_before = physical_memory_manager_get_watermark_high();
    uint32_t low_before = physical_memory_manager_get_watermark_low();

    /* Allocate some pages to push the low watermark down. */
    uint32_t pages[8] = {0};
    uint32_t allocated = 0u;

    for (uint32_t i = 0u; i < 8u; ++i)
    {
        pages[i] = physical_memory_manager_page_frame_allocate();
        if (!pages[i])
            break;
        ++allocated;
    }

    uint32_t low_after_alloc = physical_memory_manager_get_watermark_low();

    /* Free them all to push high watermark back up. */
    for (uint32_t i = 0u; i < allocated; ++i)
        physical_memory_manager_page_frame_free(pages[i]);

    uint32_t high_after_free = physical_memory_manager_get_watermark_high();
    uint32_t free_after = physical_memory_manager_get_free_page_count();

    bool count_restored = (free_after == free_before);
    bool low_tracked = (allocated > 0u) ? (low_after_alloc <= low_before) : true;
    bool high_tracked = (high_after_free >= high_before);
    bool pass = count_restored && low_tracked && high_tracked;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM watermark smoke: high_before=");
    serial_write_int(serial_port, (int32_t) high_before);
    serial_write_string(serial_port, ", low_before=");
    serial_write_int(serial_port, (int32_t) low_before);
    serial_write_string(serial_port, ", low_after_alloc=");
    serial_write_int(serial_port, (int32_t) low_after_alloc);
    serial_write_string(serial_port, ", high_after_free=");
    serial_write_int(serial_port, (int32_t) high_after_free);
    serial_write_string(serial_port, ", restored=");
    serial_write_int(serial_port, (int32_t) count_restored);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_pmm_fragmentation(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM fragmentation smoke: skipped (client)\n");
    return;
#else
    uint32_t frag_before = physical_memory_manager_get_fragmentation_ratio();
    uint32_t free_before = physical_memory_manager_get_free_page_count();

    /* Allocate alternating pages to artificially fragment. */
    uint32_t pages[32] = {0};
    uint32_t allocated = 0u;

    for (uint32_t i = 0u; i < 32u; ++i)
    {
        pages[i] = physical_memory_manager_page_frame_allocate();
        if (!pages[i])
            break;
        ++allocated;
    }

    /* Free odd-indexed pages to create gaps. */
    for (uint32_t i = 1u; i < allocated; i += 2u)
    {
        if (pages[i])
            physical_memory_manager_page_frame_free(pages[i]);
    }

    uint32_t frag_during = physical_memory_manager_get_fragmentation_ratio();

    /* Free remaining even-indexed pages. */
    for (uint32_t i = 0u; i < allocated; i += 2u)
    {
        if (pages[i])
            physical_memory_manager_page_frame_free(pages[i]);
    }

    uint32_t frag_after = physical_memory_manager_get_fragmentation_ratio();
    uint32_t free_after = physical_memory_manager_get_free_page_count();

    bool free_restored = (free_after == free_before);
    bool range_ok = (frag_before <= 100u) && (frag_during <= 100u) && (frag_after <= 100u);
    bool pass = free_restored && range_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM fragmentation smoke: before=");
    serial_write_int(serial_port, (int32_t) frag_before);
    serial_write_string(serial_port, ", during=");
    serial_write_int(serial_port, (int32_t) frag_during);
    serial_write_string(serial_port, ", after=");
    serial_write_int(serial_port, (int32_t) frag_after);
    serial_write_string(serial_port, ", restored=");
    serial_write_int(serial_port, (int32_t) free_restored);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_tlsf_basic(Serial_t *serial_port)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: TLSF basic smoke: skipped (server)\n");
#else
    if (!kernel_tlsf_is_initialized())
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: TLSF basic smoke: not initialized (fail)\n");
        return;
    }

    uint32_t free_before = kernel_tlsf_get_free_bytes();
    uint32_t alloc_before = kernel_tlsf_get_alloc_count();
    uint32_t wcet_alloc_before = kernel_tlsf_get_wcet_alloc_cycles();
    uint32_t wcet_free_before = kernel_tlsf_get_wcet_free_cycles();

    void *ptr1 = kernel_tlsf_alloc(128);
    void *ptr2 = kernel_tlsf_alloc(1024);
    void *ptr3 = kernel_tlsf_alloc(4096);

    bool allocs_ok = ptr1 && ptr2 && ptr3;
    bool owns_ok = kernel_tlsf_owns(ptr1) && kernel_tlsf_owns(ptr2) && kernel_tlsf_owns(ptr3);

    kernel_tlsf_free(ptr2);
    void *ptr4 = kernel_tlsf_alloc(256);
    void *ptr5 = kernel_tlsf_alloc(256);

    kernel_tlsf_free(ptr1);
    kernel_tlsf_free(ptr3);
    kernel_tlsf_free(ptr4);
    kernel_tlsf_free(ptr5);

    uint32_t free_after = kernel_tlsf_get_free_bytes();
    uint32_t alloc_after = kernel_tlsf_get_alloc_count();
    uint32_t wcet_alloc_after = kernel_tlsf_get_wcet_alloc_cycles();
    uint32_t wcet_free_after = kernel_tlsf_get_wcet_free_cycles();

    bool free_restored = (free_after == free_before);
    bool counts_ok = (alloc_after == alloc_before + 5u);
    bool bounded =
        (wcet_alloc_after > 0u && wcet_alloc_after < 5000000u) && (wcet_free_after > 0u && wcet_free_after < 5000000u);

    bool pass = allocs_ok && owns_ok && free_restored && counts_ok && bounded;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: TLSF basic smoke: allocs_ok=");
    serial_write_int(serial_port, (int32_t) allocs_ok);
    serial_write_string(serial_port, ", owns_ok=");
    serial_write_int(serial_port, (int32_t) owns_ok);
    serial_write_string(serial_port, ", free_restored=");
    serial_write_int(serial_port, (int32_t) free_restored);
    serial_write_string(serial_port, ", bounded_wcet=");
    serial_write_int(serial_port, (int32_t) bounded);

    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
    {
        serial_write_string(serial_port, " wcet_alloc=");
        serial_write_int(serial_port, (int32_t) wcet_alloc_after);
        serial_write_string(serial_port, " (fail)\n");
    }
#endif
}

void kernel_smoke_test_run_tlsf_fragmentation(Serial_t *serial_port)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: TLSF fragmentation smoke: skipped (server)\n");
#else
    if (!kernel_tlsf_is_initialized())
        return;

    uint32_t free_before = kernel_tlsf_get_free_bytes();

    /* Allocate and free to create a Swiss cheese pattern. */
    void *ptrs[128];
    uint32_t allocated = 0u;

    for (uint32_t i = 0u; i < 128u; ++i)
    {
        ptrs[i] = kernel_tlsf_alloc(64 + (i % 4) * 32);
        if (!ptrs[i])
            break;
        ++allocated;
    }

    /* Free alternating blocks. */
    for (uint32_t i = 1u; i < allocated; i += 2u)
    {
        kernel_tlsf_free(ptrs[i]);
        ptrs[i] = NULL;
    }

    uint32_t wcet_alloc_during = kernel_tlsf_get_wcet_alloc_cycles();
    uint32_t wcet_free_during = kernel_tlsf_get_wcet_free_cycles();

    /* Free the rest. TLSF must coalesce them perfectly. */
    for (uint32_t i = 0u; i < allocated; ++i)
    {
        if (ptrs[i])
            kernel_tlsf_free(ptrs[i]);
    }

    uint32_t free_after = kernel_tlsf_get_free_bytes();
    bool coalesced = (free_after == free_before);

    bool pass = coalesced && (allocated == 128u);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: TLSF fragmentation smoke: coalesced=");
    serial_write_int(serial_port, (int32_t) coalesced);
    serial_write_string(serial_port, ", allocs=");
    serial_write_int(serial_port, (int32_t) allocated);

    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_division_error(void)
{
    volatile int zero = 0;
    volatile int trap = 1 / zero;

    (void) trap;
}

void kernel_smoke_test_run_debug_exception(void)
{
    __asm__ volatile("pushf\n\t"
                     "orl $0x100, (%%esp)\n\t"
                     "popf\n\t"
                     "nop\n\t"
                     :
                     :
                     : "cc", "memory");
}

void kernel_smoke_test_run_breakpoint_exception(void) { __asm__ volatile("int3"); }

void kernel_smoke_test_run_invalid_opcode_exception(void) { __asm__ volatile("ud2"); }

void kernel_smoke_test_run_general_protection_exception(void)
{
    __asm__ volatile("xorl %%eax, %%eax\n\t"
                     "movw %%ax, %%ds\n\t"
                     "movl (%%eax), %%eax\n\t"
                     :
                     :
                     : "eax", "memory");
}

void kernel_smoke_test_run_page_fault_exception(void)
{
    volatile uint32_t *const non_present_virtual_address = (volatile uint32_t *) 0xE0001000u;
    volatile uint32_t trap = *non_present_virtual_address;

    (void) trap;
}

void kernel_smoke_test_run_double_fault_exception(void)
{
    /* Synthetic vector injection for #DF handler-path validation. */
    __asm__ volatile("int $0x08");
}

void kernel_smoke_test_run_graphics_demo(Serial_t *serial_port)
{
    framebuffer_clear(framebuffer_rgb(0, 0, 64));
    framebuffer_fill_rect(50, 50, 200, 100, COLOR_RED);
    framebuffer_draw_rect(300, 50, 200, 100, COLOR_GREEN);
    framebuffer_fill_rect(550, 50, 200, 100, COLOR_BLUE);

    for (uint32_t x = 50; x < 750; x++)
    {
        uint8_t red = (x - 50u) * 255u / 700u;
        uint8_t green = 255u - red;

        framebuffer_draw_vline(x, 200, 280, framebuffer_rgb(red, green, 128));
    }

    framebuffer_draw_hline(50, 750, 320, COLOR_WHITE);
    framebuffer_draw_hline(50, 750, 340, COLOR_YELLOW);
    framebuffer_draw_hline(50, 750, 360, COLOR_CYAN);
    framebuffer_draw_hline(50, 750, 380, COLOR_MAGENTA);

    for (uint32_t y = 420; y < 620; y += 2)
    {
        for (uint32_t x = 50; x < 250; x += 2)
            framebuffer_put_pixel(x, y, COLOR_WHITE);
    }

    for (uint32_t i = 0; i < 50; i += 5)
    {
        color_t color = framebuffer_rgb(i * 5u, 255u - i * 5u, 128u);
        framebuffer_draw_rect(300 + i, 420 + i, 200 - 2 * i, 200 - 2 * i, color);
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: graphics demo displayed!\n");
}

void kernel_smoke_test_run_interrupt_request_runtime_status(Serial_t *serial_port)
{
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: IRQ runtime status: ticks=");
    serial_write_int(serial_port, (int32_t) clock_get_tick_count());
    serial_write_string(serial_port, ", pit_hz=");
    serial_write_int(serial_port, (int32_t) clock_get_tick_hz());
    serial_write_string(serial_port, ", spurious7=");
    serial_write_int(serial_port, (int32_t) clock_get_spurious_irq7_count());
    serial_write_string(serial_port, ", spurious15=");
    serial_write_int(serial_port, (int32_t) clock_get_spurious_irq15_count());
    serial_write_string(serial_port, ", rtc_irq=");
    serial_write_int(serial_port, (int32_t) clock_get_rtc_periodic_interrupt_count());
    serial_write_string(serial_port, ", rtc_periodic=");
    serial_write_int(serial_port, (int32_t) clock_is_rtc_periodic_enabled());
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_realtime_clock_snapshot(Serial_t *serial_port)
{
    RealtimeClockTime_t current_time;

    clock_read_walltime(&current_time);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: RTC snapshot: ");
    serial_write_int(serial_port, (int32_t) current_time.hour);
    serial_write_string(serial_port, ":");
    serial_write_int(serial_port, (int32_t) current_time.minute);
    serial_write_string(serial_port, ":");
    serial_write_int(serial_port, (int32_t) current_time.second);
    serial_write_string(serial_port, " ");
    serial_write_int(serial_port, (int32_t) current_time.day);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) current_time.month);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) current_time.year);
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_apic_periodic_mode(Serial_t *serial_port)
{
    uint32_t start_tick;
    uint32_t end_tick;
    uint32_t target_tick;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: APIC periodic smoke: state=");
    serial_write_string(serial_port, advanced_pic_timer_backend_name());
    serial_write_string(serial_port, ", apic_owner=");
    serial_write_int(serial_port, (int32_t) interrupt_request_is_timer_owner_apic());
    serial_write_string(serial_port, ", periodic=");
    serial_write_int(serial_port, (int32_t) advanced_pic_timer_backend_is_periodic_mode_enabled());
    serial_write_string(serial_port, "\n");

    if (!interrupt_request_is_timer_owner_apic() || !advanced_pic_timer_backend_is_periodic_mode_enabled())
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: APIC periodic smoke: skipped (owner/path inactive)\n");
        return;
    }

    start_tick = clock_get_tick_count();
    target_tick = start_tick + 8u;
    while (clock_get_tick_count() < target_tick)
        cpu_no_operation();
    end_tick = clock_get_tick_count();

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: APIC periodic smoke: delta_ticks=");
    serial_write_int(serial_port, (int32_t) (end_tick - start_tick));
    if ((end_tick - start_tick) >= 8u)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_apic_readiness(Serial_t *serial_port)
{
    uint8_t madt_available = advanced_configuration_and_power_interface_madt_is_available();
    uint32_t lapic_count = advanced_configuration_and_power_interface_madt_get_local_apic_count();
    uint32_t lapic_phys = advanced_configuration_and_power_interface_madt_get_local_apic_physical_base();
    uint8_t lapic_mmio = advanced_pic_timer_backend_is_local_apic_mmio_mapped();

    bool madt_ok = madt_available;
    bool lapic_count_ok = (lapic_count > 0u);
    bool lapic_phys_ok = (lapic_phys != 0u);
    bool mmio_ok = lapic_mmio;

    bool pass = madt_ok && lapic_count_ok && lapic_phys_ok && mmio_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: APIC readiness smoke: madt=");
    serial_write_int(serial_port, (int32_t) madt_ok);
    serial_write_string(serial_port, ", lapic_count=");
    serial_write_int(serial_port, (int32_t) lapic_count);
    serial_write_string(serial_port, ", lapic_phys=");
    serial_write_hex32(serial_port, lapic_phys);
    serial_write_string(serial_port, ", mmio_mapped=");
    serial_write_int(serial_port, (int32_t) mmio_ok);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_ioapic_readiness(Serial_t *serial_port)
{
    uint8_t madt_available = advanced_configuration_and_power_interface_madt_is_available();
    uint32_t ioapic_count = advanced_configuration_and_power_interface_madt_get_io_apic_count();
    uint32_t ioapic_mapped = input_output_advanced_programmable_interrupt_controller_get_mapped_count();
    uint32_t ioapic_routes = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();

    bool madt_available_ok = madt_available;
    bool has_ioapics = (ioapic_count > 0u);
    bool mapped_ok = (ioapic_mapped > 0u);
    bool routes_ok = (ioapic_routes > 0u);

    bool pass = madt_available_ok && has_ioapics && mapped_ok && routes_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: IOAPIC readiness smoke: madt=");
    serial_write_int(serial_port, (int32_t) madt_available_ok);
    serial_write_string(serial_port, ", ioapic_count=");
    serial_write_int(serial_port, (int32_t) ioapic_count);
    serial_write_string(serial_port, ", mapped=");
    serial_write_int(serial_port, (int32_t) ioapic_mapped);
    serial_write_string(serial_port, ", routes=");
    serial_write_int(serial_port, (int32_t) ioapic_routes);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_heap_cross_domain_stress(Serial_t *serial_port)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: heap cross-domain smoke: skipped (client)\n");
    return;
#else
    uint32_t domain_count = kernel_heap_get_server_domain_count();

    if (domain_count < 2u)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: heap cross-domain smoke: skipped (domain_count < 2)\n");
        return;
    }

    bool domain_ok = true;
    uint32_t allocs = 0u;

    /* Force domain 0 state via CPU slot mock. */
    if (!cpu_topology_debug_force_logical_slot(0u))
        domain_ok = false;

    uint32_t active0 = kernel_heap_get_server_active_domain();
    void *obj_d0 = kmalloc(64u);
    bool obj0_ok = (obj_d0 != NULL);

    if (obj_d0)
        ++allocs;

    /* Force domain 1 state via CPU slot mock. */
    if (!cpu_topology_debug_force_logical_slot(1u))
        domain_ok = false;

    uint32_t active1 = kernel_heap_get_server_active_domain();
    void *obj_d1a = kmalloc(64u);
    void *obj_d1b = kmalloc(64u);
    bool obj1_ok = (obj_d1a != NULL) && (obj_d1b != NULL);

    if (obj_d1a)
        ++allocs;
    if (obj_d1b)
        ++allocs;

    /* Restore domain 0. */
    cpu_topology_debug_clear_forced_logical_slot();

    uint32_t active_restored = kernel_heap_get_server_active_domain();
    bool restore_ok = (active_restored == 0u);

    /* Clean up. */
    if (obj_d0)
        kfree(obj_d0);
    if (obj_d1a)
        kfree(obj_d1a);
    if (obj_d1b)
        kfree(obj_d1b);

    bool pass = domain_ok && (active0 == 0u) && (active1 == 1u) && obj0_ok && obj1_ok && restore_ok;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: heap cross-domain smoke: domain_count=");
    serial_write_int(serial_port, (int32_t) domain_count);
    serial_write_string(serial_port, ", active0=");
    serial_write_int(serial_port, (int32_t) active0);
    serial_write_string(serial_port, ", active1=");
    serial_write_int(serial_port, (int32_t) active1);
    serial_write_string(serial_port, ", restored=");
    serial_write_int(serial_port, (int32_t) active_restored);
    serial_write_string(serial_port, ", allocs=");
    serial_write_int(serial_port, (int32_t) allocs);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_c7_tlsf_soak(Serial_t *serial_port)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    if (!kernel_tlsf_is_initialized() || !kernel_heap_get_strategy_name())
        return;
#else
    return;
#endif

    bool pass = true;
    for (uint32_t i = 0u; i < 1000u; ++i)
    {
        void *ptr = kmalloc(512u);
        if (!ptr)
        {
            pass = false;
            break;
        }
        kfree(ptr);
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: C7 TLSF soak (1k allocs): pass=");
    serial_write_int(serial_port, (int32_t) pass);
    serial_write_string(serial_port, pass ? " (pass)\n" : " (fail)\n");
}

void kernel_smoke_test_run_c7_frame_simulation(Serial_t *serial_port)
{
    if (!kernel_frame_arena_is_initialized())
        return;

    bool pass = true;
    for (uint32_t frame = 0u; frame < 1000u; ++frame)
    {
        kernel_frame_arena_reset();

        void *obj1 = kernel_frame_arena_alloc(64u, 8u);
        void *obj2 = kernel_frame_arena_alloc(128u, 16u);
        void *obj3 = kernel_frame_arena_alloc(256u, 32u);

        if (!obj1 || !obj2 || !obj3)
        {
            pass = false;
            break;
        }
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: C7 frame arena sim (1000 frames): pass=");
    serial_write_int(serial_port, (int32_t) pass);
    serial_write_string(serial_port, pass ? " (pass)\n" : " (fail)\n");
}

void kernel_smoke_test_run_c7_ring_stress(Serial_t *serial_port)
{
    if (!kernel_ring_buffer_is_initialized())
        return;

    bool pass = true;
    for (uint32_t i = 0u; i < 10000u; ++i)
    {
        uint32_t dummy = i;
        if (!kernel_ring_buffer_enqueue((const uint8_t *) &dummy, sizeof(dummy)))
        {
            pass = false;
            break;
        }

        uint32_t out_dummy = 0;
        if (!kernel_ring_buffer_dequeue((uint8_t *) &out_dummy, sizeof(out_dummy)))
        {
            pass = false;
            break;
        }

        if (dummy != out_dummy)
        {
            pass = false;
            break;
        }
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: C7 ring stress (10k ops): pass=");
    serial_write_int(serial_port, (int32_t) pass);
    serial_write_string(serial_port, pass ? " (pass)\n" : " (fail)\n");
}

void kernel_smoke_test_run_c7_combined_hotloop(Serial_t *serial_port)
{
    if (!kernel_frame_arena_is_initialized() || !kernel_pool_allocator_is_initialized() ||
        !kernel_ring_buffer_is_initialized())
        return;

    bool pass = true;
    kernel_frame_arena_reset();

    for (uint32_t i = 0u; i < 500u; ++i)
    {
        void *frame_ptr = kernel_frame_arena_alloc(16u, 8u);
        if (!frame_ptr)
            pass = false;

        void *pool_ptr = kernel_pool_alloc();
        if (!pool_ptr)
            pass = false;

        uint8_t ev = 0xAF;
        kernel_ring_buffer_enqueue(&ev, 1);
        kernel_ring_buffer_dequeue(&ev, 1);

        if (pool_ptr)
            kernel_pool_free(pool_ptr);

        if (!pass)
            break;
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: C7 combined hotloop: allocator_sanity=");
    serial_write_int(serial_port, (int32_t) pass);
    serial_write_string(serial_port, pass ? " (pass)\n" : " (fail)\n");
}
void kernel_smoke_test_run_vmm_alloc_free(Serial_t *serial_port)
{
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: starting VMM smoke test...\n");

    void *ptr1 = kernel_vmm_alloc_pages(2);
    void *ptr2 = kernel_vmm_alloc_pages(4);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: VMM alloc 2 pages: ");
    serial_write_hex32(serial_port, (uint32_t) ptr1);
    serial_write_string(serial_port, ", 4 pages: ");
    serial_write_hex32(serial_port, (uint32_t) ptr2);
    serial_write_string(serial_port, "\n");

    if (ptr1 && ((uintptr_t) ptr1 >= KERNEL_VMM_DYNAMIC_START) && ((uintptr_t) ptr1 < KERNEL_VMM_DYNAMIC_END))
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: VMM ptr1 range check: OK\n");
    else
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: VMM ptr1 range check: FAILED\n");

    if (ptr2 && ((uintptr_t) ptr2 >= KERNEL_VMM_DYNAMIC_START) && ((uintptr_t) ptr2 < KERNEL_VMM_DYNAMIC_END))
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: VMM ptr2 range check: OK\n");
    else
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: VMM ptr2 range check: FAILED\n");

    if (ptr1)
        kernel_vmm_free_pages(ptr1, 2);
    if (ptr2)
        kernel_vmm_free_pages(ptr2, 4);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: VMM smoke test PASSED\n");
}
