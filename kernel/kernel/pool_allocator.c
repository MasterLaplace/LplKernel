#define __LPL_KERNEL__

#include <kernel/mm/pool_allocator.h>
#include <kernel/mm/heap.h>
#include <stddef.h>

static uint8_t *kernel_pool_base = NULL;
static void *kernel_pool_free_head = NULL;
static uint32_t kernel_pool_slot_size = 0u;
static uint32_t kernel_pool_object_size = 0u;
static uint32_t kernel_pool_capacity = 0u;
static uint32_t kernel_pool_free_count = 0u;
static uint32_t kernel_pool_peak_used = 0u;
static uint32_t kernel_pool_failed_alloc_count = 0u;
static uint32_t kernel_pool_wcet_alloc = 0u;
static uint32_t kernel_pool_wcet_free = 0u;
static bool kernel_pool_initialized = false;

static inline uint32_t pool_allocator_rdtsc_low(void)
{
#if defined(__i386__) || defined(__x86_64__)
    uint32_t lo;
    asm volatile("rdtsc" : "=a"(lo) :: "edx");
    return lo;
#else
    return 0u;
#endif
}

static uint32_t kernel_pool_align_up(uint32_t value, uint32_t align)
{
    if (align == 0u)
        return value;
    return (value + (align - 1u)) & ~(align - 1u);
}

bool kernel_pool_allocator_initialize(uint32_t object_size, uint32_t object_count)
{
    if (kernel_pool_initialized)
        return true;

    if (object_size == 0u || object_count == 0u)
        return false;

    kernel_pool_object_size = object_size;
    uint32_t min_slot = (uint32_t) sizeof(void *) + (uint32_t) sizeof(uint32_t);
    kernel_pool_slot_size = kernel_pool_align_up(object_size, (uint32_t) sizeof(void *));
    if (kernel_pool_slot_size < min_slot)
        kernel_pool_slot_size = kernel_pool_align_up(min_slot, (uint32_t) sizeof(void *));

    uint32_t total_bytes = kernel_pool_slot_size * object_count;
    void *backing = kmalloc((size_t) total_bytes);

    if (!backing)
        return false;

    kernel_pool_base = (uint8_t *) backing;
    kernel_pool_capacity = object_count;
    kernel_pool_free_count = object_count;
    kernel_pool_peak_used = 0u;
    kernel_pool_failed_alloc_count = 0u;
    kernel_pool_wcet_alloc = 0u;
    kernel_pool_wcet_free = 0u;
    kernel_pool_free_head = NULL;

    for (uint32_t index = 0u; index < object_count; ++index)
    {
        uint8_t *slot = kernel_pool_base + (index * kernel_pool_slot_size);
        *((void **) slot) = kernel_pool_free_head;
        *((uint32_t *) (slot + sizeof(void *))) = 0x504F4F4Cu; /* 'POOL' */
        kernel_pool_free_head = slot;
    }

    kernel_pool_initialized = true;
    return true;
}

void *kernel_pool_alloc(void)
{
    uint32_t t0 = pool_allocator_rdtsc_low();

    if (!kernel_pool_initialized || !kernel_pool_free_head)
    {
        if (kernel_pool_initialized)
            ++kernel_pool_failed_alloc_count;
        return NULL;
    }

    void *slot = kernel_pool_free_head;
    kernel_pool_free_head = *((void **) slot);
    *((uint32_t *) ((uint8_t *) slot + sizeof(void *))) = 0u;
    --kernel_pool_free_count;

    uint32_t used_count = kernel_pool_capacity - kernel_pool_free_count;
    if (used_count > kernel_pool_peak_used)
        kernel_pool_peak_used = used_count;

    uint32_t t1 = pool_allocator_rdtsc_low();
    uint32_t delta = t1 - t0;
    if (delta > kernel_pool_wcet_alloc)
        kernel_pool_wcet_alloc = delta;

    return slot;
}

bool kernel_pool_free(void *ptr)
{
    uint32_t t0 = pool_allocator_rdtsc_low();

    if (!kernel_pool_initialized || !ptr)
        return false;

    uintptr_t ptr_addr = (uintptr_t) ptr;
    uintptr_t base_addr = (uintptr_t) kernel_pool_base;
    uintptr_t end_addr = base_addr + (kernel_pool_slot_size * kernel_pool_capacity);

    if (ptr_addr < base_addr || ptr_addr >= end_addr)
        return false;

    if (((uint32_t) (ptr_addr - base_addr) % kernel_pool_slot_size) != 0u)
        return false;

    uint32_t *cookie_word = (uint32_t *) ((uint8_t *) ptr + sizeof(void *));
    if (*cookie_word == 0x504F4F4Cu)
        return false;

#ifdef LPL_KERNEL_DEBUG_POISON
    /* Leave room for the free_head pointer, poison the rest. */
    if (kernel_pool_object_size > sizeof(void *))
    {
        uint8_t *poison_start = (uint8_t *) ptr + sizeof(void *);
        uint32_t poison_size = kernel_pool_object_size - (uint32_t) sizeof(void *);
        for (uint32_t i = 0u; i < poison_size; ++i)
            poison_start[i] = 0xDF;
    }
#endif

    *((void **) ptr) = kernel_pool_free_head;
    *cookie_word = 0x504F4F4Cu;
    kernel_pool_free_head = ptr;
    if (kernel_pool_free_count < kernel_pool_capacity)
        ++kernel_pool_free_count;

    uint32_t t1 = pool_allocator_rdtsc_low();
    uint32_t delta = t1 - t0;
    if (delta > kernel_pool_wcet_free)
        kernel_pool_wcet_free = delta;

    return true;
}

bool kernel_pool_allocator_is_initialized(void) { return kernel_pool_initialized; }

uint32_t kernel_pool_get_object_size(void) { return kernel_pool_object_size; }

uint32_t kernel_pool_get_capacity(void) { return kernel_pool_capacity; }

uint32_t kernel_pool_get_free_count(void) { return kernel_pool_free_count; }

uint32_t kernel_pool_get_peak_used_count(void) { return kernel_pool_peak_used; }

uint32_t kernel_pool_get_failed_alloc_count(void) { return kernel_pool_failed_alloc_count; }

uint32_t kernel_pool_get_wcet_alloc_cycles(void) { return kernel_pool_wcet_alloc; }

uint32_t kernel_pool_get_wcet_free_cycles(void) { return kernel_pool_wcet_free; }
