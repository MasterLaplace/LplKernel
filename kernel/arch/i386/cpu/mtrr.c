#include <kernel/cpu/mtrr.h>
#include <kernel/lib/asmutils.h>

#define MEMORY_TYPE_RANGE_REGISTERS_CPUID_FLAG     (1u << 12)  /**< CPUID.01H:EDX's MTRR flag. */
#define MEMORY_TYPE_RANGE_REGISTERS_CAPABILITY     0x0FEu      /**< IA32_MTRRCAP. */
#define MEMORY_TYPE_RANGE_REGISTERS_DEFAULT_TYPE   0x2FFu      /**< IA32_MTRR_DEF_TYPE. */
#define MEMORY_TYPE_RANGE_REGISTERS_FIRST_BASE     0x200u      /**< IA32_MTRR_PHYSBASE0; PHYSMASKn follows BASEn. */
#define MEMORY_TYPE_RANGE_REGISTERS_VARIABLE_COUNT 0xFFull     /**< IA32_MTRRCAP's VCNT field, bits 7:0. */
#define MEMORY_TYPE_RANGE_REGISTERS_ADDRESS_BITS   (~0xFFFull) /**< PhysBase and PhysMask, bits 63:12. */
#define MEMORY_TYPE_RANGE_REGISTERS_FIXED_END      0x100000u   /**< Where the fixed ranges end, 1 MiB. */

static MemoryTypeRangeRegisters_t memory_type_range_registers_boot;

/**
 * @brief Whether @p range is enabled and holds @p page: page AND PhysMask equals PhysBase AND PhysMask (§14.11.2.3).
 */
static bool memory_type_range_registers_range_holds(const MemoryTypeRangeRegistersRange_t *range, uint64_t page)
{
    const uint64_t mask = range->mask & MEMORY_TYPE_RANGE_REGISTERS_ADDRESS_BITS;

    return (range->mask & MEMORY_TYPE_RANGE_REGISTERS_RANGE_VALID) != 0u && ((page ^ range->base) & mask) == 0u;
}

bool memory_type_range_registers_make_write_back(const MemoryTypeRangeRegisters_t *registers, uint64_t page)
{
    if ((registers->default_type & MEMORY_TYPE_RANGE_REGISTERS_ENABLED) == 0u ||
        registers->range_count > MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY)
        return false;
    if (page < MEMORY_TYPE_RANGE_REGISTERS_FIXED_END &&
        (registers->default_type & MEMORY_TYPE_RANGE_REGISTERS_FIXED_ENABLED) != 0u)
        return false;

    bool held = false;

    for (uint32_t index = 0u; index < registers->range_count; ++index)
    {
        const MemoryTypeRangeRegistersRange_t *const range = &registers->ranges[index];

        if (!memory_type_range_registers_range_holds(range, page))
            continue;
        if ((range->base & MEMORY_TYPE_RANGE_REGISTERS_TYPE_MASK) != MEMORY_TYPE_RANGE_REGISTERS_WRITE_BACK)
            return false;
        held = true;
    }
    return held ||
           (registers->default_type & MEMORY_TYPE_RANGE_REGISTERS_TYPE_MASK) == MEMORY_TYPE_RANGE_REGISTERS_WRITE_BACK;
}

void memory_type_range_registers_initialize(void)
{
    MemoryTypeRangeRegisters_t *const registers = &memory_type_range_registers_boot;
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    registers->default_type = 0u;
    registers->range_count = 0u;
    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    if ((edx & MEMORY_TYPE_RANGE_REGISTERS_CPUID_FLAG) == 0u)
        return;

    const uint32_t variable_count =
        (uint32_t) (asmutils_read_model_specific_register(MEMORY_TYPE_RANGE_REGISTERS_CAPABILITY) &
                    MEMORY_TYPE_RANGE_REGISTERS_VARIABLE_COUNT);

    for (uint32_t index = 0u; index < variable_count; ++index)
    {
        const uint32_t base_register = MEMORY_TYPE_RANGE_REGISTERS_FIRST_BASE + 2u * index;
        const uint64_t mask = asmutils_read_model_specific_register(base_register + 1u);

        if ((mask & MEMORY_TYPE_RANGE_REGISTERS_RANGE_VALID) == 0u)
            continue;
        if (registers->range_count < MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY)
            registers->ranges[registers->range_count] =
                (MemoryTypeRangeRegistersRange_t){asmutils_read_model_specific_register(base_register), mask};
        ++registers->range_count;
    }
    registers->default_type = asmutils_read_model_specific_register(MEMORY_TYPE_RANGE_REGISTERS_DEFAULT_TYPE);
}

bool memory_type_range_registers_frame_is_write_back(uint32_t frame)
{
    return memory_type_range_registers_make_write_back(&memory_type_range_registers_boot, frame);
}
