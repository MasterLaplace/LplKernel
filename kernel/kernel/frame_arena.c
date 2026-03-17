#define __LPL_KERNEL__

#include <kernel/mm/frame_arena.h>
#include <kernel/mm/heap.h>

static uint8_t *kernel_frame_arena_base = NULL;
static uint32_t kernel_frame_arena_capacity = 0u;
static uint32_t kernel_frame_arena_offset = 0u;
static uint32_t kernel_frame_arena_peak = 0u;
static uint32_t kernel_frame_arena_reset_count = 0u;
static uint32_t kernel_frame_arena_failed_alloc_count = 0u;
static uint32_t kernel_frame_arena_budget = 0u;
static uint32_t kernel_frame_arena_budget_exceeded_count = 0u;
static uint32_t kernel_frame_arena_wcet_alloc = 0u;
static uint32_t kernel_frame_arena_wcet_reset = 0u;
static bool kernel_frame_arena_initialized = false;

static inline uint32_t frame_arena_rdtsc_low(void)
{
#if defined(__i386__) || defined(__x86_64__)
    uint32_t lo;
    asm volatile("rdtsc" : "=a"(lo) :: "edx");
    return lo;
#else
    return 0u;
#endif
}

static uint32_t kernel_frame_arena_align_up(uint32_t value, uint32_t align)
{
    if (align == 0u)
        return value;
    return (value + (align - 1u)) & ~(align - 1u);
}

bool kernel_frame_arena_initialize(uint32_t capacity_bytes)
{
    if (kernel_frame_arena_initialized)
        return true;

    if (capacity_bytes == 0u)
        return false;

    uint32_t aligned_capacity = kernel_frame_arena_align_up(capacity_bytes, 16u);
    void *backing = kmalloc((size_t) aligned_capacity);

    if (!backing)
        return false;

    kernel_frame_arena_base = (uint8_t *) backing;
    kernel_frame_arena_capacity = aligned_capacity;
    kernel_frame_arena_offset = 0u;
    kernel_frame_arena_peak = 0u;
    kernel_frame_arena_reset_count = 0u;
    kernel_frame_arena_failed_alloc_count = 0u;
    kernel_frame_arena_budget = 0u;
    kernel_frame_arena_budget_exceeded_count = 0u;
    kernel_frame_arena_wcet_alloc = 0u;
    kernel_frame_arena_wcet_reset = 0u;
    kernel_frame_arena_initialized = true;

#ifdef LPL_KERNEL_DEBUG_POISON
    for (uint32_t i = 0u; i < kernel_frame_arena_capacity; ++i)
        kernel_frame_arena_base[i] = 0xAA;
#endif

    return true;
}

void *kernel_frame_arena_alloc(uint32_t size, uint32_t align)
{
    uint32_t t0 = frame_arena_rdtsc_low();

    if (!kernel_frame_arena_initialized || size == 0u)
        return NULL;

    uint32_t effective_align = (align == 0u) ? 8u : align;
    uint32_t aligned_offset = kernel_frame_arena_align_up(kernel_frame_arena_offset, effective_align);

    if (aligned_offset > kernel_frame_arena_capacity || size > (kernel_frame_arena_capacity - aligned_offset))
    {
        ++kernel_frame_arena_failed_alloc_count;
        return NULL;
    }

    uint32_t real_size = size;
#ifdef LPL_KERNEL_DEBUG_POISON
    real_size += sizeof(uint32_t);
#endif

    if (kernel_frame_arena_budget > 0u && (aligned_offset + real_size) > kernel_frame_arena_budget)
    {
        ++kernel_frame_arena_budget_exceeded_count;
        ++kernel_frame_arena_failed_alloc_count;
        return NULL;
    }

    void *result = kernel_frame_arena_base + aligned_offset;

#ifdef LPL_KERNEL_DEBUG_POISON
    *((uint32_t *) ((uint8_t *) result + size)) = 0xFBADBEEFu;
#endif

    kernel_frame_arena_offset = aligned_offset + real_size;
    if (kernel_frame_arena_offset > kernel_frame_arena_peak)
        kernel_frame_arena_peak = kernel_frame_arena_offset;

    uint32_t t1 = frame_arena_rdtsc_low();
    uint32_t delta = t1 - t0;
    if (delta > kernel_frame_arena_wcet_alloc)
        kernel_frame_arena_wcet_alloc = delta;

    return result;
}

void kernel_frame_arena_reset(void)
{
    uint32_t t0 = frame_arena_rdtsc_low();

    if (!kernel_frame_arena_initialized)
        return;

#ifdef LPL_KERNEL_DEBUG_POISON
    for (uint32_t i = 0u; i < kernel_frame_arena_offset; ++i)
        kernel_frame_arena_base[i] = 0xAA;
#endif

    kernel_frame_arena_offset = 0u;
    ++kernel_frame_arena_reset_count;

    uint32_t t1 = frame_arena_rdtsc_low();
    uint32_t delta = t1 - t0;
    if (delta > kernel_frame_arena_wcet_reset)
        kernel_frame_arena_wcet_reset = delta;
}

bool kernel_frame_arena_is_initialized(void) { return kernel_frame_arena_initialized; }

uint32_t kernel_frame_arena_get_capacity_bytes(void) { return kernel_frame_arena_capacity; }

uint32_t kernel_frame_arena_get_used_bytes(void) { return kernel_frame_arena_offset; }

uint32_t kernel_frame_arena_get_peak_used_bytes(void) { return kernel_frame_arena_peak; }

uint32_t kernel_frame_arena_get_reset_count(void) { return kernel_frame_arena_reset_count; }

uint32_t kernel_frame_arena_get_failed_alloc_count(void) { return kernel_frame_arena_failed_alloc_count; }

void kernel_frame_arena_set_frame_budget(uint32_t budget_bytes) { kernel_frame_arena_budget = budget_bytes; }

uint32_t kernel_frame_arena_get_budget_exceeded_count(void) { return kernel_frame_arena_budget_exceeded_count; }

uint32_t kernel_frame_arena_get_wcet_alloc_cycles(void) { return kernel_frame_arena_wcet_alloc; }

uint32_t kernel_frame_arena_get_wcet_reset_cycles(void) { return kernel_frame_arena_wcet_reset; }
