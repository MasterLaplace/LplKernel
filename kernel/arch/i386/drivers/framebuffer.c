/**
 * @file framebuffer.c
 * @brief Linear framebuffer driver implementation
 *
 * This driver provides basic graphics primitives for drawing to a linear
 * framebuffer. It supports 32-bit color depth with Direct Color mode.
 */

#include <kernel/boot/multiboot_info.h>
#include <kernel/cpu/paging.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/lib/asmutils.h>
#include <string.h>

/* Global framebuffer state */
static framebuffer_info_t fb_info = {0};

/* Static page table for framebuffer mapping (768 pages = 3MB, enough for 1024x768x32bpp) */
/* Must be 4KB aligned */
static PageTable_t __attribute__((aligned(4096))) framebuffer_page_table;

/* External multiboot info - note the type is MultibootInfo_t */
extern MultibootInfo_t *multiboot_info;

/* External page directory from paging.c */
extern PageDirectory_t *current_page_directory;

/**
 * @brief Map the framebuffer memory into kernel space
 *
 * The framebuffer is typically at a high physical address (e.g., 0xFD000000)
 * which we need to map into our virtual address space.
 *
 * Since we don't have a page frame allocator yet, we use a static page table
 * allocated in .bss that we manually insert into the page directory.
 *
 * @param phys_addr Physical address of the framebuffer
 * @param size Size of the framebuffer in bytes
 * @return Virtual address of the mapped framebuffer, or NULL on failure
 */
static uint32_t *map_framebuffer(uint32_t phys_addr, uint32_t size)
{
    /* Calculate number of pages needed */
    uint32_t num_pages = (size + 0xFFF) / 0x1000;

    /* Fixed virtual address for the framebuffer: 0xE0000000 (PD index 896) */
    uint32_t virt_addr = 0xE0000000;
    uint32_t pd_index = virt_addr >> 22; /* = 896 */

    /* Safety check */
    if (!current_page_directory)
    {
        return NULL;
    }

    /* Initialize our static page table (clear all entries) */
    for (uint32_t i = 0; i < 1024; i++)
    {
        framebuffer_page_table.entries[i] = (PageTableEntry_t){0};
    }

    /* Map each framebuffer page into our page table */
    for (uint32_t i = 0; i < num_pages && i < 1024; i++)
    {
        uint32_t page_phys = phys_addr + (i * 0x1000);

        /* Setup page table entry: present, read/write, supervisor, write-through */
        framebuffer_page_table.entries[i].present = 1;
        framebuffer_page_table.entries[i].read_write = 1;
        framebuffer_page_table.entries[i].user_supervisor = 0;
        framebuffer_page_table.entries[i].write_through = 1; /* Better for framebuffer */
        framebuffer_page_table.entries[i].cache_disable = 0;
        framebuffer_page_table.entries[i].page_frame_base = page_phys >> 12;
    }

    /* Get physical address of our page table */
    /* Since we're in higher-half kernel, convert virtual to physical */
    uint32_t pt_phys = (uint32_t) &framebuffer_page_table - 0xC0000000;

    /* Insert the page table into the page directory */
    PageDirectoryEntry_t *pde = &current_page_directory->entries[pd_index];
    pde->present = 1;
    pde->read_write = 1;
    pde->user_supervisor = 0;
    pde->write_through = 1;
    pde->cache_disable = 0;
    pde->page_size = 0; /* 4KB pages */
    pde->page_table_base = pt_phys >> 12;

    /* Flush TLB to make the new mappings effective */
    cpu_reload_page_directory();

    return (uint32_t *) virt_addr;
}

bool framebuffer_init(void)
{
    if (fb_info.initialized)
    {
        return true; /* Already initialized */
    }

    /* Check if we have multiboot info */
    if (multiboot_info == NULL)
    {
        return false;
    }

    /* Check if framebuffer info is available (flag bit 12) */
    if (!(multiboot_info->flags & (1 << 12)))
    {
        return false;
    }

    /* Check framebuffer type - we only support linear framebuffer (type 1) */
    if (multiboot_info->framebuffer_type != 1)
    {
        return false; /* Not a linear framebuffer */
    }

    /* We only support 32-bit color for now */
    if (multiboot_info->framebuffer_bpp != 32)
    {
        return false;
    }

    /* Store framebuffer information */
    fb_info.physical_addr = (uint32_t) multiboot_info->framebuffer_addr;
    fb_info.width = multiboot_info->framebuffer_width;
    fb_info.height = multiboot_info->framebuffer_height;
    fb_info.pitch = multiboot_info->framebuffer_pitch;
    fb_info.bpp = multiboot_info->framebuffer_bpp;

    /* Store color field information - these are in the direct_color_t union member */
    /* Note: Some GRUB/QEMU combinations return 0 for mask sizes, which is incorrect.
     * For 32-bit color, we assume 8 bits per channel if mask is 0 */
    fb_info.red_pos = multiboot_info->direct_color_t.framebuffer_red_field_position;
    fb_info.red_mask = multiboot_info->direct_color_t.framebuffer_red_mask_size;
    if (fb_info.red_mask == 0)
        fb_info.red_mask = 8; /* Default to 8 bits */

    fb_info.green_pos = multiboot_info->direct_color_t.framebuffer_green_field_position;
    fb_info.green_mask = multiboot_info->direct_color_t.framebuffer_green_mask_size;
    if (fb_info.green_mask == 0)
        fb_info.green_mask = 8;

    fb_info.blue_pos = multiboot_info->direct_color_t.framebuffer_blue_field_position;
    fb_info.blue_mask = multiboot_info->direct_color_t.framebuffer_blue_mask_size;
    if (fb_info.blue_mask == 0)
        fb_info.blue_mask = 8;

    /* Calculate framebuffer size */
    uint32_t fb_size = fb_info.pitch * fb_info.height;

    /* Map the framebuffer into kernel virtual memory */
    fb_info.buffer = map_framebuffer(fb_info.physical_addr, fb_size);
    if (fb_info.buffer == NULL)
    {
        return false;
    }

    fb_info.initialized = true;
    return true;
}

bool framebuffer_available(void) { return fb_info.initialized; }

const framebuffer_info_t *framebuffer_get_info(void) { return &fb_info; }

uint32_t framebuffer_color_to_pixel(color_t color)
{
    if (!fb_info.initialized)
    {
        return 0;
    }

    /* Build the pixel value based on field positions */
    uint32_t pixel = 0;
    pixel |= ((uint32_t) color.r) << fb_info.red_pos;
    pixel |= ((uint32_t) color.g) << fb_info.green_pos;
    pixel |= ((uint32_t) color.b) << fb_info.blue_pos;

    return pixel;
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, color_t color)
{
    if (!fb_info.initialized)
    {
        return;
    }

    /* Bounds checking */
    if (x >= fb_info.width || y >= fb_info.height)
    {
        return;
    }

    /* Calculate pixel offset */
    /* pitch is in bytes, but buffer is uint32_t* so divide by 4 */
    uint32_t offset = y * (fb_info.pitch / 4) + x;

    /* Write the pixel */
    fb_info.buffer[offset] = framebuffer_color_to_pixel(color);
}

color_t framebuffer_get_pixel(uint32_t x, uint32_t y)
{
    color_t color = {0, 0, 0, 255};

    if (!fb_info.initialized)
    {
        return color;
    }

    if (x >= fb_info.width || y >= fb_info.height)
    {
        return color;
    }

    uint32_t offset = y * (fb_info.pitch / 4) + x;
    uint32_t pixel = fb_info.buffer[offset];

    /* Extract color components */
    color.r = (pixel >> fb_info.red_pos) & ((1 << fb_info.red_mask) - 1);
    color.g = (pixel >> fb_info.green_pos) & ((1 << fb_info.green_mask) - 1);
    color.b = (pixel >> fb_info.blue_pos) & ((1 << fb_info.blue_mask) - 1);

    return color;
}

void framebuffer_clear(color_t color)
{
    if (!fb_info.initialized)
    {
        return;
    }

    uint32_t pixel = framebuffer_color_to_pixel(color);
    uint32_t total_pixels = fb_info.width * fb_info.height;

    /* Fast clear using 32-bit writes */
    for (uint32_t i = 0; i < total_pixels; i++)
    {
        fb_info.buffer[i] = pixel;
    }
}

void framebuffer_draw_hline(uint32_t x1, uint32_t x2, uint32_t y, color_t color)
{
    if (!fb_info.initialized)
    {
        return;
    }

    /* Ensure x1 <= x2 */
    if (x1 > x2)
    {
        uint32_t tmp = x1;
        x1 = x2;
        x2 = tmp;
    }

    /* Bounds checking */
    if (y >= fb_info.height)
    {
        return;
    }
    if (x2 >= fb_info.width)
    {
        x2 = fb_info.width - 1;
    }

    uint32_t pixel = framebuffer_color_to_pixel(color);
    uint32_t offset = y * (fb_info.pitch / 4) + x1;

    for (uint32_t x = x1; x <= x2; x++)
    {
        fb_info.buffer[offset++] = pixel;
    }
}

void framebuffer_draw_vline(uint32_t x, uint32_t y1, uint32_t y2, color_t color)
{
    if (!fb_info.initialized)
    {
        return;
    }

    /* Ensure y1 <= y2 */
    if (y1 > y2)
    {
        uint32_t tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    /* Bounds checking */
    if (x >= fb_info.width)
    {
        return;
    }
    if (y2 >= fb_info.height)
    {
        y2 = fb_info.height - 1;
    }

    uint32_t pixel = framebuffer_color_to_pixel(color);
    uint32_t pitch_in_dwords = fb_info.pitch / 4;

    for (uint32_t y = y1; y <= y2; y++)
    {
        fb_info.buffer[y * pitch_in_dwords + x] = pixel;
    }
}

void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color)
{
    if (!fb_info.initialized || width == 0 || height == 0)
    {
        return;
    }

    /* Draw top and bottom horizontal lines */
    framebuffer_draw_hline(x, x + width - 1, y, color);
    framebuffer_draw_hline(x, x + width - 1, y + height - 1, color);

    /* Draw left and right vertical lines */
    framebuffer_draw_vline(x, y, y + height - 1, color);
    framebuffer_draw_vline(x + width - 1, y, y + height - 1, color);
}

void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color)
{
    if (!fb_info.initialized || width == 0 || height == 0)
    {
        return;
    }

    /* Bounds checking */
    if (x >= fb_info.width || y >= fb_info.height)
    {
        return;
    }
    if (x + width > fb_info.width)
    {
        width = fb_info.width - x;
    }
    if (y + height > fb_info.height)
    {
        height = fb_info.height - y;
    }

    uint32_t pixel = framebuffer_color_to_pixel(color);
    uint32_t pitch_in_dwords = fb_info.pitch / 4;

    for (uint32_t row = 0; row < height; row++)
    {
        uint32_t offset = (y + row) * pitch_in_dwords + x;
        for (uint32_t col = 0; col < width; col++)
        {
            fb_info.buffer[offset + col] = pixel;
        }
    }
}
