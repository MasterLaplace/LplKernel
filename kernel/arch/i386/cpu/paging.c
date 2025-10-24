#include <kernel/cpu/paging.h>
#include <stddef.h>

// ============================================================================
// Higher-Half Kernel Paging Implementation
// ============================================================================
//
// IMPORTANT: This implementation assumes the following setup (done in boot.s):
// 1. Paging is already enabled
// 2. Page directory is at a known physical location
// 3. Higher-half kernel mapped at 0xC0000000
// 4. Identity mapping has been removed after boot
// 5. One page table exists for the kernel (entry 768 in page directory)
//
// This code provides RUNTIME paging management - NOT initial setup.
// ============================================================================

// ============================================================================
// Private State
// ============================================================================

// Page directory from boot.s (in higher-half virtual address space)
extern PageDirectory_t boot_page_directory;

// Current page directory (virtual address)
static PageDirectory_t *current_page_directory = NULL;

// ============================================================================
// Helper: Virtual to Physical Address Translation
// ============================================================================

/**
 * @brief Convert higher-half virtual address to physical address
 *
 * For addresses in kernel space (0xC0000000+), subtract KERNEL_VIRTUAL_BASE.
 * This works because boot.s identity-mapped the kernel's physical location.
 *
 * @param virt_addr Virtual address in higher half
 * @return Corresponding physical address
 */
static inline uint32_t virt_to_phys(uint32_t virt_addr)
{
    if (virt_addr >= KERNEL_VIRTUAL_BASE)
        return virt_addr - KERNEL_VIRTUAL_BASE;
    return virt_addr; // Already physical (shouldn't happen in normal operation)
}

/**
 * @brief Convert physical address to higher-half virtual address
 *
 * @param phys_addr Physical address
 * @return Corresponding virtual address in higher half
 */
static inline uint32_t phys_to_virt(uint32_t phys_addr) { return phys_addr + KERNEL_VIRTUAL_BASE; }

// ============================================================================
// Public API Implementation
// ============================================================================

void paging_init_runtime(void)
{
    // Set current page directory to the one from boot.s
    current_page_directory = &boot_page_directory;

    // Note: We don't need to do anything else here because boot.s already
    // set up paging correctly. This function just initializes our state
    // for runtime management.
}

bool paging_map_page(uint32_t virt_addr, uint32_t phys_addr, PageDirectoryEntry_t pde_flags, PageTableEntry_t pte_flags)
{
    if (!current_page_directory)
        return false;

    // Align addresses to page boundaries
    virt_addr = PAGE_ALIGN_DOWN(virt_addr);
    phys_addr = PAGE_ALIGN_DOWN(phys_addr);

    // Get directory and table indices
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virt_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virt_addr);

    // Get page directory entry
    PageDirectoryEntry_t *pde = &current_page_directory->entries[pd_index];

    // Check if page table exists
    if (!pde->present)
    {
        // Page table doesn't exist - we need to create one
        // TODO: This requires a page frame allocator (Phase 4)
        // For now, we can only map pages in existing tables
        return false;
    }
    else
    {
        /* Update PDE flags while preserving the page_table_base.
         * This uses the provided pde_flags so the caller can request
         * changed permissions for the page table without changing its base.
         */
        pde->present = pde_flags.present;
        pde->read_write = pde_flags.read_write;
        pde->user_supervisor = pde_flags.user_supervisor;
        pde->write_through = pde_flags.write_through;
        pde->cache_disable = pde_flags.cache_disable;
        pde->accessed = pde_flags.accessed;
        pde->reserved_zero = pde_flags.reserved_zero;
        pde->page_size = pde_flags.page_size;
        pde->ignored = pde_flags.ignored;
        pde->available = pde_flags.available;
    }

    // Get physical address of page table from PDE
    uint32_t pt_phys_addr = PAGE_FRAME_ADDR(pde);

    // Convert to virtual address so we can access it
    // The page table is already mapped in the higher half
    PageTable_t *page_table = (PageTable_t *) phys_to_virt(pt_phys_addr);

    // Set the page table entry with all the flags from pte_flags
    PageTableEntry_t *pte = &page_table->entries[pt_index];
    pte->present = pte_flags.present;
    pte->read_write = pte_flags.read_write;
    pte->user_supervisor = pte_flags.user_supervisor;
    pte->write_through = pte_flags.write_through;
    pte->cache_disable = pte_flags.cache_disable;
    pte->accessed = pte_flags.accessed;
    pte->dirty = pte_flags.dirty;
    pte->page_attribute_table = pte_flags.page_attribute_table;
    pte->global = pte_flags.global;
    pte->available = pte_flags.available;
    pte->page_frame_base = phys_addr >> 12; // Physical address shifted right by 12 bits

    // Invalidate TLB entry for this virtual address
    paging_invlpg(virt_addr);

    return true;
}

bool paging_unmap_page(uint32_t virt_addr)
{
    if (!current_page_directory)
        return false;

    virt_addr = PAGE_ALIGN_DOWN(virt_addr);

    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virt_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virt_addr);

    PageDirectoryEntry_t *pde = &current_page_directory->entries[pd_index];

    // Check if page table exists
    if (!pde->present)
        return false;

    // Get page table
    uint32_t pt_phys_addr = PAGE_FRAME_ADDR(pde);
    PageTable_t *page_table = (PageTable_t *) phys_to_virt(pt_phys_addr);

    PageTableEntry_t *pte = &page_table->entries[pt_index];

    // Check if page is mapped
    if (!pte->present)
        return false;

    // Clear the entry (set all fields to 0)
    pte->present = 0;
    pte->read_write = 0;
    pte->user_supervisor = 0;
    pte->write_through = 0;
    pte->cache_disable = 0;
    pte->accessed = 0;
    pte->dirty = 0;
    pte->page_attribute_table = 0;
    pte->global = 0;
    pte->available = 0;
    pte->page_frame_base = 0;

    // Invalidate TLB entry
    paging_invlpg(virt_addr);

    return true;
}

bool paging_get_physical_address(uint32_t virt_addr, uint32_t *phys_addr)
{
    if (!current_page_directory || !phys_addr)
        return false;

    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virt_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virt_addr);
    uint32_t offset = PAGE_OFFSET(virt_addr);

    PageDirectoryEntry_t *pde = &current_page_directory->entries[pd_index];

    // Check if page table exists
    if (!pde->present)
        return false;

    // Get page table
    uint32_t pt_phys_addr = PAGE_FRAME_ADDR(pde);
    PageTable_t *page_table = (PageTable_t *) phys_to_virt(pt_phys_addr);

    PageTableEntry_t *pte = &page_table->entries[pt_index];

    // Check if page is mapped
    if (!pte->present)
        return false;

    // Extract physical frame address and add offset
    *phys_addr = (pte->page_frame_base << 12) | offset;

    return true;
}

bool paging_is_mapped(uint32_t virt_addr)
{
    uint32_t phys_addr;
    return paging_get_physical_address(virt_addr, &phys_addr);
}
