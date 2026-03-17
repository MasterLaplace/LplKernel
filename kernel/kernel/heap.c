#define __LPL_KERNEL__

#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/slab.h>
#include <kernel/mm/tlsf.h>
#include <stdbool.h>
#include <stdint.h>

#define KERNEL_HEAP_ALIGNMENT       8u
#define KERNEL_HEAP_BLOCK_FLAG_FREE 0x01u
#define KERNEL_HEAP_BLOCK_FLAG_BIG  0x02u
#define KERNEL_HEAP_BLOCK_FLAG_SC   0x04u  /* server size-class bucket block */
#define KERNEL_HEAP_BLOCK_FLAG_VMM  0x08u
#define KERNEL_HEAP_MIN_SPLIT       32u
#define KERNEL_HEAP_MAX_ORDER       18u
#define KERNEL_HEAP_HEADER_MAGIC    0x4C50u

#ifdef LPL_KERNEL_REAL_TIME_MODE
/* Total pages pre-allocated in the boot pool for first-fit fallback. */
#define KERNEL_HEAP_CLIENT_BOOT_POOL_PAGES 8u
/*
 * Pages donated to the slab sub-system (taken from the boot pool).
 * KERNEL_SLAB_CACHE_MAX_PAGES pages per cache × 3 caches.
 */
#define KERNEL_HEAP_CLIENT_SLAB_PAGES (KERNEL_SLAB_CACHE_MAX_PAGES * 3u)

/* TLSF deterministic O(1) heap pool size (4 MB). */
#define KERNEL_HEAP_CLIENT_TLSF_POOL_SIZE (4u * 1024u * 1024u)
static uint8_t kernel_heap_client_tlsf_pool[KERNEL_HEAP_CLIENT_TLSF_POOL_SIZE] __attribute__((aligned(8)));
#else
/* Server size-class fast-path: power-of-two sizes 8 … 512 B. */
#define KERNEL_HEAP_SIZE_CLASSES      7u
/*
 * Logical domain count for server allocator staging.
 * With current single-core bring-up, selection defaults to domain 0.
 * Phase 4 SMP work will wire this selector to per-CPU topology.
 */
#define KERNEL_HEAP_SERVER_DOMAINS    2u
#endif

static KernelHeapBlock_t *kernel_heap_free_list = NULL;
static uint32_t kernel_heap_small_free_blocks = 0u;
static uint32_t kernel_heap_small_free_bytes = 0u;
static uint32_t kernel_heap_large_allocation_count = 0u;
static uint32_t kernel_heap_rejected_free_count = 0u;
static uint32_t kernel_heap_double_free_count = 0u;
static bool kernel_heap_initialized = false;

#ifdef LPL_KERNEL_REAL_TIME_MODE
/*
 * Client-only deterministic rule guard:
 * kmalloc/kfree are forbidden in the hot loop.
 */
static uint32_t kernel_heap_hot_loop_depth = 0u;
static uint32_t kernel_heap_hot_loop_violation_count = 0u;
#endif

#ifndef LPL_KERNEL_REAL_TIME_MODE
typedef struct KernelHeapServerDomain {
    KernelHeapBlock_t *size_class_lists[KERNEL_HEAP_SIZE_CLASSES];
    uint32_t size_class_free_counts[KERNEL_HEAP_SIZE_CLASSES];
    uint32_t size_class_hit_counts[KERNEL_HEAP_SIZE_CLASSES];
    uint32_t size_class_refill_counts[KERNEL_HEAP_SIZE_CLASSES];
    uint32_t first_fit_fallback_count;
    uint32_t remote_probe_count;
    uint32_t remote_hit_count;
} KernelHeapServerDomain_t;

/*
 * Server size-class buckets.
 * Slot i holds objects whose usable payload fits in (8 << i) bytes:
 *   i=0 →  8 B, i=1 → 16 B, i=2 → 32 B, i=3 → 64 B,
 *   i=4 → 128 B, i=5 → 256 B, i=6 → 512 B
 * Each bucket is a singly-linked free-list using the KernelHeapBlock next
 * pointer.  When a bucket is empty, kmalloc falls through to first-fit.
 */
static uint32_t kernel_heap_size_class_sizes[KERNEL_HEAP_SIZE_CLASSES] = {
    8u, 16u, 32u, 64u, 128u, 256u, 512u
};
static KernelHeapServerDomain_t kernel_heap_server_domains[KERNEL_HEAP_SERVER_DOMAINS];
static uint32_t kernel_heap_server_active_domain = 0u;
static uint8_t kernel_heap_server_slot_domain_override_enabled[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];
static uint32_t kernel_heap_server_slot_domain_override[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];
#endif

#ifdef LPL_KERNEL_REAL_TIME_MODE
static const char kernel_heap_strategy_name[] = "slab+tlsf-client";
#else
static const char kernel_heap_strategy_name[] = "sizeclass+first-fit-server";
#endif

static uint32_t kernel_heap_align_up(uint32_t value, uint32_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

static uint32_t kernel_heap_virt_to_phys(const void *virt_ptr)
{
    return (uint32_t) (uintptr_t) virt_ptr - KERNEL_VIRTUAL_BASE;
}

static void *kernel_heap_phys_to_virt(uint32_t phys_addr)
{
    return (void *) (uintptr_t) (phys_addr + KERNEL_VIRTUAL_BASE);
}

#ifndef LPL_KERNEL_REAL_TIME_MODE
static KernelHeapServerDomain_t *kernel_heap_server_get_domain(uint32_t domain_index)
{
    if (domain_index >= KERNEL_HEAP_SERVER_DOMAINS)
        return NULL;

    return &kernel_heap_server_domains[domain_index];
}

static uint32_t kernel_heap_server_get_local_domain_index(void)
{
    uint32_t logical_slot = cpu_topology_get_logical_slot();
    if (logical_slot >= KERNEL_HEAP_SERVER_DOMAINS)
        logical_slot = 0u;

    kernel_heap_server_active_domain = logical_slot;
    return logical_slot;
}

static KernelHeapBlock_t *kernel_heap_server_try_pop_remote_bucket(uint32_t local_domain_index,
                                                                    uint32_t size_class_index,
                                                                    uint32_t *owner_domain_index)
{
    if (!owner_domain_index || size_class_index >= KERNEL_HEAP_SIZE_CLASSES)
        return NULL;

    for (uint32_t domain_index = 0u; domain_index < KERNEL_HEAP_SERVER_DOMAINS; ++domain_index)
    {
        if (domain_index == local_domain_index)
            continue;

        KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(domain_index);

        if (!domain || !domain->size_class_lists[size_class_index])
            continue;

        KernelHeapBlock_t *block = domain->size_class_lists[size_class_index];

        domain->size_class_lists[size_class_index] = block->next;
        --domain->size_class_free_counts[size_class_index];
        *owner_domain_index = domain_index;
        return block;
    }

    return NULL;
}

static uint8_t kernel_heap_required_order(uint32_t total_size)
{
    uint32_t pages = (total_size + (PAGE_SIZE - 1u)) / PAGE_SIZE;
    uint32_t block_pages = 1u;
    uint8_t order = 0u;

    while (block_pages < pages && order < KERNEL_HEAP_MAX_ORDER)
    {
        block_pages <<= 1u;
        ++order;
    }

    if (block_pages < pages)
        return 0xFFu;

    return order;
}
#endif

static void kernel_heap_add_free_block(KernelHeapBlock_t *block)
{
    block->magic = KERNEL_HEAP_HEADER_MAGIC;
    block->flags = KERNEL_HEAP_BLOCK_FLAG_FREE;
    block->order = 0u;

    if (!kernel_heap_free_list || block < kernel_heap_free_list)
    {
        block->next = kernel_heap_free_list;
        kernel_heap_free_list = block;
    }
    else
    {
        KernelHeapBlock_t *current = kernel_heap_free_list;

        while (current->next && current->next < block)
            current = current->next;

        block->next = current->next;
        current->next = block;
    }

    ++kernel_heap_small_free_blocks;
    kernel_heap_small_free_bytes += block->size;
}

static bool kernel_heap_merge_adjacent_blocks(KernelHeapBlock_t *left)
{
    if (!left || !left->next)
        return false;

    uint32_t left_end = (uint32_t) (uintptr_t) left + left->size;
    if (left_end != (uint32_t) (uintptr_t) left->next)
        return false;

    left->size += left->next->size;
    left->next = left->next->next;

    --kernel_heap_small_free_blocks;
    return true;
}

static void kernel_heap_coalesce_around(KernelHeapBlock_t *block)
{
    if (!block)
        return;

    while (kernel_heap_merge_adjacent_blocks(block))
    {
    }

    KernelHeapBlock_t *prev = NULL;
    KernelHeapBlock_t *current = kernel_heap_free_list;

    while (current && current != block)
    {
        prev = current;
        current = current->next;
    }

    if (!prev)
        return;

    while (kernel_heap_merge_adjacent_blocks(prev))
    {
    }
}

static bool kernel_heap_grow_small_pool(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    return false;
#else
    uint32_t page_phys = physical_memory_manager_page_frame_allocate();

    if (!page_phys)
        return false;

    KernelHeapBlock_t *block = (KernelHeapBlock_t *) kernel_heap_phys_to_virt(page_phys);
    block->size = PAGE_SIZE;
    block->magic = KERNEL_HEAP_HEADER_MAGIC;
    block->next = NULL;
    block->reserved = 0u;
    kernel_heap_add_free_block(block);
    return true;
#endif
}

#ifdef LPL_KERNEL_REAL_TIME_MODE
static void kernel_heap_build_client_boot_pool(void)
{
    /*
     * The first KERNEL_HEAP_CLIENT_SLAB_PAGES pages are handed to
     * the slab allocator; the remaining pages feed the first-fit list.
     */
    void *slab_backing[KERNEL_HEAP_CLIENT_SLAB_PAGES];
    uint32_t slab_donated = 0u;

    for (uint32_t page_index = 0u; page_index < KERNEL_HEAP_CLIENT_BOOT_POOL_PAGES; ++page_index)
    {
        uint32_t page_phys = physical_memory_manager_page_frame_allocate();

        if (!page_phys)
            break;

        void *page_virt = kernel_heap_phys_to_virt(page_phys);

        if (slab_donated < KERNEL_HEAP_CLIENT_SLAB_PAGES)
        {
            slab_backing[slab_donated++] = page_virt;
        }
        else
        {
            KernelHeapBlock_t *block = (KernelHeapBlock_t *) page_virt;
            block->size = PAGE_SIZE;
            block->magic = KERNEL_HEAP_HEADER_MAGIC;
            block->next = NULL;
            block->reserved = 0u;
            kernel_heap_add_free_block(block);
        }
    }

    if (slab_donated > 0u)
        kernel_slab_initialize(slab_backing, slab_donated);

    kernel_tlsf_initialize(kernel_heap_client_tlsf_pool, KERNEL_HEAP_CLIENT_TLSF_POOL_SIZE);
}
#else
static void kernel_heap_server_size_class_initialize(void)
{
    kernel_heap_server_active_domain = 0u;

    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        kernel_heap_server_slot_domain_override_enabled[slot] = 0u;
        kernel_heap_server_slot_domain_override[slot] = 0u;
    }

    for (uint32_t domain_index = 0u; domain_index < KERNEL_HEAP_SERVER_DOMAINS; ++domain_index)
    {
        KernelHeapServerDomain_t *domain = &kernel_heap_server_domains[domain_index];

        domain->first_fit_fallback_count = 0u;
        domain->remote_probe_count = 0u;
        domain->remote_hit_count = 0u;
        for (uint32_t i = 0u; i < KERNEL_HEAP_SIZE_CLASSES; ++i)
        {
            domain->size_class_lists[i] = NULL;
            domain->size_class_free_counts[i] = 0u;
            domain->size_class_hit_counts[i] = 0u;
            domain->size_class_refill_counts[i] = 0u;
        }
    }
}
#endif

#ifndef LPL_KERNEL_REAL_TIME_MODE
void kernel_heap_initialize_ap_domain(uint32_t logical_slot)
{
    (void) logical_slot;

    /* Pre-populate the AP domain with a few blocks from the global first-fit pool.
       Since this is called by the AP itself during startup, kmalloc() will automatically
       resolve to the AP's local domain and kfree() will stash the blocks there. */
    for (uint32_t sc = 0u; sc < KERNEL_HEAP_SIZE_CLASSES; ++sc)
    {
        uint32_t payload_size = kernel_heap_size_class_sizes[sc];
        void *ptrs[4];
        for (uint32_t i = 0u; i < 4u; ++i)
            ptrs[i] = kmalloc(payload_size);
        for (uint32_t i = 0u; i < 4u; ++i)
            if (ptrs[i])
                kfree(ptrs[i]);
    }
}
#else
void kernel_heap_initialize_ap_domain(uint32_t logical_slot)
{
    (void) logical_slot;
}
#endif

void kernel_heap_initialize(void)
{
    kernel_heap_free_list = NULL;
    kernel_heap_small_free_blocks = 0u;
    kernel_heap_small_free_bytes = 0u;
    kernel_heap_large_allocation_count = 0u;
    kernel_heap_rejected_free_count = 0u;
    kernel_heap_double_free_count = 0u;
    kernel_heap_initialized = true;

#ifdef LPL_KERNEL_REAL_TIME_MODE
    kernel_heap_hot_loop_depth = 0u;
    kernel_heap_hot_loop_violation_count = 0u;
#endif

#ifdef LPL_KERNEL_REAL_TIME_MODE
    kernel_heap_build_client_boot_pool();
#else
    kernel_heap_server_size_class_initialize();
#endif
}

void *kmalloc(size_t size)
{
    if (!kernel_heap_initialized || size == 0u)
        return NULL;

#ifdef LPL_KERNEL_REAL_TIME_MODE
    if (kernel_heap_hot_loop_depth > 0u)
    {
        ++kernel_heap_hot_loop_violation_count;
        return NULL;
    }
#endif

    uint32_t payload_size = kernel_heap_align_up((uint32_t) size, KERNEL_HEAP_ALIGNMENT);
    uint32_t total_size =
        kernel_heap_align_up(payload_size + (uint32_t) sizeof(KernelHeapBlock_t), KERNEL_HEAP_ALIGNMENT);

#ifdef LPL_KERNEL_REAL_TIME_MODE
    /*
     * Client fast-path: try the slab for exact cache sizes.
     * Slab objects carry no KernelHeapBlock_t header — the pointer
     * returned is the raw object.  kfree() detects this via
     * kernel_slab_free() ownership check.
     */
    {
        void *slab_obj = kernel_slab_alloc(payload_size);
        if (slab_obj)
            return slab_obj;
    }

    /* Client O(1) deterministic heap: TLSF. */
    void *tlsf_obj = kernel_tlsf_alloc(payload_size);
    if (tlsf_obj)
        return tlsf_obj;
#else
    /*
     * Server fast-path: current mono-domain scaffold.
     * Today every CPU resolves to domain 0; later per-CPU/per-NUMA work
     * can swap only the selector while preserving the bucket contract.
     */
    uint32_t local_domain_index = kernel_heap_server_get_local_domain_index();
    KernelHeapServerDomain_t *local_domain = kernel_heap_server_get_domain(local_domain_index);
    uint32_t matched_sc = 0xFFFFFFFFu;
    uint32_t batch_count = 1u;

    if (payload_size <= kernel_heap_size_class_sizes[KERNEL_HEAP_SIZE_CLASSES - 1u])
    {
        for (uint32_t sc = 0u; sc < KERNEL_HEAP_SIZE_CLASSES; ++sc)
        {
            if (payload_size > kernel_heap_size_class_sizes[sc])
                continue;

            matched_sc = sc;
            KernelHeapBlock_t *block = NULL;
            uint32_t owner_domain_index = local_domain_index;

            uint32_t eflags;
            __asm__ volatile("pushf\n\tpop %0\n\tcli" : "=r"(eflags) :: "memory");

            if (local_domain && local_domain->size_class_lists[sc])
            {
                block = local_domain->size_class_lists[sc];
                local_domain->size_class_lists[sc] = block->next;
                --local_domain->size_class_free_counts[sc];
            }

            __asm__ volatile("push %0\n\tpopf" :: "r"(eflags) : "memory", "cc");

            if (!block && local_domain)
            {
                ++local_domain->remote_probe_count;
                __asm__ volatile("pushf\n\tpop %0\n\tcli" : "=r"(eflags) :: "memory");
                block = kernel_heap_server_try_pop_remote_bucket(local_domain_index, sc, &owner_domain_index);
                __asm__ volatile("push %0\n\tpopf" :: "r"(eflags) : "memory", "cc");
                if (block)
                    ++local_domain->remote_hit_count;
            }

            if (block)
            {
                if (local_domain)
                    ++local_domain->size_class_hit_counts[sc];

                block->flags = KERNEL_HEAP_BLOCK_FLAG_SC;
                block->order = (uint8_t) sc;
                block->reserved = (uint16_t) owner_domain_index;
                block->next = NULL;
                return (void *) (block + 1);
            }

            if (local_domain)
                ++local_domain->first_fit_fallback_count;
                
            batch_count = 4u; /* Batch size for refill */
            total_size = kernel_heap_align_up(kernel_heap_size_class_sizes[matched_sc] + (uint32_t) sizeof(KernelHeapBlock_t), KERNEL_HEAP_ALIGNMENT);
            total_size *= batch_count;
            if (local_domain)
                ++local_domain->size_class_refill_counts[matched_sc];
            break;
        }
    }

    if (total_size > PAGE_SIZE)
    {
        uint32_t page_count = (total_size + PAGE_SIZE - 1u) / PAGE_SIZE;
        KernelHeapBlock_t *header = (KernelHeapBlock_t *) kernel_vmm_alloc_pages(page_count);

        if (!header)
            return NULL;

        header->size = page_count * PAGE_SIZE;
        header->magic = KERNEL_HEAP_HEADER_MAGIC;
        header->flags = KERNEL_HEAP_BLOCK_FLAG_VMM;
        header->order = 0u;
        header->reserved = (uint16_t) page_count;
        header->next = NULL;

        ++kernel_heap_large_allocation_count;
        return (void *) (header + 1);
    }
#endif

#ifdef LPL_KERNEL_REAL_TIME_MODE
    if (total_size > PAGE_SIZE)
        return NULL;
#endif

    /* First-fit path. */
    KernelHeapBlock_t *prev = NULL;
    KernelHeapBlock_t *current = kernel_heap_free_list;

    while (true)
    {
        while (current)
        {
            if (current->size >= total_size)
                break;
            prev = current;
            current = current->next;
        }

        if (current)
            break;

        if (!kernel_heap_grow_small_pool())
            return NULL;

        prev = NULL;
        current = kernel_heap_free_list;
    }

    if (prev)
        prev->next = current->next;
    else
        kernel_heap_free_list = current->next;

    --kernel_heap_small_free_blocks;
    kernel_heap_small_free_bytes -= current->size;

    uint32_t remaining_size = current->size - total_size;

    if (remaining_size >= KERNEL_HEAP_MIN_SPLIT)
    {
        KernelHeapBlock_t *tail = (KernelHeapBlock_t *) ((uint8_t *) current + total_size);

        tail->size = remaining_size;
        tail->magic = KERNEL_HEAP_HEADER_MAGIC;
        tail->next = NULL;
        tail->reserved = 0u;
        kernel_heap_add_free_block(tail);
        current->size = total_size;
    }

    current->magic = KERNEL_HEAP_HEADER_MAGIC;
    current->flags = 0u;
    current->order = 0u;
    current->next = NULL;

#ifndef LPL_KERNEL_REAL_TIME_MODE
    if (matched_sc != 0xFFFFFFFFu && batch_count > 1u)
    {
         uint32_t sc_full_size = kernel_heap_align_up(kernel_heap_size_class_sizes[matched_sc] + (uint32_t) sizeof(KernelHeapBlock_t), KERNEL_HEAP_ALIGNMENT);

         uint32_t eflags;
         __asm__ volatile("pushf\n\tpop %0\n\tcli" : "=r"(eflags) :: "memory");

         for (uint32_t i = 1u; i < batch_count; ++i)
         {
             if (current->size < sc_full_size * (i + 1u))
                 break;

             KernelHeapBlock_t *piece = (KernelHeapBlock_t *) ((uint8_t *) current + (i * sc_full_size));
             piece->size = sc_full_size;
             piece->flags = KERNEL_HEAP_BLOCK_FLAG_SC | KERNEL_HEAP_BLOCK_FLAG_FREE;
             piece->order = (uint8_t) matched_sc;
             piece->reserved = (uint16_t) local_domain_index;
             piece->magic = KERNEL_HEAP_HEADER_MAGIC;
             
             if (local_domain)
             {
                 piece->next = local_domain->size_class_lists[matched_sc];
                 local_domain->size_class_lists[matched_sc] = piece;
                 ++local_domain->size_class_free_counts[matched_sc];
             }
         }
         __asm__ volatile("push %0\n\tpopf" :: "r"(eflags) : "memory", "cc");

         current->size = sc_full_size; 
         current->flags = KERNEL_HEAP_BLOCK_FLAG_SC;
         current->order = (uint8_t) matched_sc;
         current->reserved = (uint16_t) local_domain_index;
    }
    else
    {
        for (uint32_t sc = 0u; sc < KERNEL_HEAP_SIZE_CLASSES; ++sc)
        {
            if (payload_size == kernel_heap_size_class_sizes[sc])
            {
                current->flags = KERNEL_HEAP_BLOCK_FLAG_SC;
                current->order = (uint8_t) sc;
                current->reserved = (uint16_t) local_domain_index;
                break;
            }
        }
    }
#endif

    return (void *) (current + 1);
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

#ifdef LPL_KERNEL_REAL_TIME_MODE
    if (kernel_heap_hot_loop_depth > 0u)
        ++kernel_heap_hot_loop_violation_count;
#endif

#ifdef LPL_KERNEL_REAL_TIME_MODE
    /* Client: check slab ownership first (no header on slab objects). */
    if (kernel_slab_free(ptr))
        return;

    /* Client: check TLSF ownership. */
    if (kernel_tlsf_owns(ptr))
    {
        kernel_tlsf_free(ptr);
        return;
    }
#endif

    KernelHeapBlock_t *header = ((KernelHeapBlock_t *) ptr) - 1;

    if (header->magic != KERNEL_HEAP_HEADER_MAGIC)
    {
        ++kernel_heap_rejected_free_count;
        return;
    }

    if (header->flags & KERNEL_HEAP_BLOCK_FLAG_FREE)
    {
        ++kernel_heap_rejected_free_count;
        ++kernel_heap_double_free_count;
        return;
    }

    if (header->flags & KERNEL_HEAP_BLOCK_FLAG_BIG)
    {
        uint32_t block_phys = kernel_heap_virt_to_phys(header);

        if (kernel_heap_large_allocation_count > 0u)
            --kernel_heap_large_allocation_count;

#ifndef LPL_KERNEL_REAL_TIME_MODE
        physical_memory_manager_page_frame_free_order(block_phys, header->order);
#else
        physical_memory_manager_page_frame_free(block_phys);
#endif
        return;
    }

    if (header->flags & KERNEL_HEAP_BLOCK_FLAG_VMM)
    {
        uint32_t page_count = header->reserved;
        if (kernel_heap_large_allocation_count > 0u)
            --kernel_heap_large_allocation_count;
        kernel_vmm_free_pages(header, page_count);
        return;
    }

#ifndef LPL_KERNEL_REAL_TIME_MODE
    /*
     * Server: if this block is size-class tagged, push it back into
     * its bucket using the stored class index (header->order).
     * This covers both SC-fast-path allocations and first-fit
     * allocations that were tagged at alloc time.
     */
    if (header->flags & KERNEL_HEAP_BLOCK_FLAG_SC)
    {
        uint8_t sc = header->order;
        uint16_t owner_domain = header->reserved;

        if (sc < KERNEL_HEAP_SIZE_CLASSES && owner_domain < KERNEL_HEAP_SERVER_DOMAINS)
        {
            KernelHeapServerDomain_t *domain = &kernel_heap_server_domains[owner_domain];

            uint32_t eflags;
            __asm__ volatile("pushf\n\tpop %0\n\tcli" : "=r"(eflags) :: "memory");

            header->flags |= KERNEL_HEAP_BLOCK_FLAG_FREE;
            header->next = domain->size_class_lists[sc];
            domain->size_class_lists[sc] = header;
            ++domain->size_class_free_counts[sc];

            __asm__ volatile("push %0\n\tpopf" :: "r"(eflags) : "memory", "cc");
            return;
        }
    }
#endif

    header->next = NULL;
    kernel_heap_add_free_block(header);
    kernel_heap_coalesce_around(header);
}

const char *kernel_heap_get_strategy_name(void) { return kernel_heap_strategy_name; }

uint32_t kernel_heap_get_small_free_block_count(void) { return kernel_heap_small_free_blocks; }

uint32_t kernel_heap_get_small_free_bytes(void) { return kernel_heap_small_free_bytes; }

uint32_t kernel_heap_get_large_allocation_count(void) { return kernel_heap_large_allocation_count; }

uint32_t kernel_heap_debug_get_rejected_free_count(void) { return kernel_heap_rejected_free_count; }

uint32_t kernel_heap_debug_get_double_free_count(void) { return kernel_heap_double_free_count; }

uint32_t kernel_heap_get_size_class_free_count(uint32_t sc)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(kernel_heap_server_get_local_domain_index());

    if (domain && sc < KERNEL_HEAP_SIZE_CLASSES)
        return domain->size_class_free_counts[sc];
#else
    (void) sc;
#endif
    return 0u;
}

uint32_t kernel_heap_get_size_class_hit_count(uint32_t sc)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(kernel_heap_server_get_local_domain_index());

    if (domain && sc < KERNEL_HEAP_SIZE_CLASSES)
        return domain->size_class_hit_counts[sc];
#else
    (void) sc;
#endif
    return 0u;
}

bool kernel_heap_set_server_active_domain(uint32_t domain_index)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    if (domain_index >= KERNEL_HEAP_SERVER_DOMAINS)
        return false;

    uint32_t logical_slot = cpu_topology_get_logical_slot();

    if (logical_slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
    {
        kernel_heap_server_slot_domain_override_enabled[logical_slot] = 1u;
        kernel_heap_server_slot_domain_override[logical_slot] = domain_index;
    }

    kernel_heap_server_active_domain = domain_index;
    return true;
#else
    (void) domain_index;
    return false;
#endif
}

uint32_t kernel_heap_get_server_domain_count(void)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    return KERNEL_HEAP_SERVER_DOMAINS;
#else
    return 0u;
#endif
}

uint32_t kernel_heap_get_server_active_domain(void)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    return kernel_heap_server_get_local_domain_index();
#else
    return 0u;
#endif
}

uint32_t kernel_heap_get_server_domain_refill_count(uint32_t domain_index, uint32_t sc)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(domain_index);

    if (domain && sc < KERNEL_HEAP_SIZE_CLASSES)
        return domain->size_class_refill_counts[sc];
#else
    (void) domain_index;
    (void) sc;
#endif
    return 0u;
}

uint32_t kernel_heap_get_server_domain_first_fit_fallback_count(uint32_t domain_index)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(domain_index);

    if (domain)
        return domain->first_fit_fallback_count;
#else
    (void) domain_index;
#endif
    return 0u;
}

uint32_t kernel_heap_get_server_domain_remote_probe_count(uint32_t domain_index)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(domain_index);

    if (domain)
        return domain->remote_probe_count;
#else
    (void) domain_index;
#endif
    return 0u;
}

uint32_t kernel_heap_get_server_domain_remote_hit_count(uint32_t domain_index)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(domain_index);

    if (domain)
        return domain->remote_hit_count;
#else
    (void) domain_index;
#endif
    return 0u;
}

uint32_t kernel_heap_get_server_per_cpu_hit_count(uint32_t slot)
{
#ifndef LPL_KERNEL_REAL_TIME_MODE
    KernelHeapServerDomain_t *domain = kernel_heap_server_get_domain(slot);
    if (!domain)
        return 0u;

    uint32_t sum = 0u;
    for (uint32_t sc = 0u; sc < KERNEL_HEAP_SIZE_CLASSES; ++sc)
        sum += domain->size_class_hit_counts[sc];
    return sum;
#else
    (void) slot;
    return 0u;
#endif
}


void kernel_heap_hot_loop_enter(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    ++kernel_heap_hot_loop_depth;
#endif
}

void kernel_heap_hot_loop_leave(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    if (kernel_heap_hot_loop_depth > 0u)
        --kernel_heap_hot_loop_depth;
#endif
}

uint32_t kernel_heap_get_hot_loop_depth(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    return kernel_heap_hot_loop_depth;
#else
    return 0u;
#endif
}

uint32_t kernel_heap_get_hot_loop_violation_count(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    return kernel_heap_hot_loop_violation_count;
#else
    return 0u;
#endif
}
