#include <kernel/core/kernel_smoke_batch.h>
#include <kernel/smoke_test.h>
#include <kernel/config.h>

void kernel_smoke_batch_run_initialization_tests(Serial_t *com1)
{
    if (KERNEL_SMOKE_TEST_ENABLE_PAGING_PT_RECLAIM)
        kernel_smoke_test_run_paging_runtime_page_table_reclaim(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_HEAP_ALLOCATE_FREE)
    {
        kernel_smoke_test_run_heap_allocate_free(com1);
        kernel_smoke_test_run_heap_poison_canary(com1);
    }

    kernel_smoke_test_run_pmm_uaf_detection(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_RING3_MINIMAL)
        kernel_smoke_test_run_ring3_minimal(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_COMPACTION)
        kernel_smoke_test_run_cpu_topology_compaction(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_MADT_SYNC)
        kernel_smoke_test_run_cpu_topology_madt_sync(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_RUNTIME_SLOT)
        kernel_smoke_test_run_cpu_topology_runtime_slot(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_ONLINE_BOOKKEEPING)
        kernel_smoke_test_run_cpu_topology_online_bookkeeping(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_SLOT_DOMAIN)
        kernel_smoke_test_run_cpu_topology_slot_domain(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_SLAB_ALLOC_FREE)
        kernel_smoke_test_run_slab_alloc_free(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CLIENT_HOT_LOOP_RULE)
        kernel_smoke_test_run_client_hot_loop_rule(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BASIC)
        kernel_smoke_test_run_frame_arena_basic(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BUDGET)
        kernel_smoke_test_run_frame_arena_budget(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_POOL_ALLOCATOR_BASIC)
        kernel_smoke_test_run_pool_allocator_basic(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_POISON_CHECK)
        kernel_smoke_test_run_frame_poison_check(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_POOL_DOUBLE_FREE)
        kernel_smoke_test_run_pool_double_free(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_TLSF_SOAK)
        kernel_smoke_test_run_c7_tlsf_soak(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_FRAME_SIMULATION)
        kernel_smoke_test_run_c7_frame_simulation(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_RING_STRESS)
        kernel_smoke_test_run_c7_ring_stress(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_COMBINED_HOTLOOP)
        kernel_smoke_test_run_c7_combined_hotloop(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_STACK_ALLOCATOR_BASIC)
        kernel_smoke_test_run_stack_allocator_basic(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_ALLOCATOR_WCET_BOUND)
        kernel_smoke_test_run_allocator_wcet_bound_check(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_BUDGET_DETERMINISM)
        kernel_smoke_test_run_frame_budget_determinism(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_RING_BUFFER_BASIC)
        kernel_smoke_test_run_ring_buffer_basic(com1);

#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    if (KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_COALESCE)
        kernel_smoke_test_run_physical_memory_manager_buddy_coalesce(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_STRESS)
        kernel_smoke_test_run_physical_memory_manager_buddy_stress(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_ORDER)
        kernel_smoke_test_run_physical_memory_manager_buddy_order(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_HEAP_CROSS_DOMAIN_STRESS)
        kernel_smoke_test_run_heap_cross_domain_stress(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_VMM_ALLOC_FREE)
        kernel_smoke_test_run_vmm_alloc_free(com1);
#endif

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_WATERMARK)
        kernel_smoke_test_run_pmm_watermark(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_FRAGMENTATION)
        kernel_smoke_test_run_pmm_fragmentation(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_TLSF_BASIC)
        kernel_smoke_test_run_tlsf_basic(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_TLSF_FRAGMENTATION)
        kernel_smoke_test_run_tlsf_fragmentation(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_IRQ_RUNTIME_STATUS)
        kernel_smoke_test_run_interrupt_request_runtime_status(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_RTC_SNAPSHOT)
        kernel_smoke_test_run_realtime_clock_snapshot(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE)
        kernel_smoke_test_run_apic_periodic_mode(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_APIC_READINESS)
        kernel_smoke_test_run_apic_readiness(com1);

    if (KERNEL_SMOKE_TEST_ENABLE_IOAPIC_READINESS)
        kernel_smoke_test_run_ioapic_readiness(com1);
}

void kernel_smoke_batch_run_post_boot_tests(Serial_t *com1)
{
    if (KERNEL_SMOKE_TEST_ENABLE_DIVISION_ERROR)
        kernel_smoke_test_run_division_error();
    if (KERNEL_SMOKE_TEST_ENABLE_DEBUG_EXCEPTION)
        kernel_smoke_test_run_debug_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_BREAKPOINT)
        kernel_smoke_test_run_breakpoint_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_INVALID_OPCODE)
        kernel_smoke_test_run_invalid_opcode_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_GENERAL_PROTECTION)
        kernel_smoke_test_run_general_protection_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_PAGE_FAULT)
        kernel_smoke_test_run_page_fault_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_DOUBLE_FAULT)
        kernel_smoke_test_run_double_fault_exception();

    if (KERNEL_SMOKE_TEST_ENABLE_GRAPHICS_DEMO)
        kernel_smoke_test_run_graphics_demo(com1);
}
