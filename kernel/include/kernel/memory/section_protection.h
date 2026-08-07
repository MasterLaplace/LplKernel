/**
 * @file section_protection.h
 * @brief The one hardware barrier a kernel with no ring 3 can still afford.
 *
 * Everything here runs in ring 0 — the kernel, the engine, the mind, the memory —
 * so there is no privilege boundary left to contain a stray write. What remains is
 * the paging unit: a page table entry with R/W clear faults a supervisor store,
 * provided CR0.WP is set. boot.S sets WP together with PG, and this module clears
 * R/W across the range the linker marks as read-only: code, constants and the
 * constructor tables walked once at boot.
 *
 * It is not hypothetical protection. A C++ static once landed past `_kernel_end`
 * because the linker script did not collect its COMDAT section, the physical memory
 * manager handed the frame out as free RAM, a page table was installed there, and
 * the engine overwrote it while writing its own statics. Four MiB of the direct map
 * disappeared and the fault surfaced much later, inside kmalloc. With the code and
 * constant range read-only, that class of write faults where it happens, with the
 * faulting instruction pointer in the panic — instead of somewhere else entirely.
 *
 * What it does NOT cover: .data and .bss stay writable, because they are meant to be
 * written. This is a barrier around what is constant, not around what is mutable.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_MEMORY_SECTION_PROTECTION_H
#define KERNEL_MEMORY_SECTION_PROTECTION_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/cpu/isr.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CR0 bit 16. With it clear, a read-only page table entry constrains ring 3 only. */
#define KERNEL_SECTION_PROTECTION_CONTROL_REGISTER_0_WRITE_PROTECT_BIT 0x00010000u

/**
 * @brief Mark the kernel's code and constant pages read-only.
 *
 * Walks `_kernel_read_only_start` to `_kernel_read_only_end` — both page-aligned by
 * the linker script — and clears the R/W bit of every page table entry in between.
 * Call it once every global constructor has run, since the constructor tables are
 * inside the range.
 *
 * @return true when the whole range was protected, false if any page was unmapped
 *         or the range is empty.
 */
bool kernel_section_protection_apply(void);

/**
 * @brief Report whether the protection pass ran and covered its whole range.
 *
 * @return true when kernel_section_protection_apply() succeeded.
 */
bool kernel_section_protection_is_active(void);

/**
 * @brief Report whether the processor enforces read-only pages against ring 0.
 *
 * Read from CR0 rather than remembered from boot: a protection that is believed to
 * be on and is not is worse than one known to be off.
 *
 * @return true when CR0.WP is set.
 */
bool kernel_section_protection_write_protect_is_enabled(void);

/**
 * @brief Number of pages the protection pass turned read-only.
 *
 * @return The page count, zero before the pass runs.
 */
uint32_t kernel_section_protection_get_read_only_page_count(void);

/**
 * @brief First address of the protected range.
 *
 * @return The linker's `_kernel_read_only_start`.
 */
uint32_t kernel_section_protection_get_range_start(void);

/**
 * @brief One past the last address of the protected range.
 *
 * @return The linker's `_kernel_read_only_end`.
 */
uint32_t kernel_section_protection_get_range_end(void);

/**
 * @brief Number of write faults the probe caught and recovered from.
 *
 * @return The count, which only the probe below can raise.
 */
uint32_t kernel_section_protection_get_recovered_fault_count(void);

/**
 * @brief Attempt one byte-sized write and report whether it faulted.
 *
 * The only way to prove the protection does something. Checking the page table bit
 * proves what was asked for, not what the processor enforces — with CR0.WP clear the
 * bit reads exactly the same and every store still succeeds.
 *
 * The store is bracketed by a resume address published to the page fault handler, so
 * a fault steps over the instruction instead of returning to it. Nothing is written
 * when the page is protected; when it is not, the target byte is overwritten with the
 * value it already held, so a probe of a writable page leaves memory unchanged too.
 *
 * @param target Address to store to.
 * @return true if the store faulted and was recovered, false if it went through.
 */
bool kernel_section_protection_probe_write(volatile uint8_t *target);

/**
 * @brief Page fault hook: recover a probe's expected write fault.
 *
 * Called first thing by the #PF handler. Every fault that is not an armed probe's
 * write to a protected page is declined, so the panic path is unchanged for
 * everything else.
 *
 * @param frame The faulting interrupt frame.
 * @param fault_address The CR2 value.
 * @return true when the fault was the probe's and execution was redirected.
 */
bool kernel_section_protection_handle_page_fault(const InterruptFrame_t *frame, uint32_t fault_address);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_MEMORY_SECTION_PROTECTION_H */
