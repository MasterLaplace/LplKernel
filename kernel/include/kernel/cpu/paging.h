/*
** EPITECH PROJECT, 2025
** LplKernel [WSL : Ubuntu]
** File description:
** paging - Runtime paging management API for higher-half kernel
*/

#ifndef PAGING_H_
#define PAGING_H_

#include <stdbool.h>
#include <stdint.h>

/// Page Directory Entry (32-bit format) - Based on OSDev wiki Paging article
/// Structure for PDE when PS=0 (points to a 4KB page table)
typedef struct __attribute__((packed)) {
    uint8_t present          : 1;  // P: Page is present in memory
    uint8_t read_write       : 1;  // R/W: 0 = read-only, 1 = read/write
    uint8_t user_supervisor  : 1;  // U/S: 0 = supervisor only, 1 = user accessible
    uint8_t write_through    : 1;  // PWT: Write-through caching
    uint8_t cache_disable    : 1;  // PCD: Cache disabled
    uint8_t accessed         : 1;  // A: Set by CPU when accessed
    uint8_t reserved_zero    : 1;  // Reserved (must be 0)
    uint8_t page_size        : 1;  // PS: 0 = 4KB page table, 1 = 4MB page (requires PSE)
    uint8_t ignored          : 1;  // Available for OS use
    uint8_t available        : 3;  // Available for OS use (bits 9-11)
    uint32_t page_table_base : 20; // Physical address of page table (bits 12-31, 4KB aligned)
} PageDirectoryEntry_t;

/// Page Table Entry (32-bit format) - Based on OSDev wiki Paging article
typedef struct __attribute__((packed)) {
    uint8_t present              : 1;  // P: Page is present in memory
    uint8_t read_write           : 1;  // R/W: 0 = read-only, 1 = read/write
    uint8_t user_supervisor      : 1;  // U/S: 0 = supervisor only, 1 = user accessible
    uint8_t write_through        : 1;  // PWT: Write-through caching
    uint8_t cache_disable        : 1;  // PCD: Cache disabled
    uint8_t accessed             : 1;  // A: Set by CPU when accessed
    uint8_t dirty                : 1;  // D: Set by CPU when written to
    uint8_t page_attribute_table : 1;  // PAT: Page Attribute Table (if supported)
    uint8_t global               : 1;  // G: Global page (not flushed on CR3 reload, requires PGE in CR4)
    uint8_t available            : 3;  // Available for OS use (bits 9-11)
    uint32_t page_frame_base     : 20; // Physical address of 4KB page frame (bits 12-31, 4KB aligned)
} PageTableEntry_t;

// ============================================================================
// Page Directory / Page Table structures
// ============================================================================

/// Page Directory: Array of 1024 page directory entries (4KB total)
typedef struct __attribute__((packed)) {
    PageDirectoryEntry_t entries[1024];
} PageDirectory_t;

/// Page Table: Array of 1024 page table entries (4KB total)
typedef struct __attribute__((packed)) {
    PageTableEntry_t entries[1024];
} PageTable_t;

// ============================================================================
// Helper macros for address manipulation
// ============================================================================

/**
 * @brief Extract page directory index from virtual address (bits 31-22)
 */
#define PAGE_DIRECTORY_INDEX(virt_addr) (((uint32_t) (virt_addr) >> 22) & 0x3FF)

/**
 * @brief Extract page table index from virtual address (bits 21-12)
 */
#define PAGE_TABLE_INDEX(virt_addr) (((uint32_t) (virt_addr) >> 12) & 0x3FF)

/**
 * @brief Extract physical frame address from page entry (bits 31-12)
 * Works for both PDE (page_table_base) and PTE (page_frame_base)
 */
#define PAGE_FRAME_ADDR(entry_ptr) (((uint32_t) (entry_ptr)->page_table_base) << 12)

/**
 * @brief Extract page offset from virtual address (bits 11-0)
 */
#define PAGE_OFFSET(virt_addr) ((uint32_t) (virt_addr) & 0xFFF)

// ============================================================================
// Address Alignment Helpers
// ============================================================================

/**
 * @brief Align address down to page boundary (4KB)
 */
#define PAGE_ALIGN_DOWN(addr) ((uint32_t) (addr) & 0xFFFFF000)

/**
 * @brief Align address up to page boundary (4KB)
 */
#define PAGE_ALIGN_UP(addr) (((uint32_t) (addr) + 0xFFF) & 0xFFFFF000)

/**
 * @brief Check if address is page-aligned
 */
#define IS_PAGE_ALIGNED(addr) (((uint32_t) (addr) & 0xFFF) == 0)

// ============================================================================
// Low-level Assembly Functions (defined in paging_asm.s)
// ============================================================================

/**
 * @brief Get the current page directory physical address from CR3
 * @return Physical address of the current page directory
 */
extern uint32_t paging_get_cr3(void);

/**
 * @brief Load a new page directory into CR3
 * @param phys_addr Physical address of the page directory (must be 4KB aligned)
 */
extern void paging_load_cr3(uint32_t phys_addr);

/**
 * @brief Invalidate a single TLB entry for a given virtual address
 * @param virt_addr Virtual address to invalidate
 */
extern void paging_invlpg(uint32_t virt_addr);

/**
 * @brief Flush entire TLB by reloading CR3
 */
extern void paging_flush_tlb(void);

// ============================================================================
// Runtime Paging Management API (defined in paging.c)
// ============================================================================

/**
 * @brief Initialize paging management subsystem
 *
 * This function initializes internal structures for runtime paging management.
 * It does NOT set up initial paging (that's already done in boot.s).
 *
 * Call this once during kernel initialization, AFTER boot.s has set up paging.
 */
extern void paging_init_runtime();

/**
 * @brief Map a virtual address to a physical address
 *
 * @param virt_addr Virtual address to map (will be page-aligned down)
 * @param phys_addr Physical address to map to (will be page-aligned down)
 * @param pde_flags Flags for the page directory entry (present, read_write, user_supervisor, etc.)
 * @param pte_flags Flags for the page table entry (present, read_write, user_supervisor, etc.)
 * @return true on success, false on failure
 *
 * Note: If the page table for this virtual address doesn't exist, it will be
 * created automatically (requires a page frame allocator).
 *
 * WARNING: For higher-half kernel, make sure you're mapping to the correct
 * virtual address range (0xC0000000+).
 */
bool paging_map_page(uint32_t virt_addr, uint32_t phys_addr, PageDirectoryEntry_t pde_flags,
                     PageTableEntry_t pte_flags);

/**
 * @brief Unmap a virtual address
 *
 * @param virt_addr Virtual address to unmap
 * @return true if page was unmapped, false if it wasn't mapped
 */
bool paging_unmap_page(uint32_t virt_addr);

/**
 * @brief Get the physical address mapped to a virtual address
 *
 * @param virt_addr Virtual address to translate
 * @param phys_addr Output: physical address (only valid if function returns true)
 * @return true if virtual address is mapped, false otherwise
 */
bool paging_get_physical_address(uint32_t virt_addr, uint32_t *phys_addr);

/**
 * @brief Check if a virtual address is currently mapped
 *
 * @param virt_addr Virtual address to check
 * @return true if mapped, false otherwise
 */
bool paging_is_mapped(uint32_t virt_addr);

// ============================================================================
// Constants
// ============================================================================

/* global_kernel_start is provided by the linker script via PROVIDE().
 * Declare it as a const uint32_t so the compiler treats it as an address-sized
 * symbol whose value can be read at runtime. Use a pointer cast to obtain
 * the integer value. */
extern const uint32_t global_kernel_start;
#define KERNEL_VIRTUAL_BASE ((uint32_t) (uintptr_t) & global_kernel_start) // Higher-half kernel base
#define PAGE_SIZE           4096                                           // 4KB pages
#define ENTRIES_PER_TABLE   1024                                           // 1024 entries per page table/directory

#endif /* !PAGING_H_ */
