#define __LPL_KERNEL__

#include <kernel/mm/slab.h>
#include <stddef.h>

/*
 * Implementation layout
 * ─────────────────────
 * Each cache owns a singly-linked free-list of same-sized objects.
 * The free-list node is stored *inside* the object slot (the first
 * sizeof(void*) bytes of a free slot hold the next pointer).  This
 * means no extra header memory per object.
 *
 * SERVER profile: every exported function is a one-liner stub.  The
 * linker discards the slab data entirely on server builds.
 */

#ifdef LPL_KERNEL_REAL_TIME_MODE

/* ------------------------------------------------------------------ */
/* CLIENT implementation                                               */
/* ------------------------------------------------------------------ */

/*
 * Anti-double-free guard.
 * When a slot is placed on the free-list we write SLAB_FREE_COOKIE
 * into the word immediately after the next pointer.
 * kernel_slab_free() checks this: if the cookie is already present
 * the object is already free (double-free) -> reject, return false.
 * kernel_slab_alloc() clears the cookie on allocation.
 */
#define SLAB_FREE_COOKIE 0x534C4131u  /* 'SLA1' */

#define SLAB_NUM_CACHES 3u

typedef struct {
    uint32_t object_size;  /* bytes per object                        */
    void    *free_head;    /* head of the free-list (NULL == full)    */
    uint32_t free_count;   /* objects currently on the free-list      */
    uint32_t used_count;   /* objects currently live                  */
    uintptr_t base;        /* virtual address of the first owned page */
    uintptr_t end;         /* first byte past the owned pages         */
} KernelSlabCache_t;

static KernelSlabCache_t slab_caches[SLAB_NUM_CACHES] = {
    { KERNEL_SLAB_SIZE_SMALL,  NULL, 0u, 0u, 0u, 0u },
    { KERNEL_SLAB_SIZE_MEDIUM, NULL, 0u, 0u, 0u, 0u },
    { KERNEL_SLAB_SIZE_LARGE,  NULL, 0u, 0u, 0u, 0u },
};

/*
 * Carve all objects from a donated page into a cache's free-list.
 * page_virt must be a valid, mapped virtual address.
 */
static void slab_cache_populate(KernelSlabCache_t *cache, void *page_virt, uint32_t page_size)
{
    uint32_t obj_size = cache->object_size;
    uint8_t *cursor   = (uint8_t *) page_virt;
    uint8_t *page_end = cursor + page_size;
    uint32_t stride = obj_size;
#ifdef LPL_KERNEL_DEBUG_POISON
    stride += sizeof(uint32_t);
    stride = (stride + 7u) & ~7u;
#endif

    /* Track ownership range across donated pages. */
    if (cache->base == 0u || (uintptr_t) page_virt < cache->base)
        cache->base = (uintptr_t) page_virt;
    if ((uintptr_t) page_end > cache->end)
        cache->end = (uintptr_t) page_end;

    while ((cursor + stride) <= page_end)
    {
        /* Use the first word of the free slot as the next pointer,
         * and the second word as the free-cookie guard. */
        void **slot = (void **) cursor;
        *slot = cache->free_head;
        *((uint32_t *) (cursor + sizeof(void *))) = SLAB_FREE_COOKIE;
#ifdef LPL_KERNEL_DEBUG_POISON
        *((uint32_t *) (cursor + obj_size)) = 0xC001CAFEu;
#endif
        cache->free_head = cursor;
        ++cache->free_count;
        cursor += stride;
    }
}

void kernel_slab_initialize(void **backing_pages, uint32_t page_count)
{
    /* 4 KiB page size; pull from the compile-time constant. */
#ifndef PAGE_SIZE
#    define PAGE_SIZE 4096u
#endif

    /*
     * Page distribution strategy: round-robin over the three caches so
     * each gets a roughly even share of the donated pages.  With the
     * default 6 donated pages (KERNEL_SLAB_CACHE_MAX_PAGES*3) each
     * cache gets exactly 2 pages.
     */
    for (uint32_t i = 0u; i < page_count; ++i)
    {
        if (!backing_pages[i])
            continue;
        uint32_t cache_index = i % SLAB_NUM_CACHES;
        slab_cache_populate(&slab_caches[cache_index], backing_pages[i], PAGE_SIZE);
    }
}

void *kernel_slab_alloc(uint32_t size)
{
    for (uint32_t i = 0u; i < SLAB_NUM_CACHES; ++i)
    {
        if (slab_caches[i].object_size != size)
            continue;

        KernelSlabCache_t *cache = &slab_caches[i];

        if (!cache->free_head)
            return NULL;

        void *obj = cache->free_head;
        cache->free_head = *((void **) obj);
        /* Clear the free-cookie so the slot is distinguishable as live. */
        *((uint32_t *) ((uint8_t *) obj + sizeof(void *))) = 0u;
        --cache->free_count;
        ++cache->used_count;
        return obj;
    }
    return NULL;
}

bool kernel_slab_free(void *ptr)
{
    if (!ptr)
        return false;

    uintptr_t addr = (uintptr_t) ptr;

    for (uint32_t i = 0u; i < SLAB_NUM_CACHES; ++i)
    {
        KernelSlabCache_t *cache = &slab_caches[i];

        /* Ownership check: pointer must fall within the donated range. */
        if (addr < cache->base || addr >= cache->end)
            continue;

        /* Alignment check: must be object-aligned within the range. */
        uint32_t stride = cache->object_size;
#ifdef LPL_KERNEL_DEBUG_POISON
        stride += sizeof(uint32_t);
        stride = (stride + 7u) & ~7u;
#endif
        if ((addr - cache->base) % stride != 0u)
            continue;

#ifdef LPL_KERNEL_DEBUG_POISON
        uint32_t *end_canary = (uint32_t *) ((uint8_t *) ptr + cache->object_size);
        if (*end_canary != 0xC001CAFEu)
        {
            return false; /* Canary corrupted */
        }
#endif

        /* Double-free guard: cookie already present means already free. */
        uint32_t *cookie_word = (uint32_t *) ((uint8_t *) ptr + sizeof(void *));
        if (*cookie_word == SLAB_FREE_COOKIE)
            return false;

        *((void **) ptr) = cache->free_head;
        *cookie_word = SLAB_FREE_COOKIE;
        cache->free_head = ptr;
        ++cache->free_count;
        if (cache->used_count > 0u)
            --cache->used_count;
        return true;
    }
    return false;
}

uint32_t kernel_slab_get_free_count(uint32_t object_size)
{
    for (uint32_t i = 0u; i < SLAB_NUM_CACHES; ++i)
    {
        if (slab_caches[i].object_size == object_size)
            return slab_caches[i].free_count;
    }
    return 0u;
}

uint32_t kernel_slab_get_used_count(uint32_t object_size)
{
    for (uint32_t i = 0u; i < SLAB_NUM_CACHES; ++i)
    {
        if (slab_caches[i].object_size == object_size)
            return slab_caches[i].used_count;
    }
    return 0u;
}

#else /* SERVER profile stubs */

/* ------------------------------------------------------------------ */
/* SERVER stubs — the slab is not used on server builds.              */
/* ------------------------------------------------------------------ */

void kernel_slab_initialize(void **backing_pages, uint32_t page_count)
{
    (void) backing_pages;
    (void) page_count;
}

void *kernel_slab_alloc(uint32_t size)
{
    (void) size;
    return NULL;
}

bool kernel_slab_free(void *ptr)
{
    (void) ptr;
    return false;
}

uint32_t kernel_slab_get_free_count(uint32_t object_size)
{
    (void) object_size;
    return 0u;
}

uint32_t kernel_slab_get_used_count(uint32_t object_size)
{
    (void) object_size;
    return 0u;
}

#endif /* LPL_KERNEL_REAL_TIME_MODE */
