#include <kernel/arch/capabilities.h>
#include <kernel/cpu/paging.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/section_protection.h>

/**
 * @brief Page fault error code bits (Intel SDM Vol.3A §4.7).
 *
 * Bit 0 separates a protection violation from a page that is simply not present; bit 1 separates a
 * write from a read. The probe expects both set, and declines anything else so an unrelated fault
 * still reaches the panic handler.
 */
#define SECTION_PROTECTION_FAULT_PROTECTION_VIOLATION 0x1u
#define SECTION_PROTECTION_FAULT_WRITE_ACCESS         0x2u

#define SECTION_PROTECTION_PAGE_SIZE_BYTES 4096u

/**
 * @brief Provided by arch/i386/linker.ld.
 *
 * Both are page-aligned there, which is what lets the walk below step a page at a time without a
 * partial first or last page. Declared as objects and used through their address, the same idiom
 * paging.h already documents for global_kernel_start.
 */
extern const uint32_t _kernel_read_only_start;
extern const uint32_t _kernel_read_only_end;

static bool section_protection_active = false;
static uint32_t section_protection_read_only_page_count = 0u;

/** Written by the probe, read by the page fault handler in interrupt context. */
static volatile bool section_protection_probe_is_armed = false;
static volatile uint32_t section_protection_probe_target = 0u;
static volatile uint32_t section_protection_probe_resume_address = 0u;
static volatile uint32_t section_protection_recovered_fault_count = 0u;

static uint32_t section_protection_range_start(void) { return (uint32_t) &_kernel_read_only_start; }

static uint32_t section_protection_range_end(void) { return (uint32_t) &_kernel_read_only_end; }

bool kernel_section_protection_apply(void)
{
#if !KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT
    section_protection_active = false;
    section_protection_read_only_page_count = 0u;
    return false;
#else
    const uint32_t range_start = section_protection_range_start();
    const uint32_t range_end = section_protection_range_end();

    section_protection_active = false;
    section_protection_read_only_page_count = 0u;

    if (range_end <= range_start)
        return false;

    bool all_pages_protected = true;

    for (uint32_t page = range_start; page < range_end; page += SECTION_PROTECTION_PAGE_SIZE_BYTES)
    {
        if (paging_set_page_read_only(page))
            ++section_protection_read_only_page_count;
        else
            all_pages_protected = false;
    }

    section_protection_active = all_pages_protected;
    return all_pages_protected;
#endif
}

bool kernel_section_protection_is_active(void) { return section_protection_active; }

bool kernel_section_protection_write_protect_is_enabled(void)
{
    const uint32_t control_register_0 = asmutils_read_control_register_0();

    return (control_register_0 & KERNEL_SECTION_PROTECTION_CONTROL_REGISTER_0_WRITE_PROTECT_BIT) != 0u;
}

uint32_t kernel_section_protection_get_read_only_page_count(void) { return section_protection_read_only_page_count; }

uint32_t kernel_section_protection_get_range_start(void) { return section_protection_range_start(); }

uint32_t kernel_section_protection_get_range_end(void) { return section_protection_range_end(); }

uint32_t kernel_section_protection_get_recovered_fault_count(void) { return section_protection_recovered_fault_count; }

bool kernel_section_protection_probe_write(volatile uint8_t *target)
{
    if (!target)
        return false;

    const uint32_t fault_count_before = section_protection_recovered_fault_count;
    const uint8_t original_value = *target;

    section_protection_probe_target = (uint32_t) target;
    section_protection_probe_resume_address = 0u;
    section_protection_probe_is_armed = true;
    asmutils_store_zero_with_resume_address(target, &section_protection_probe_resume_address);

    section_protection_probe_is_armed = false;
    section_protection_probe_resume_address = 0u;
    section_protection_probe_target = 0u;

    const bool did_fault = section_protection_recovered_fault_count != fault_count_before;
    if (!did_fault)
        *target = original_value;

    return did_fault;
}

bool kernel_section_protection_handle_page_fault(const InterruptFrame_t *frame, uint32_t fault_address)
{
    if (!frame || !section_protection_probe_is_armed)
        return false;

    if (section_protection_probe_resume_address == 0u)
        return false;

    if (fault_address != section_protection_probe_target)
        return false;

    const uint32_t expected_bits =
        SECTION_PROTECTION_FAULT_PROTECTION_VIOLATION | SECTION_PROTECTION_FAULT_WRITE_ACCESS;

    if ((frame->err_code & expected_bits) != expected_bits)
        return false;

    section_protection_probe_is_armed = false;
    ++section_protection_recovered_fault_count;
    interrupt_frame_set_resume_address(frame, section_protection_probe_resume_address);
    return true;
}
