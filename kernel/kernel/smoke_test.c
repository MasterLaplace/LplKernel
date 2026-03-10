#define __LPL_KERNEL__

#include <kernel/config.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/lib/asmutils.h>
#include <kernel/smoke_test.h>

void kernel_smoke_test_run_physical_memory_manager_allocate_free(Serial_t *serial_port)
{
    uint32_t page_address_1 = physical_memory_manager_page_frame_allocate();
    uint32_t page_address_2 = physical_memory_manager_page_frame_allocate();

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: example allocate -> ");
    serial_write_hex32(serial_port, page_address_1);
    serial_write_string(serial_port, ", ");
    serial_write_hex32(serial_port, page_address_2);
    serial_write_string(serial_port, "\n");

    if (page_address_1)
        physical_memory_manager_page_frame_free(page_address_1);
    if (page_address_2)
        physical_memory_manager_page_frame_free(page_address_2);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: after free count = ");
    serial_write_int(serial_port, (int32_t) physical_memory_manager_get_free_page_count());
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_physical_memory_manager_buddy_coalesce(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy coalesce smoke: skipped (client)\n");
    return;
#else
    uint32_t pages[128] = {0};
    uint32_t allocated_count = 0u;
    int32_t buddy_index_a = -1;
    int32_t buddy_index_b = -1;

    for (uint32_t i = 0u; i < 128u; ++i)
    {
        pages[i] = physical_memory_manager_page_frame_allocate();
        if (!pages[i])
            break;

        allocated_count = i + 1u;

        for (uint32_t j = 0u; j < i; ++j)
        {
            if ((pages[j] ^ PAGE_SIZE) == pages[i])
            {
                buddy_index_a = (int32_t) j;
                buddy_index_b = (int32_t) i;
                break;
            }
        }

        if (buddy_index_a >= 0)
            break;
    }

    if (buddy_index_a < 0)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: PMM buddy coalesce smoke: skipped (no pair found)\n");
        for (uint32_t i = 0u; i < allocated_count; ++i)
            physical_memory_manager_page_frame_free(pages[i]);
        return;
    }

    uint32_t page_a = pages[(uint32_t) buddy_index_a];
    uint32_t page_b = pages[(uint32_t) buddy_index_b];
    uint32_t block_base = (page_a < page_b) ? page_a : page_b;

    physical_memory_manager_page_frame_free(page_a);
    physical_memory_manager_page_frame_free(page_b);

    bool merged_order_1 = physical_memory_manager_debug_is_free_block(block_base, 1u);

    for (uint32_t i = 0u; i < allocated_count; ++i)
    {
        if ((int32_t) i == buddy_index_a || (int32_t) i == buddy_index_b)
            continue;
        physical_memory_manager_page_frame_free(pages[i]);
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy coalesce smoke: pair=");
    serial_write_hex32(serial_port, page_a);
    serial_write_string(serial_port, "/");
    serial_write_hex32(serial_port, page_b);
    serial_write_string(serial_port, ", merged_order1=");
    serial_write_int(serial_port, (int32_t) merged_order_1);
    if (merged_order_1)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_physical_memory_manager_buddy_stress(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy stress: skipped (client)\n");
    return;
#else
    uint32_t pages[96] = {0};
    uint32_t allocation_count = 0u;
    uint32_t free_count_before = physical_memory_manager_get_free_page_count();
    uint32_t rejected_before = physical_memory_manager_debug_get_rejected_free_count();
    uint32_t double_free_before = physical_memory_manager_debug_get_double_free_count();

    for (uint32_t i = 0u; i < 96u; ++i)
    {
        pages[i] = physical_memory_manager_page_frame_allocate();
        if (!pages[i])
            break;
        ++allocation_count;
    }

    if (allocation_count < 3u)
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy stress: skipped (pool too small)\n");
        for (uint32_t i = 0u; i < allocation_count; ++i)
            physical_memory_manager_page_frame_free(pages[i]);
        return;
    }

    uint32_t victim = pages[1u];

    for (uint32_t i = 0u; i < allocation_count; ++i)
    {
        if ((i & 1u) != 0u)
            physical_memory_manager_page_frame_free(pages[i]);
    }
    for (uint32_t i = 0u; i < allocation_count; ++i)
    {
        if ((i & 1u) == 0u)
            physical_memory_manager_page_frame_free(pages[i]);
    }

    /* Intentional double free to validate guard path. */
    physical_memory_manager_page_frame_free(victim);

    uint32_t free_count_after = physical_memory_manager_get_free_page_count();
    uint32_t rejected_after = physical_memory_manager_debug_get_rejected_free_count();
    uint32_t double_free_after = physical_memory_manager_debug_get_double_free_count();

    bool free_count_restored = (free_count_after == free_count_before);
    bool guard_triggered =
        (rejected_after == (rejected_before + 1u)) && (double_free_after == (double_free_before + 1u));

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy stress: alloc=");
    serial_write_int(serial_port, (int32_t) allocation_count);
    serial_write_string(serial_port, ", free_restored=");
    serial_write_int(serial_port, (int32_t) free_count_restored);
    serial_write_string(serial_port, ", guard_triggered=");
    serial_write_int(serial_port, (int32_t) guard_triggered);
    if (free_count_restored && guard_triggered)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_physical_memory_manager_buddy_order(Serial_t *serial_port)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy order smoke: skipped (client)\n");
    return;
#else
    uint32_t free_before = physical_memory_manager_get_free_page_count();
    uint32_t block = physical_memory_manager_page_frame_allocate_order(1u);

    if (!block)
    {
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy order smoke: alloc failed\n");
        return;
    }

    uint32_t free_after_alloc = physical_memory_manager_get_free_page_count();
    bool alloc_delta_ok = (free_after_alloc + 2u == free_before);

    physical_memory_manager_page_frame_free_order(block, 1u);

    uint32_t free_after_free = physical_memory_manager_get_free_page_count();
    bool free_restored = (free_after_free == free_before);

    uint32_t block_realloc = physical_memory_manager_page_frame_allocate_order(1u);
    bool realloc_ok = (block_realloc != 0u);
    if (block_realloc)
        physical_memory_manager_page_frame_free_order(block_realloc, 1u);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: PMM buddy order smoke: block=");
    serial_write_hex32(serial_port, block);
    serial_write_string(serial_port, ", alloc_delta_ok=");
    serial_write_int(serial_port, (int32_t) alloc_delta_ok);
    serial_write_string(serial_port, ", free_restored=");
    serial_write_int(serial_port, (int32_t) free_restored);
    serial_write_string(serial_port, ", realloc_ok=");
    serial_write_int(serial_port, (int32_t) realloc_ok);
    if (alloc_delta_ok && free_restored && realloc_ok)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
#endif
}

void kernel_smoke_test_run_paging_runtime_page_table_reclaim(Serial_t *serial_port)
{
    uint32_t virt_base = 0u;
    bool candidate_found = false;

    for (uint32_t candidate = 0xD0000000u; candidate <= 0xFF800000u; candidate += 0x00400000u)
    {
        if (paging_is_mapped(candidate) || paging_is_mapped(candidate + PAGE_SIZE))
            continue;

        virt_base = candidate;
        candidate_found = true;
        break;
    }

    if (!candidate_found)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: skipped (no free 4MB slot)\n");
        return;
    }

    uint32_t runtime_pt_before = paging_get_runtime_owned_page_table_count();
    uint32_t phys_a = physical_memory_manager_page_frame_allocate();
    uint32_t phys_b = physical_memory_manager_page_frame_allocate();

    if (!phys_a || !phys_b)
    {
        if (phys_a)
            physical_memory_manager_page_frame_free(phys_a);
        if (phys_b)
            physical_memory_manager_page_frame_free(phys_b);

        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: skipped (no physical pages)\n");
        return;
    }

    PageDirectoryEntry_t pde_flags = {0};
    pde_flags.present = 1;
    pde_flags.read_write = 1;

    PageTableEntry_t pte_flags = {0};
    pte_flags.present = 1;
    pte_flags.read_write = 1;

    bool map_a = paging_map_page(virt_base, phys_a, pde_flags, pte_flags);
    bool map_b = false;

    if (!map_a)
    {
        physical_memory_manager_page_frame_free(phys_a);
        physical_memory_manager_page_frame_free(phys_b);
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: map A failed\n");
        return;
    }

    map_b = paging_map_page(virt_base + PAGE_SIZE, phys_b, pde_flags, pte_flags);
    if (!map_b)
    {
        paging_unmap_page(virt_base);
        physical_memory_manager_page_frame_free(phys_a);
        physical_memory_manager_page_frame_free(phys_b);
        serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: map B failed\n");
        return;
    }

    uint32_t runtime_pt_after_map = paging_get_runtime_owned_page_table_count();

    bool unmap_b = paging_unmap_page(virt_base + PAGE_SIZE);
    bool unmap_a = paging_unmap_page(virt_base);

    physical_memory_manager_page_frame_free(phys_b);
    physical_memory_manager_page_frame_free(phys_a);

    uint32_t runtime_pt_after_unmap = paging_get_runtime_owned_page_table_count();

    if (runtime_pt_after_map <= runtime_pt_before)
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: skipped (no new runtime PT)\n");
        return;
    }

    bool reclaimed = (runtime_pt_after_unmap == runtime_pt_before);
    bool pass = unmap_a && unmap_b && reclaimed;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: paging reclaim smoke: pt_before=");
    serial_write_int(serial_port, (int32_t) runtime_pt_before);
    serial_write_string(serial_port, ", pt_after_map=");
    serial_write_int(serial_port, (int32_t) runtime_pt_after_map);
    serial_write_string(serial_port, ", pt_after_unmap=");
    serial_write_int(serial_port, (int32_t) runtime_pt_after_unmap);
    serial_write_string(serial_port, ", reclaimed=");
    serial_write_int(serial_port, (int32_t) reclaimed);
    if (pass)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}

void kernel_smoke_test_run_division_error(void)
{
    volatile int zero = 0;
    volatile int trap = 1 / zero;

    (void) trap;
}

void kernel_smoke_test_run_debug_exception(void)
{
    __asm__ volatile("pushf\n\t"
                     "orl $0x100, (%%esp)\n\t"
                     "popf\n\t"
                     "nop\n\t"
                     :
                     :
                     : "cc", "memory");
}

void kernel_smoke_test_run_breakpoint_exception(void) { __asm__ volatile("int3"); }

void kernel_smoke_test_run_invalid_opcode_exception(void) { __asm__ volatile("ud2"); }

void kernel_smoke_test_run_general_protection_exception(void)
{
    __asm__ volatile("xorl %%eax, %%eax\n\t"
                     "movw %%ax, %%ds\n\t"
                     "movl (%%eax), %%eax\n\t"
                     :
                     :
                     : "eax", "memory");
}

void kernel_smoke_test_run_page_fault_exception(void)
{
    volatile uint32_t *const non_present_virtual_address = (volatile uint32_t *) 0xE0001000u;
    volatile uint32_t trap = *non_present_virtual_address;

    (void) trap;
}

void kernel_smoke_test_run_double_fault_exception(void)
{
    /* Synthetic vector injection for #DF handler-path validation. */
    __asm__ volatile("int $0x08");
}

void kernel_smoke_test_run_graphics_demo(Serial_t *serial_port)
{
    framebuffer_clear(framebuffer_rgb(0, 0, 64));
    framebuffer_fill_rect(50, 50, 200, 100, COLOR_RED);
    framebuffer_draw_rect(300, 50, 200, 100, COLOR_GREEN);
    framebuffer_fill_rect(550, 50, 200, 100, COLOR_BLUE);

    for (uint32_t x = 50; x < 750; x++)
    {
        uint8_t red = (x - 50u) * 255u / 700u;
        uint8_t green = 255u - red;

        framebuffer_draw_vline(x, 200, 280, framebuffer_rgb(red, green, 128));
    }

    framebuffer_draw_hline(50, 750, 320, COLOR_WHITE);
    framebuffer_draw_hline(50, 750, 340, COLOR_YELLOW);
    framebuffer_draw_hline(50, 750, 360, COLOR_CYAN);
    framebuffer_draw_hline(50, 750, 380, COLOR_MAGENTA);

    for (uint32_t y = 420; y < 620; y += 2)
    {
        for (uint32_t x = 50; x < 250; x += 2)
            framebuffer_put_pixel(x, y, COLOR_WHITE);
    }

    for (uint32_t i = 0; i < 50; i += 5)
    {
        color_t color = framebuffer_rgb(i * 5u, 255u - i * 5u, 128u);
        framebuffer_draw_rect(300 + i, 420 + i, 200 - 2 * i, 200 - 2 * i, color);
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: graphics demo displayed!\n");
}

void kernel_smoke_test_run_interrupt_request_runtime_status(Serial_t *serial_port)
{
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: IRQ runtime status: ticks=");
    serial_write_int(serial_port, (int32_t) clock_get_tick_count());
    serial_write_string(serial_port, ", pit_hz=");
    serial_write_int(serial_port, (int32_t) clock_get_tick_hz());
    serial_write_string(serial_port, ", spurious7=");
    serial_write_int(serial_port, (int32_t) clock_get_spurious_irq7_count());
    serial_write_string(serial_port, ", spurious15=");
    serial_write_int(serial_port, (int32_t) clock_get_spurious_irq15_count());
    serial_write_string(serial_port, ", rtc_irq=");
    serial_write_int(serial_port, (int32_t) clock_get_rtc_periodic_interrupt_count());
    serial_write_string(serial_port, ", rtc_periodic=");
    serial_write_int(serial_port, (int32_t) clock_is_rtc_periodic_enabled());
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_realtime_clock_snapshot(Serial_t *serial_port)
{
    RealtimeClockTime_t current_time;

    clock_read_walltime(&current_time);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: RTC snapshot: ");
    serial_write_int(serial_port, (int32_t) current_time.hour);
    serial_write_string(serial_port, ":");
    serial_write_int(serial_port, (int32_t) current_time.minute);
    serial_write_string(serial_port, ":");
    serial_write_int(serial_port, (int32_t) current_time.second);
    serial_write_string(serial_port, " ");
    serial_write_int(serial_port, (int32_t) current_time.day);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) current_time.month);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) current_time.year);
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_apic_periodic_mode(Serial_t *serial_port)
{
    uint32_t start_tick;
    uint32_t end_tick;
    uint32_t target_tick;

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: APIC periodic smoke: state=");
    serial_write_string(serial_port, advanced_pic_timer_backend_name());
    serial_write_string(serial_port, ", apic_owner=");
    serial_write_int(serial_port, (int32_t) interrupt_request_is_timer_owner_apic());
    serial_write_string(serial_port, ", periodic=");
    serial_write_int(serial_port, (int32_t) advanced_pic_timer_backend_is_periodic_mode_enabled());
    serial_write_string(serial_port, "\n");

    if (!interrupt_request_is_timer_owner_apic() || !advanced_pic_timer_backend_is_periodic_mode_enabled())
    {
        serial_write_string(serial_port,
                            "[" KERNEL_SYSTEM_STRING "]: APIC periodic smoke: skipped (owner/path inactive)\n");
        return;
    }

    start_tick = clock_get_tick_count();
    target_tick = start_tick + 8u;
    while (clock_get_tick_count() < target_tick)
        cpu_no_operation();
    end_tick = clock_get_tick_count();

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: APIC periodic smoke: delta_ticks=");
    serial_write_int(serial_port, (int32_t) (end_tick - start_tick));
    if ((end_tick - start_tick) >= 8u)
        serial_write_string(serial_port, " (pass)\n");
    else
        serial_write_string(serial_port, " (fail)\n");
}
