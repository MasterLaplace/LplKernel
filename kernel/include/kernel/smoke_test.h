/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** smoke_test
*/

#ifndef SMOKE_TEST_H_
#define SMOKE_TEST_H_

#include <kernel/drivers/serial.h>

#define KERNEL_SMOKE_TEST_ENABLE_PMM_ALLOCATE_FREE  1u
#define KERNEL_SMOKE_TEST_ENABLE_DIVISION_ERROR     0u
#define KERNEL_SMOKE_TEST_ENABLE_DEBUG_EXCEPTION    0u
#define KERNEL_SMOKE_TEST_ENABLE_BREAKPOINT         0u
#define KERNEL_SMOKE_TEST_ENABLE_INVALID_OPCODE     0u
#define KERNEL_SMOKE_TEST_ENABLE_GENERAL_PROTECTION 0u
#define KERNEL_SMOKE_TEST_ENABLE_PAGE_FAULT         0u
#define KERNEL_SMOKE_TEST_ENABLE_DOUBLE_FAULT       0u
#define KERNEL_SMOKE_TEST_ENABLE_GRAPHICS_DEMO      1u
#define KERNEL_SMOKE_TEST_ENABLE_IRQ_RUNTIME_STATUS 1u
#define KERNEL_SMOKE_TEST_ENABLE_RTC_SNAPSHOT       1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_COALESCE 1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_STRESS   1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_ORDER    1u
#define KERNEL_SMOKE_TEST_ENABLE_PAGING_PT_RECLAIM  0u
#define KERNEL_SMOKE_TEST_ENABLE_HEAP_ALLOCATE_FREE 1u
#define KERNEL_SMOKE_TEST_ENABLE_RING3_MINIMAL 0u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_COMPACTION 1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_MADT_SYNC 1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_RUNTIME_SLOT 1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_ONLINE_BOOKKEEPING 1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_SLOT_DOMAIN 1u
#define KERNEL_SMOKE_TEST_ENABLE_SLAB_ALLOC_FREE    1u
#define KERNEL_SMOKE_TEST_ENABLE_CLIENT_HOT_LOOP_RULE 1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BASIC  1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BUDGET  1u
#define KERNEL_SMOKE_TEST_ENABLE_POOL_ALLOCATOR_BASIC 1u
#define KERNEL_SMOKE_TEST_ENABLE_STACK_ALLOCATOR_BASIC 1u
#define KERNEL_SMOKE_TEST_ENABLE_ALLOCATOR_WCET_BOUND 1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_BUDGET_DETERMINISM 1u
#define KERNEL_SMOKE_TEST_ENABLE_POOL_DOUBLE_FREE      1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_POISON_CHECK    1u
#define KERNEL_SMOKE_TEST_ENABLE_APIC_READINESS      1u
#define KERNEL_SMOKE_TEST_ENABLE_IOAPIC_READINESS    1u
#ifndef KERNEL_SMOKE_TEST_ENABLE_HEAP_CROSS_DOMAIN_STRESS
#    define KERNEL_SMOKE_TEST_ENABLE_HEAP_CROSS_DOMAIN_STRESS 1u
#endif
#define KERNEL_SMOKE_TEST_ENABLE_RING_BUFFER_BASIC 1u
#define KERNEL_SMOKE_TEST_ENABLE_TLSF_BASIC        1u
#define KERNEL_SMOKE_TEST_ENABLE_TLSF_FRAGMENTATION 1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_WATERMARK     1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_FRAGMENTATION 1u
#ifndef KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE
#    define KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE 0u
#endif
#define KERNEL_SMOKE_TEST_ENABLE_VMM_ALLOC_FREE      1u

/* C7: Final Validation Campaign */
#define KERNEL_SMOKE_TEST_ENABLE_C7_TLSF_SOAK        1u
#define KERNEL_SMOKE_TEST_ENABLE_C7_FRAME_SIMULATION 1u
#define KERNEL_SMOKE_TEST_ENABLE_C7_RING_STRESS      1u
#define KERNEL_SMOKE_TEST_ENABLE_C7_COMBINED_HOTLOOP 1u

extern void kernel_smoke_test_run_physical_memory_manager_allocate_free(Serial_t *serial_port);

extern void kernel_smoke_test_run_physical_memory_manager_buddy_coalesce(Serial_t *serial_port);

extern void kernel_smoke_test_run_physical_memory_manager_buddy_stress(Serial_t *serial_port);

extern void kernel_smoke_test_run_physical_memory_manager_buddy_order(Serial_t *serial_port);

extern void kernel_smoke_test_run_paging_runtime_page_table_reclaim(Serial_t *serial_port);

extern void kernel_smoke_test_run_heap_allocate_free(Serial_t *serial_port);
extern void kernel_smoke_test_run_heap_poison_canary(Serial_t *serial_port);
extern void kernel_smoke_test_run_pmm_uaf_detection(Serial_t *serial_port);
extern void kernel_smoke_test_run_ring3_minimal(Serial_t *serial_port);

extern void kernel_smoke_test_run_cpu_topology_compaction(Serial_t *serial_port);

extern void kernel_smoke_test_run_cpu_topology_madt_sync(Serial_t *serial_port);

extern void kernel_smoke_test_run_cpu_topology_runtime_slot(Serial_t *serial_port);

extern void kernel_smoke_test_run_cpu_topology_online_bookkeeping(Serial_t *serial_port);

extern void kernel_smoke_test_run_cpu_topology_slot_domain(Serial_t *serial_port);

extern void kernel_smoke_test_run_slab_alloc_free(Serial_t *serial_port);

extern void kernel_smoke_test_run_client_hot_loop_rule(Serial_t *serial_port);

extern void kernel_smoke_test_run_frame_arena_basic(Serial_t *serial_port);

extern void kernel_smoke_test_run_frame_arena_budget(Serial_t *serial_port);

extern void kernel_smoke_test_run_pool_allocator_basic(Serial_t *serial_port);

extern void kernel_smoke_test_run_stack_allocator_basic(Serial_t *serial_port);

extern void kernel_smoke_test_run_allocator_wcet_bound_check(Serial_t *serial_port);

extern void kernel_smoke_test_run_frame_budget_determinism(Serial_t *serial_port);

extern void kernel_smoke_test_run_pool_double_free(Serial_t *serial_port);

extern void kernel_smoke_test_run_frame_poison_check(Serial_t *serial_port);

extern void kernel_smoke_test_run_ring_buffer_basic(Serial_t *serial_port);

extern void kernel_smoke_test_run_apic_readiness(Serial_t *serial_port);

extern void kernel_smoke_test_run_ioapic_readiness(Serial_t *serial_port);

extern void kernel_smoke_test_run_heap_cross_domain_stress(Serial_t *serial_port);

extern void kernel_smoke_test_run_division_error(void);

extern void kernel_smoke_test_run_debug_exception(void);

extern void kernel_smoke_test_run_breakpoint_exception(void);

extern void kernel_smoke_test_run_invalid_opcode_exception(void);

extern void kernel_smoke_test_run_general_protection_exception(void);

extern void kernel_smoke_test_run_page_fault_exception(void);

extern void kernel_smoke_test_run_double_fault_exception(void);

extern void kernel_smoke_test_run_graphics_demo(Serial_t *serial_port);

extern void kernel_smoke_test_run_interrupt_request_runtime_status(Serial_t *serial_port);

extern void kernel_smoke_test_run_realtime_clock_snapshot(Serial_t *serial_port);

extern void kernel_smoke_test_run_apic_periodic_mode(Serial_t *serial_port);

extern void kernel_smoke_test_run_pmm_watermark(Serial_t *serial_port);

extern void kernel_smoke_test_run_pmm_fragmentation(Serial_t *serial_port);

extern void kernel_smoke_test_run_tlsf_basic(Serial_t *serial_port);

extern void kernel_smoke_test_run_tlsf_fragmentation(Serial_t *serial_port);

extern void kernel_smoke_test_run_c7_tlsf_soak(Serial_t *serial_port);
extern void kernel_smoke_test_run_c7_frame_simulation(Serial_t *serial_port);
extern void kernel_smoke_test_run_c7_ring_stress(Serial_t *serial_port);
extern void kernel_smoke_test_run_c7_combined_hotloop(Serial_t *serial_port);
extern void kernel_smoke_test_run_vmm_alloc_free(Serial_t *serial_port);

#endif /* !SMOKE_TEST_H_ */
