/**
 * @file paging.c
 * @brief Runtime paging management for the higher-half kernel.
 *
 * @details Assumes boot.S has already enabled paging, mapped the kernel
 *          at 0xC0000000 (PD entries 768–771), and removed the identity
 *          mapping.  This module provides runtime map / unmap / query
 *          operations on the active page directory.
 */

#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <stddef.h>
#include <string.h>

////////////////////////////////////////////////////////////
// Private State
////////////////////////////////////////////////////////////

extern PageDirectory_t boot_page_directory;

PageDirectory_t *current_page_directory = NULL;

////////////////////////////////////////////////////////////
// Address Translation
////////////////////////////////////////////////////////////

/**
 * @brief Convert a higher-half virtual address to its physical counterpart.
 * @param virt_addr Virtual address (should be >= KERNEL_VIRTUAL_BASE).
 * @return Physical address.
 */
static inline uint32_t virt_to_phys(uint32_t virt_addr)
{
    if (virt_addr >= KERNEL_VIRTUAL_BASE)
        return virt_addr - KERNEL_VIRTUAL_BASE;
    return virt_addr;
}

/**
 * @brief Convert a physical address to its higher-half virtual alias.
 * @param phys_addr Physical address.
 * @return Corresponding virtual address.
 */
static inline uint32_t phys_to_virt(uint32_t phys_addr) { return phys_addr + KERNEL_VIRTUAL_BASE; }

////////////////////////////////////////////////////////////
// Page Table Helpers
////////////////////////////////////////////////////////////

/**
 * @brief Resolve a present PDE to the Page Table it references.
 * @param pde Pointer to a present Page Directory Entry.
 * @return Virtual pointer to the Page Table.
 */
static inline PageTable_t *paging_get_page_table(const PageDirectoryEntry_t *pde)
{
    return (PageTable_t *) phys_to_virt(PAGE_FRAME_ADDR(pde));
}

/**
 * @brief Allocate and install a new Page Table for a missing PDE.
 *
 * @details Obtains a physical page via page_frame_alloc(), zeroes it to
 *          prevent the CPU from interpreting stale data as PTEs, then
 *          installs it into the given PDE with the specified flags.
 *
 * @param pde       Pointer to the PDE to populate.
 * @param pde_flags Permission flags for the new PDE.
 * @return true on success, false if no physical memory is available.
 */
static bool paging_create_page_table(PageDirectoryEntry_t *pde, PageDirectoryEntry_t pde_flags)
{
    uint32_t new_pt_phys = page_frame_alloc();
    if (new_pt_phys == 0)
        return false;

    memset((void *) phys_to_virt(new_pt_phys), 0, PAGE_SIZE);

    pde->present = 1;
    pde->read_write = pde_flags.read_write;
    pde->user_supervisor = pde_flags.user_supervisor;
    pde->write_through = pde_flags.write_through;
    pde->cache_disable = pde_flags.cache_disable;
    pde->accessed = 0;
    pde->reserved_zero = 0;
    pde->page_size = 0;
    pde->ignored = 0;
    pde->available = 0;
    pde->page_table_base = new_pt_phys >> 12;
    return true;
}

/**
 * @brief Update the permission flags of an existing PDE.
 *
 * @details Preserves page_table_base while applying the new flags.
 *
 * @param pde       Pointer to the existing PDE.
 * @param pde_flags New flags to apply.
 */
static void paging_update_pde_flags(PageDirectoryEntry_t *pde, PageDirectoryEntry_t pde_flags)
{
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

/**
 * @brief Write a physical mapping into a Page Table Entry.
 *
 * @param pte       Pointer to the PTE to populate.
 * @param phys_addr Page-aligned physical address to map.
 * @param pte_flags Permission flags for the PTE.
 */
static void paging_install_pte(PageTableEntry_t *pte, uint32_t phys_addr, PageTableEntry_t pte_flags)
{
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
    pte->page_frame_base = phys_addr >> 12;
}

/**
 * @brief Clear all fields of a Page Table Entry.
 * @param pte Pointer to the PTE to zero out.
 */
static void paging_clear_pte(PageTableEntry_t *pte)
{
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
}

////////////////////////////////////////////////////////////
// Public API functions of the Paging module
////////////////////////////////////////////////////////////

void paging_initialize_runtime(void) { current_page_directory = &boot_page_directory; }

bool paging_map_page(uint32_t virt_addr, uint32_t phys_addr, PageDirectoryEntry_t pde_flags, PageTableEntry_t pte_flags)
{
    if (!current_page_directory)
        return false;

    virt_addr = PAGE_ALIGN_DOWN(virt_addr);
    phys_addr = PAGE_ALIGN_DOWN(phys_addr);

    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virt_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virt_addr);

    PageDirectoryEntry_t *pde = &current_page_directory->entries[pd_index];

    if (!pde->present)
    {
        if (!paging_create_page_table(pde, pde_flags))
            return false;
    }
    else
    {
        paging_update_pde_flags(pde, pde_flags);
    }

    PageTable_t *page_table = paging_get_page_table(pde);
    paging_install_pte(&page_table->entries[pt_index], phys_addr, pte_flags);
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
    if (!pde->present)
        return false;

    PageTable_t *page_table = paging_get_page_table(pde);
    PageTableEntry_t *pte = &page_table->entries[pt_index];

    if (!pte->present)
        return false;

    paging_clear_pte(pte);
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
    if (!pde->present)
        return false;

    PageTable_t *page_table = paging_get_page_table(pde);
    PageTableEntry_t *pte = &page_table->entries[pt_index];

    if (!pte->present)
        return false;

    *phys_addr = (pte->page_frame_base << 12) | offset;
    return true;
}

bool paging_is_mapped(uint32_t virt_addr)
{
    uint32_t unused;
    return paging_get_physical_address(virt_addr, &unused);
}
