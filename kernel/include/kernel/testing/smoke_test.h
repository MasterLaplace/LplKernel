/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file smoke_test.h
 * @brief Kernel smoke tests, one function per checked behaviour.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-07-21
 **************************************************************************/

#ifndef KERNEL_TESTING_SMOKE_TEST_H_
#define KERNEL_TESTING_SMOKE_TEST_H_

#include <kernel/drivers/serial.h>

#define KERNEL_SMOKE_TEST_ENABLE_PMM_ALLOCATE_FREE               1u
#define KERNEL_SMOKE_TEST_ENABLE_DIVISION_ERROR                  0u
#define KERNEL_SMOKE_TEST_ENABLE_DEBUG_EXCEPTION                 0u
#define KERNEL_SMOKE_TEST_ENABLE_BREAKPOINT                      0u
#define KERNEL_SMOKE_TEST_ENABLE_INVALID_OPCODE                  0u
#define KERNEL_SMOKE_TEST_ENABLE_GENERAL_PROTECTION              0u
#define KERNEL_SMOKE_TEST_ENABLE_PAGE_FAULT                      0u
#define KERNEL_SMOKE_TEST_ENABLE_DOUBLE_FAULT                    0u
#define KERNEL_SMOKE_TEST_ENABLE_GRAPHICS_DEMO                   1u
#define KERNEL_SMOKE_TEST_ENABLE_IRQ_RUNTIME_STATUS              1u
#define KERNEL_SMOKE_TEST_ENABLE_RTC_SNAPSHOT                    1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_COALESCE              1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_STRESS                1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_ORDER                 1u
#define KERNEL_SMOKE_TEST_ENABLE_PAGING_PT_RECLAIM               0u
#define KERNEL_SMOKE_TEST_ENABLE_HEAP_ALLOCATE_FREE              1u
#define KERNEL_SMOKE_TEST_ENABLE_RING3_MINIMAL                   0u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_COMPACTION         1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_MADT_SYNC          1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_RUNTIME_SLOT       1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_ONLINE_BOOKKEEPING 1u
#define KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_SLOT_DOMAIN        1u
#define KERNEL_SMOKE_TEST_ENABLE_SLAB_ALLOC_FREE                 1u
#define KERNEL_SMOKE_TEST_ENABLE_CLIENT_HOT_LOOP_RULE            1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BASIC               1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BUDGET              1u
#define KERNEL_SMOKE_TEST_ENABLE_POOL_ALLOCATOR_BASIC            1u
#define KERNEL_SMOKE_TEST_ENABLE_STACK_ALLOCATOR_BASIC           1u
#define KERNEL_SMOKE_TEST_ENABLE_ALLOCATOR_WCET_BOUND            1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_BUDGET_DETERMINISM        1u
#define KERNEL_SMOKE_TEST_ENABLE_POOL_DOUBLE_FREE                1u
#define KERNEL_SMOKE_TEST_ENABLE_FRAME_POISON_CHECK              1u
#define KERNEL_SMOKE_TEST_ENABLE_APIC_READINESS                  1u
#define KERNEL_SMOKE_TEST_ENABLE_IOAPIC_READINESS                1u
#ifndef KERNEL_SMOKE_TEST_ENABLE_HEAP_CROSS_DOMAIN_STRESS
#    define KERNEL_SMOKE_TEST_ENABLE_HEAP_CROSS_DOMAIN_STRESS 1u
#endif
#define KERNEL_SMOKE_TEST_ENABLE_RING_BUFFER_BASIC  1u
#define KERNEL_SMOKE_TEST_ENABLE_SECTION_PROTECTION 1u
#define KERNEL_SMOKE_TEST_ENABLE_RECONCILER         1u
#define KERNEL_SMOKE_TEST_ENABLE_WAKEUP_ACCOUNTING  1u
#define KERNEL_SMOKE_TEST_ENABLE_SLEEP_DEPTH        1u
#define KERNEL_SMOKE_TEST_ENABLE_SLEEP_UNTIL_WRITE  1u
#define KERNEL_SMOKE_TEST_ENABLE_FREQUENCY_FEEDBACK 1u
#define KERNEL_SMOKE_TEST_ENABLE_TLSF_BASIC         1u
#define KERNEL_SMOKE_TEST_ENABLE_TLSF_FRAGMENTATION 1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_WATERMARK      1u
#define KERNEL_SMOKE_TEST_ENABLE_PMM_FRAGMENTATION  1u
#ifndef KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE
#    define KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE 0u
#endif
#define KERNEL_SMOKE_TEST_ENABLE_VMM_ALLOC_FREE 1u

/**
 * @name C7: Final Validation Campaign
 * @{
 */
#define KERNEL_SMOKE_TEST_ENABLE_C7_TLSF_SOAK        1u
#define KERNEL_SMOKE_TEST_ENABLE_C7_FRAME_SIMULATION 1u
#define KERNEL_SMOKE_TEST_ENABLE_C7_RING_STRESS      1u
#define KERNEL_SMOKE_TEST_ENABLE_C7_COMBINED_HOTLOOP 1u
/** @} */

extern void smoke_test_run_physical_memory_manager_allocate_free(Serial_t *serial_port);

extern void smoke_test_run_physical_memory_manager_buddy_coalesce(Serial_t *serial_port);

extern void smoke_test_run_physical_memory_manager_buddy_stress(Serial_t *serial_port);

extern void smoke_test_run_physical_memory_manager_buddy_order(Serial_t *serial_port);

extern void smoke_test_run_paging_runtime_page_table_reclaim(Serial_t *serial_port);

extern void smoke_test_run_heap_allocate_free(Serial_t *serial_port);
extern void smoke_test_run_heap_poison_canary(Serial_t *serial_port);
extern void smoke_test_run_pmm_uaf_detection(Serial_t *serial_port);
extern void smoke_test_run_ring3_minimal(Serial_t *serial_port);

extern void smoke_test_run_cpu_topology_compaction(Serial_t *serial_port);

extern void smoke_test_run_cpu_topology_madt_sync(Serial_t *serial_port);

extern void smoke_test_run_cpu_topology_runtime_slot(Serial_t *serial_port);

/**
 * @brief Marks the local CPU and a synthetic neighbour online and checks the count follows.
 *
 * @note Restores the global state before returning: the synthetic CPU would otherwise stay
 *       online, inflate the online count, and make every later TLB shootdown spin forever
 *       waiting for an acknowledgement from a CPU that does not exist.
 *
 * @param serial_port Serial port the result is written to.
 */
extern void smoke_test_run_cpu_topology_online_bookkeeping(Serial_t *serial_port);

extern void smoke_test_run_cpu_topology_slot_domain(Serial_t *serial_port);

extern void smoke_test_run_slab_alloc_free(Serial_t *serial_port);

/**
 * @brief Checks the heap's hot-loop rule: bounded paths are served, unbounded ones refused.
 *
 * @details Inside the hot loop a 64-byte request is a slab class and must be served, while a
 *          64 MiB one would grow the heap and must be refused and counted as a violation.
 *          Real-time builds only; the server reports the smoke as skipped.
 *
 * @param serial_port Serial port the result is written to.
 */
extern void smoke_test_run_client_hot_loop_rule(Serial_t *serial_port);

extern void smoke_test_run_frame_arena_basic(Serial_t *serial_port);

extern void smoke_test_run_frame_arena_budget(Serial_t *serial_port);

extern void smoke_test_run_pool_allocator_basic(Serial_t *serial_port);

extern void smoke_test_run_stack_allocator_basic(Serial_t *serial_port);

extern void smoke_test_run_allocator_wcet_bound_check(Serial_t *serial_port);

extern void smoke_test_run_frame_budget_determinism(Serial_t *serial_port);

extern void smoke_test_run_pool_double_free(Serial_t *serial_port);

extern void smoke_test_run_frame_poison_check(Serial_t *serial_port);

extern void smoke_test_run_ring_buffer_basic(Serial_t *serial_port);

extern void smoke_test_run_apic_readiness(Serial_t *serial_port);

extern void smoke_test_run_ioapic_readiness(Serial_t *serial_port);

extern void smoke_test_run_heap_cross_domain_stress(Serial_t *serial_port);

extern void smoke_test_run_division_error(void);

extern void smoke_test_run_debug_exception(void);

extern void smoke_test_run_breakpoint_exception(void);

extern void smoke_test_run_invalid_opcode_exception(void);

extern void smoke_test_run_general_protection_exception(void);

extern void smoke_test_run_page_fault_exception(void);

extern void smoke_test_run_double_fault_exception(void);

extern void smoke_test_run_graphics_demo(Serial_t *serial_port);

extern void smoke_test_run_interrupt_request_runtime_status(Serial_t *serial_port);

extern void smoke_test_run_realtime_clock_snapshot(Serial_t *serial_port);

extern void smoke_test_run_apic_periodic_mode(Serial_t *serial_port);

extern void smoke_test_run_pmm_watermark(Serial_t *serial_port);

extern void smoke_test_run_pmm_fragmentation(Serial_t *serial_port);

extern void smoke_test_run_tlsf_basic(Serial_t *serial_port);

extern void smoke_test_run_tlsf_fragmentation(Serial_t *serial_port);

extern void smoke_test_run_c7_tlsf_soak(Serial_t *serial_port);
extern void smoke_test_run_c7_frame_simulation(Serial_t *serial_port);
extern void smoke_test_run_c7_ring_stress(Serial_t *serial_port);
extern void smoke_test_run_c7_combined_hotloop(Serial_t *serial_port);
extern void smoke_test_run_vmm_alloc_free(Serial_t *serial_port);

/**
 * @brief Writes into .rodata, into .text and into .data, and checks only the first two fault.
 *
 * @details The probe casts a constant's address to a writable pointer on purpose: that is
 *          exactly the abuse the protection exists to stop, and the processor is expected to
 *          refuse it. The write into .data is the control that must NOT fault.
 *
 * @note One record carries both the structured fields and the battery's own pass marker:
 *       `result` holds the literal "(pass)"/"(fail)" the whole-log scan looks for, so no
 *       second prose line says the same thing twice.
 *
 * @param serial_port Serial port the record is written to.
 */
extern void smoke_test_run_section_protection(Serial_t *serial_port);

/**
 * @brief Drives reconciler passes by hand, then proves a pass can notice drift at all.
 *
 * @details `detects_drift` declares a contract the kernel cannot meet; `restored` puts the
 *          real one back, which is the contract the periodic check runs against afterwards.
 *
 * @param serial_port Serial port the record is written to.
 */
extern void smoke_test_run_reconciler(Serial_t *serial_port);

/**
 * @brief Drives the wake-up accounting through every outcome and checks the law.
 *
 * @details One field per criterion:
 *          - `idle_costs_nothing` is THE criterion: an interrupt with nothing armed is not a
 *            wake-up. Without it the module counts interrupts, which looks exactly like a
 *            working wake-up profile and means nothing;
 *          - `credited_to_one_vector` checks that NO other vector moves: checking only the
 *            credited one would pass against an implementation that bumps the whole table;
 *          - `credited_once`: the interrupts that FOLLOW a wake-up did not cause it;
 *          - `unnamed_is_kept`: an unattributed wake-up is counted apart rather than absorbed,
 *            so a shortfall in the law comes explained;
 *          - `write_is_not_an_interrupt`: a monitor write has its own bucket — conflating the
 *            two would report a device write as an unnamed wake-up on every processor that has
 *            MWAIT, which is all but the emulator;
 *          - `double_arm_refused`: a second arm would double the denominator against one
 *            outcome;
 *          - `busiest_is_right`: two extra wake-ups make the busiest vector a majority rather
 *            than a tie;
 *          - `conserves`: six entered, four named, one write, one unnamed. The denominator is
 *            required alongside the law, because 0 == 0 holds for a module that does nothing.
 *
 * @note The vectors credited, 0xFE and 0xFD, are ones no line delivers: crediting a real one
 *       would make this battery indistinguishable from the boot's own wake-ups.
 * @note Interrupts are off throughout, because the 1 kHz tick would land between an arm and
 *       its attribution and turn every assertion into a coin toss. The live counters are reset
 *       on the way out, so the power floor's record describes the boot.
 *
 * @param serial_port Serial port the record is written to.
 */
extern void smoke_test_run_wakeup_accounting(Serial_t *serial_port);

/**
 * @brief Checks how MWAIT hints are read out of CPUID and how a requested depth is clamped.
 *
 * @details The offset decides the whole slice: CPUID.05H:EDX reports C1* in bits 7:4 while
 *          MWAIT's hint 0 targets C1, so hint h is enumerated when nibble h+1 is set — a hint
 *          derived straight from the nibble index would aim one state too deep.
 *          - `c0_ignored`: C0's nibble is not a sleep state, and counting it would shift every
 *            hint;
 *          - `gap_is_kept`: real processors leave GAPS, C1 and C6 with nothing between. A count
 *            would claim the missing four exist; a mask says exactly which are there;
 *          - `clamped_or_granted`: a depth nothing enumerates is lowered to the deepest below it
 *            and counted, never passed through;
 *          - `floor_is_granted`: C1 is always askable, since it is what MWAIT does with a zero
 *            hint, enumerated or not.
 *
 * @param serial_port Serial port the record is written to.
 */
extern void smoke_test_run_processor_sleep_depth(Serial_t *serial_port);

/**
 * @brief Checks both outcomes of sleeping on a watched word.
 *
 * @details `skips_a_wait_that_is_over`: a word that already differs from what the caller
 *          expected means the device wrote while the caller was looking elsewhere, so there is
 *          nothing to wait for. `sleeps_when_nothing_moved`: a word that has not moved must
 *          actually sleep. `sleep_was_attributed` is where the two slices meet — whatever ended
 *          that sleep was named by the wake-up accounting.
 *
 * @note The real sleep is gated on the tick running: without an interrupt to end it the sleep
 *       halts the kernel, and a battery that can hang is worse than one that skips a case.
 *
 * @param serial_port Serial port the record is written to.
 */
extern void smoke_test_run_sleep_until_write(Serial_t *serial_port);

/**
 * @brief Checks the APERF/MPERF arithmetic on deltas no emulator produces.
 *
 * @details - `above_nominal`: turbo delivers MORE than nominal, which is why the sentinel is
 *            not 1000;
 *          - `no_reference_is_no_answer`: a reference that never moved is an absence, not a
 *            clock of zero;
 *          - `no_overflow`: months of cycles at a few gigahertz still fit, so the multiply must
 *            not wrap;
 *          - `absence_is_reported`: where the pair is absent the live reading says so rather
 *            than invent one.
 *
 * @param serial_port Serial port the record is written to.
 */
extern void smoke_test_run_frequency_feedback(Serial_t *serial_port);

#endif /* !KERNEL_TESTING_SMOKE_TEST_H_ */
