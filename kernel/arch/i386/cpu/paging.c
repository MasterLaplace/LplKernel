#include <kernel/boot/multiboot_info.h>
#include <kernel/cpu/paging.h>
#include <stddef.h>

// ============================================================================
// Higher-Half Kernel Paging Implementation
// ============================================================================
//
// IMPORTANT: This implementation assumes the following setup (done in boot.S):
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

// Page directory from boot.S (in higher-half virtual address space)
extern PageDirectory_t boot_page_directory;
extern MultibootInfo_t *multiboot_info;

/* linker-provided markers for the kernel image; used to avoid returning
 * those pages to the free list during initialization.  They reside in
 * the higher-half address space, so subtract the virtual base to get the
 * corresponding physical addresses. */
extern const uint32_t global_kernel_start;
extern const uint32_t global_kernel_end;

// Current page directory (virtual address)
PageDirectory_t *current_page_directory = NULL;

#ifdef REAL_TIME_MODE
/* Simple stack to hold physical addresses of free frames.  This avoids
 * touching unmapped pages during early boot; the addresses are kept in a
 * kernel-resident array.  Allocation and freeing are both O(1). */
#define MAX_FREE_FRAMES 65536u /* ~256 MiB of RAM / 4 KiB pages */
static uint32_t free_stack[MAX_FREE_FRAMES];
static uint32_t free_stack_top = 0;
#else
#endif

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

// Parse the multiboot memory map and push every usable 4‑KiB page
// into the free‑list.  This helper is only used during initialization and
// therefore doesn't need to be re‑entrant or thread‑safe.
static void populate_free_list_from_mmap(void)
{
    if (!multiboot_info)
        return;

    if (!(multiboot_info->flags & (1 << 6)) ||
        multiboot_info->mmap_length == 0 ||
        multiboot_info->mmap_addr == 0)
    {
        return;
    }

    /* The map entries themselves are provided by GRUB and usually live in
     * low memory (<1 MiB).  We can convert the physical pointer to a higher‑half
     * virtual address using the same mapping that covers the first megabyte. */
    uint8_t *mmap_base = (uint8_t *)phys_to_virt((uint32_t)(uintptr_t)multiboot_info->mmap_addr);
    if (!mmap_base)
        return;

    /* Compute kernel physical bounds so we avoid freeing pages belonging to
     * the kernel image.  The globals are addresses in the higher half, so
     * subtract the base to obtain the underlying physical values. */
    uint32_t kernel_phys_start = (uint32_t)&global_kernel_start - KERNEL_VIRTUAL_BASE;
    uint32_t kernel_phys_end   = (uint32_t)&global_kernel_end   - KERNEL_VIRTUAL_BASE;

    uint32_t offset = 0;

    while (offset < multiboot_info->mmap_length)
    {
        MemoryMapEntry_t *entry = (MemoryMapEntry_t *)(mmap_base + offset);
        uint32_t size = entry->size;            // bytes after the size field
        uint64_t base = entry->base_addr;
        uint64_t len  = entry->length;
        uint32_t type = entry->type;

        offset += size + 4; /* move to next descriptor */

        if (type != 1) /* only usable RAM */
            continue;

        uint32_t start = PAGE_ALIGN_UP((uint32_t)base);
        uint32_t end   = PAGE_ALIGN_DOWN((uint32_t)(base + len));

        for (uint32_t addr = start; addr < end; addr += PAGE_SIZE)
        {
            if (addr < 0x100000u)
                continue;                              /* leave low 1 MiB alone */
            if (addr >= kernel_phys_start && addr < kernel_phys_end)
                continue;                              /* skip kernel image */

            page_frame_free(addr);
        }
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

void paging_initialize_runtime(void)
{
    // Set current page directory to the one from boot.s
    current_page_directory = &boot_page_directory;

    // Note: We don't need to do anything else here because boot.s already
    // set up paging correctly. This function just initializes our state
    // for runtime management.
#ifdef REAL_TIME_MODE
    /* reset the stack then populate from multiboot memory map */
    free_stack_top = 0u;
    populate_free_list_from_mmap();
#else
#endif
}

bool page_frame_alloc(uint32_t *phys_addr)
{
#ifdef REAL_TIME_MODE
    if (!phys_addr || free_stack_top == 0u)
        return false;

    /* pop from the stack of free physical frames */
    *phys_addr = free_stack[--free_stack_top];
    return true;
#else
    return false;
#endif
}

void page_frame_free(uint32_t phys_addr)
{
#ifdef REAL_TIME_MODE
    if (phys_addr % PAGE_SIZE != 0u)
        return;

    /* push back onto the stack; if overflow, the page is leaked */
    if (free_stack_top < MAX_FREE_FRAMES)
        free_stack[free_stack_top++] = phys_addr;
#endif
}


#ifdef REAL_TIME_MODE
/**
 * @brief Return number of free frames currently available (for debug).
 */
uint32_t page_frame_count(void)
{
    return free_stack_top;
}
#endif

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
