#include <kernel/config.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/memory/vmm.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define VMM_PAGE_COUNT              (KERNEL_VMM_DYNAMIC_SIZE / PAGE_SIZE)
#define VMM_BITMAP_SIZE             (VMM_PAGE_COUNT / 8)
#define VMM_MEMORY_MAP_RAM          1u /**< A usable RAM entry of the boot memory map. */
#define VMM_MEMORY_MAP_ACPI_RECLAIM 3u /**< An ACPI reclaimable entry of the boot memory map: tables. */
#define VMM_MEMORY_MAP_ACPI_NVS     4u /**< An ACPI NVS entry of the boot memory map: memory the firmware keeps. */

static uint8_t vmm_bitmap[VMM_BITMAP_SIZE];
static uint32_t vmm_last_search_index = 0u;
static bool vmm_initialized = false;
static uint32_t vmm_unmapped_range_count = 0u;

static void vmm_bitmap_set(uint32_t index) { vmm_bitmap[index / 8] |= (1 << (index % 8)); }

static void vmm_bitmap_clear(uint32_t index) { vmm_bitmap[index / 8] &= ~(1 << (index % 8)); }

static bool vmm_bitmap_test(uint32_t index) { return (vmm_bitmap[index / 8] & (1 << (index % 8))) != 0; }

/**
 * @brief Gives a reserved run back: unmaps its first @p mapped pages, then clears the whole run.
 *
 * @param virt_start  First page of the run.
 * @param page_count  Pages in the run.
 * @param mapped      Pages at its start that are mapped.
 * @param free_frames Whether their frames go back to the physical memory manager: a device's do not.
 */
static void vmm_release(uint32_t virt_start, uint32_t page_count, uint32_t mapped, bool free_frames)
{
    const uint32_t start_index = (virt_start - KERNEL_VMM_DYNAMIC_START) / PAGE_SIZE;

    if (mapped != 0u && paging_unmap_range(virt_start, mapped, free_frames) != 0u)
        ++vmm_unmapped_range_count;
    for (uint32_t i = 0u; i < page_count; ++i)
        vmm_bitmap_clear(start_index + i);
}

/**
 * @brief Pages a window of @p size bytes spans, starting @p page_offset bytes into its first page.
 */
static uint32_t vmm_window_page_count(uint32_t page_offset, uint32_t size)
{
    return (page_offset + size + PAGE_SIZE - 1u) / PAGE_SIZE;
}

/**
 * @brief Whether the boot memory map calls the whole page at @p frame memory: usable RAM, ACPI reclaimable or ACPI
 *        NVS.
 */
static bool vmm_frame_is_memory(uint32_t frame)
{
    const uint32_t type = physical_memory_manager_memory_type(frame, PAGE_SIZE);

    return type == VMM_MEMORY_MAP_RAM || type == VMM_MEMORY_MAP_ACPI_RECLAIM || type == VMM_MEMORY_MAP_ACPI_NVS;
}

/**
 * @brief The entry @p frame gets in a window: @p pte_flags, with the memory type the frame decides, whatever kind of
 *        window asks, so that no page has two memory types (Intel SDM vol. 3, §14.12.4), between two windows either.
 *
 * @details A frame the kernel's map of physical memory holds takes that map's type. That map keeps the first MiB
 *          write-back, as Linux does, and past it only RAM (physical_memory_manager_restrict_boot_mapping): a window
 *          there is write-back, and the MTRRs decide its effective type, as Linux does for 0xA0000-0xFFFFF; below it,
 *          where Linux would alias, the window takes the map's type too. Any other frame is write-back when the boot
 *          memory map calls it memory, and uncached otherwise: a reserved page or one no entry holds, whether an ACPI
 *          table or a device's registers lie there.
 */
static PageTableEntry_t vmm_window_page_flags(uint32_t frame, PageTableEntry_t pte_flags)
{
    PageTableEntry_t direct;
    const bool held = frame < KERNEL_VMM_DYNAMIC_START - KERNEL_VIRTUAL_BASE &&
                      paging_get_page_table_entry(frame + KERNEL_VIRTUAL_BASE, &direct) &&
                      ((uint32_t) direct.page_frame_base << 12u) == frame;
    const uint32_t uncached = !held && !vmm_frame_is_memory(frame);

    pte_flags.write_through = held ? direct.write_through : uncached;
    pte_flags.cache_disable = held ? direct.cache_disable : uncached;
    pte_flags.page_attribute_table = held ? direct.page_attribute_table : 0u;
    return pte_flags;
}

/**
 * @brief Maps the frames under @p size bytes from @p physical_base into a free run of kernel pages.
 *
 * @param physical_base Physical address of the window's first byte.
 * @param size          Window length in bytes.
 * @param pte_flags     The flags every page of the window gets, but its memory type, which the frame decides
 *                      (vmm_window_page_flags).
 * @return The virtual address of @p physical_base, its offset within its page kept, or 0 when the
 *         window's last byte would lie past 4 GiB, no run is free or a page cannot be mapped; the run
 *         is then given back, and no frame freed.
 */
static uint32_t vmm_map_window(uint32_t physical_base, uint32_t size, PageTableEntry_t pte_flags)
{
    if (physical_base == 0u || size == 0u || size - 1u > UINT32_MAX - physical_base)
        return 0u;

    const uint32_t page_offset = physical_base & (PAGE_SIZE - 1u);
    const uint32_t first_frame = physical_base - page_offset;
    const uint32_t page_count = vmm_window_page_count(page_offset, size);
    void *const virt = kernel_vmm_reserve_pages(page_count);

    if (virt == NULL)
        return 0u;

    const PageDirectoryEntry_t pde_flags = {.present = 1u, .read_write = 1u};
    const uint32_t virt_start = (uint32_t) (uintptr_t) virt;

    for (uint32_t i = 0u; i < page_count; ++i)
    {
        const uint32_t frame = first_frame + i * PAGE_SIZE;

        if (!paging_map_page(virt_start + i * PAGE_SIZE, frame, pde_flags, vmm_window_page_flags(frame, pte_flags)))
        {
            vmm_release(virt_start, page_count, i, false);
            return 0u;
        }
    }
    return virt_start + page_offset;
}

/**
 * @brief Unmaps a window vmm_map_window gave, and frees none of its frames.
 *
 * @param window The address vmm_map_window returned.
 * @param size   The size it was given.
 */
static void vmm_unmap_window(uint32_t window, uint32_t size)
{
    if (!vmm_initialized || size == 0u || window < KERNEL_VMM_DYNAMIC_START || window >= KERNEL_VMM_DYNAMIC_END)
        return;

    const uint32_t page_offset = window & (PAGE_SIZE - 1u);
    const uint32_t page_count = vmm_window_page_count(page_offset, size);

    vmm_release(window - page_offset, page_count, page_count, false);
}

bool kernel_vmm_initialize(void)
{
    memset(vmm_bitmap, 0, sizeof(vmm_bitmap));
    vmm_last_search_index = 0u;

    vmm_initialized = true;
    return true;
}

void *kernel_vmm_reserve_pages(uint32_t page_count)
{
    if (!vmm_initialized || page_count == 0 || page_count > VMM_PAGE_COUNT)
        return NULL;

    uint32_t found_index = 0xFFFFFFFFu;

    uint32_t i = 0;
    while (i <= VMM_PAGE_COUNT - page_count)
    {
        bool match = true;
        for (uint32_t j = 0u; j < page_count; ++j)
        {
            if (vmm_bitmap_test(i + j))
            {
                match = false;
                i += j + 1;
                break;
            }
        }

        if (match)
        {
            found_index = i;
            break;
        }
    }

    if (found_index == 0xFFFFFFFFu)
        return NULL;

    for (uint32_t i = 0u; i < page_count; ++i)
        vmm_bitmap_set(found_index + i);

    vmm_last_search_index = found_index + page_count;

    return (void *) (uintptr_t) (KERNEL_VMM_DYNAMIC_START + (found_index * PAGE_SIZE));
}

bool kernel_vmm_reserve_at(void *virt, uint32_t page_count)
{
    if (!vmm_initialized || !virt || page_count == 0)
        return false;

    uint32_t virt_addr = (uint32_t) (uintptr_t) virt;
    if (virt_addr < KERNEL_VMM_DYNAMIC_START || (virt_addr + page_count * PAGE_SIZE) > KERNEL_VMM_DYNAMIC_END)
        return false;

    uint32_t start_index = (virt_addr - KERNEL_VMM_DYNAMIC_START) / PAGE_SIZE;

    for (uint32_t i = 0u; i < page_count; ++i)
    {
        if (vmm_bitmap_test(start_index + i))
            return false;
    }

    for (uint32_t i = 0u; i < page_count; ++i)
        vmm_bitmap_set(start_index + i);

    return true;
}

void *kernel_vmm_alloc_pages(uint32_t page_count)
{
    void *virt_base = kernel_vmm_reserve_pages(page_count);
    if (!virt_base)
        return NULL;

    uint32_t start_virt = (uint32_t) (uintptr_t) virt_base;

    for (uint32_t i = 0u; i < page_count; ++i)
    {
        uint32_t phys = physical_memory_manager_page_frame_allocate();
        if (phys == 0u)
        {
            kernel_vmm_free_pages(virt_base, i);
            return NULL;
        }

        PageDirectoryEntry_t pde = {0};
        pde.present = 1;
        pde.read_write = 1;
        pde.user_supervisor = 0;

        PageTableEntry_t pte = {0};
        pte.present = 1;
        pte.read_write = 1;
        pte.user_supervisor = 0;

        if (!paging_map_page(start_virt + (i * PAGE_SIZE), phys, pde, pte))
        {
            physical_memory_manager_page_frame_free(phys);
            kernel_vmm_free_pages(virt_base, i);
            return NULL;
        }
    }

    return virt_base;
}

void kernel_vmm_free_pages(void *ptr, uint32_t page_count)
{
    if (!vmm_initialized || !ptr || page_count == 0)
        return;

    uint32_t virt_start = (uint32_t) (uintptr_t) ptr;
    if (virt_start < KERNEL_VMM_DYNAMIC_START || virt_start >= KERNEL_VMM_DYNAMIC_END)
        return;

    vmm_release(virt_start, page_count, page_count, true);
}

uint32_t kernel_vmm_map_device_window(uint32_t physical_base, uint32_t size)
{
    const PageTableEntry_t pte_flags = {.present = 1u, .read_write = 1u};

    return vmm_map_window(physical_base, size, pte_flags);
}

void kernel_vmm_unmap_device_window(uint32_t window, uint32_t size) { vmm_unmap_window(window, size); }

uint32_t kernel_vmm_map_firmware_window(uint32_t physical_base, uint32_t size)
{
    const PageTableEntry_t pte_flags = {.present = 1u};

    return vmm_map_window(physical_base, size, pte_flags);
}

void kernel_vmm_unmap_firmware_window(uint32_t window, uint32_t size) { vmm_unmap_window(window, size); }

uint32_t kernel_vmm_map_firmware_writable_window(uint32_t physical_base, uint32_t size)
{
    const PageTableEntry_t pte_flags = {.present = 1u, .read_write = 1u};

    return vmm_map_window(physical_base, size, pte_flags);
}

uint32_t kernel_vmm_get_unmapped_range_count(void) { return vmm_unmapped_range_count; }
