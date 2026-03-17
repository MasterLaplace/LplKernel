/*
** LplKernel
** kernel/kernel/tlsf.c
**
** Two-Level Segregated Fit (TLSF) O(1) deterministic allocator.
**
** This is a freestanding implementation for the client realtime kernel
** profile.  It manages a single contiguous memory pool donated at boot
** time.  Allocation and deallocation are O(1) (bounded by a constant
** number of bit-scan + pointer dereferences).
**
** Design:
**   - FLI (First-Level Index): log2(block_size), up to TLSF_FLI_COUNT.
**   - SLI (Second-Level Index): TLSF_SLI_LOG2 bits => 4 sub-divisions.
**   - Two bitmaps (fl_bitmap, sl_bitmap[]) for O(1) best-fit search.
**   - Each block has a header: { size | prev_phys_block | free_prev | free_next }.
**   - Immediate coalescing on free (boundary-tag style).
*/

#include <kernel/mm/tlsf.h>

/* ── Tuning ────────────────────────────────────────────── */

/** Number of second-level index bits per first-level class. */
#define TLSF_SLI_LOG2       2u
#define TLSF_SLI_COUNT      (1u << TLSF_SLI_LOG2)   /* 4 */

/** Maximum first-level classes (covers up to 2^(FLI-1) byte blocks). */
#define TLSF_FLI_COUNT      16u

/** Minimum block payload size (bytes).  Must be >= 2 * sizeof(void*). */
#define TLSF_MIN_BLOCK_SIZE 16u

/** Alignment of all returned pointers. */
#define TLSF_ALIGN          8u
#define TLSF_ALIGN_MASK     (TLSF_ALIGN - 1u)

/** Bit flags stored in the low bits of block_header_t::size. */
#define TLSF_BLOCK_FREE_BIT     (1u << 0)
#define TLSF_BLOCK_PREV_FREE_BIT (1u << 1)
#define TLSF_BLOCK_FLAG_MASK    (TLSF_BLOCK_FREE_BIT | TLSF_BLOCK_PREV_FREE_BIT)

/* ── Block header ──────────────────────────────────────── */

typedef struct block_header
{
    /**
     * Size of the usable payload in bytes, with the two LSBs used as flags:
     *   bit 0: 1 = this block is free
     *   bit 1: 1 = the physically previous block is free
     */
    uint32_t size;

    /**
     * Pointer to the physically previous block.  NULL for the first block.
     */
    struct block_header *prev_phys;

    /**
     * When free, the block is part of a doubly-linked segregated free list.
     * These fields lie inside the payload area (only valid when free).
     */
    struct block_header *free_prev;
    struct block_header *free_next;
} block_header_t;

/* Compile-time check: header must fit in TLSF_MIN_BLOCK_SIZE + overhead. */
_Static_assert(sizeof(block_header_t) <= TLSF_MIN_BLOCK_SIZE + 2 * sizeof(uint32_t),
               "block_header_t too large for TLSF_MIN_BLOCK_SIZE");

/* Overhead = the non-payload part of the header (size + prev_phys). */
#define TLSF_BLOCK_OVERHEAD  (sizeof(uint32_t) + sizeof(block_header_t *))

/* ── TLSF control structure ───────────────────────────── */

typedef struct
{
    /** FL-level bitmap: bit i set => at least one free block in FL class i. */
    uint32_t fl_bitmap;

    /** SL-level bitmaps: bit j in sl_bitmap[i] => free block in (i, j). */
    uint32_t sl_bitmap[TLSF_FLI_COUNT];

    /** Segregated free-list heads indexed by [fl][sl]. */
    block_header_t *free_lists[TLSF_FLI_COUNT][TLSF_SLI_COUNT];

    /** Pool boundaries for ownership checks. */
    uint8_t *pool_start;
    uint8_t *pool_end;

    /** Telemetry. */
    uint32_t pool_size;
    uint32_t free_bytes;
    uint32_t alloc_count;
    uint32_t free_op_count;
    uint32_t failed_alloc_count;
    uint32_t wcet_alloc_cycles;
    uint32_t wcet_free_cycles;

    bool initialized;
} tlsf_control_t;

static tlsf_control_t g_tlsf;

/* ── Compiler builtins (freestanding) ─────────────────── */

static inline uint32_t tlsf_clz(uint32_t v)
{
    return v ? (uint32_t) __builtin_clz(v) : 32u;
}

static inline uint32_t tlsf_ffs(uint32_t v)
{
    return v ? (uint32_t) __builtin_ctz(v) : 32u;
}

static inline uint32_t tlsf_log2_floor(uint32_t v)
{
    return 31u - tlsf_clz(v);
}

/* ── rdtsc helper (optional; gracefully degrades) ─────── */

static inline uint32_t tlsf_rdtsc_low(void)
{
#if defined(__i386__) || defined(__x86_64__)
    uint32_t lo;
    asm volatile("rdtsc" : "=a"(lo) :: "edx");
    return lo;
#else
    return 0u;
#endif
}

/* ── Block helpers ─────────────────────────────────────── */

static inline uint32_t block_get_size(const block_header_t *b)
{
    return b->size & ~TLSF_BLOCK_FLAG_MASK;
}

static inline void block_set_size(block_header_t *b, uint32_t sz)
{
    b->size = sz | (b->size & TLSF_BLOCK_FLAG_MASK);
}

static inline bool block_is_free(const block_header_t *b)
{
    return (b->size & TLSF_BLOCK_FREE_BIT) != 0u;
}

static inline void block_set_free(block_header_t *b)
{
    b->size |= TLSF_BLOCK_FREE_BIT;
}

static inline void block_set_used(block_header_t *b)
{
    b->size &= ~TLSF_BLOCK_FREE_BIT;
}

static inline bool block_is_prev_free(const block_header_t *b)
{
    return (b->size & TLSF_BLOCK_PREV_FREE_BIT) != 0u;
}

static inline void block_set_prev_free(block_header_t *b)
{
    b->size |= TLSF_BLOCK_PREV_FREE_BIT;
}

static inline void block_set_prev_used(block_header_t *b)
{
    b->size &= ~TLSF_BLOCK_PREV_FREE_BIT;
}

static inline void *block_to_ptr(const block_header_t *b)
{
    return (void *) ((uint8_t *) b + TLSF_BLOCK_OVERHEAD);
}

static inline block_header_t *ptr_to_block(const void *ptr)
{
    return (block_header_t *) ((uint8_t *) ptr - TLSF_BLOCK_OVERHEAD);
}

/** The next physically contiguous block. */
static inline block_header_t *block_next_phys(const block_header_t *b)
{
    return (block_header_t *) ((uint8_t *) block_to_ptr(b) + block_get_size(b));
}

/** Check if a block is the sentinel (last in pool). */
static inline bool block_is_sentinel(const block_header_t *b)
{
    return block_get_size(b) == 0u;
}

/* ── Mapping: size → (fl, sl) ─────────────────────────── */

static void mapping_insert(uint32_t size, uint32_t *p_fl, uint32_t *p_sl)
{
    if (size < (1u << (TLSF_SLI_LOG2 + 1u)))
    {
        *p_fl = 0u;
        *p_sl = size / (TLSF_MIN_BLOCK_SIZE / TLSF_SLI_COUNT);
        if (*p_sl >= TLSF_SLI_COUNT)
            *p_sl = TLSF_SLI_COUNT - 1u;
    }
    else
    {
        uint32_t fl = tlsf_log2_floor(size);
        uint32_t sl = (size >> (fl - TLSF_SLI_LOG2)) ^ (1u << TLSF_SLI_LOG2);
        if (fl >= TLSF_FLI_COUNT)
        {
            fl = TLSF_FLI_COUNT - 1u;
            sl = TLSF_SLI_COUNT - 1u;
        }
        *p_fl = fl;
        *p_sl = sl;
    }
}

/** Round-up mapping: find the smallest (fl, sl) that can satisfy size. */
static void mapping_search(uint32_t size, uint32_t *p_fl, uint32_t *p_sl)
{
    /* Round up to the next class boundary so we don't return too-small blocks. */
    if (size >= (1u << (TLSF_SLI_LOG2 + 1u)))
    {
        uint32_t round = (1u << (tlsf_log2_floor(size) - TLSF_SLI_LOG2)) - 1u;
        size += round;
    }
    mapping_insert(size, p_fl, p_sl);
}

/* ── Free-list manipulation ───────────────────────────── */

static void free_list_remove(block_header_t *block)
{
    block_header_t *prev = block->free_prev;
    block_header_t *next = block->free_next;

    if (next)
        next->free_prev = prev;
    if (prev)
        prev->free_next = next;

    /* If block was the head, update the free list head. */
    uint32_t fl, sl;
    mapping_insert(block_get_size(block), &fl, &sl);

    if (g_tlsf.free_lists[fl][sl] == block)
    {
        g_tlsf.free_lists[fl][sl] = next;
        if (!next)
        {
            g_tlsf.sl_bitmap[fl] &= ~(1u << sl);
            if (g_tlsf.sl_bitmap[fl] == 0u)
                g_tlsf.fl_bitmap &= ~(1u << fl);
        }
    }
}

static void free_list_insert(block_header_t *block)
{
    uint32_t fl, sl;
    mapping_insert(block_get_size(block), &fl, &sl);

    block_header_t *head = g_tlsf.free_lists[fl][sl];
    block->free_prev = NULL;
    block->free_next = head;
    if (head)
        head->free_prev = block;
    g_tlsf.free_lists[fl][sl] = block;

    g_tlsf.fl_bitmap |= (1u << fl);
    g_tlsf.sl_bitmap[fl] |= (1u << sl);
}

/* ── Find a suitable free block ───────────────────────── */

static block_header_t *find_suitable_block(uint32_t *p_fl, uint32_t *p_sl)
{
    uint32_t fl = *p_fl;
    uint32_t sl = *p_sl;

    /* Search in the same FL class for a larger SL. */
    uint32_t sl_map = g_tlsf.sl_bitmap[fl] & (~0u << sl);

    if (sl_map == 0u)
    {
        /* No block in this FL — search higher FL classes. */
        uint32_t fl_map = g_tlsf.fl_bitmap & (~0u << (fl + 1u));
        if (fl_map == 0u)
            return NULL; /* out of memory */

        fl = tlsf_ffs(fl_map);
        sl_map = g_tlsf.sl_bitmap[fl];
    }

    sl = tlsf_ffs(sl_map);

    *p_fl = fl;
    *p_sl = sl;
    return g_tlsf.free_lists[fl][sl];
}

/* ── Split / absorb helpers ───────────────────────────── */

static block_header_t *block_split(block_header_t *block, uint32_t size)
{
    uint32_t remaining = block_get_size(block) - size - TLSF_BLOCK_OVERHEAD;

    block_header_t *rest = (block_header_t *) ((uint8_t *) block_to_ptr(block) + size);
    rest->size = 0u;
    block_set_size(rest, remaining);
    rest->prev_phys = block;

    block_set_size(block, size);

    /* Update the next physical block's prev_phys pointer. */
    block_header_t *next = block_next_phys(rest);
    if (!block_is_sentinel(next))
        next->prev_phys = rest;

    return rest;
}

static block_header_t *block_absorb_next(block_header_t *block, block_header_t *next)
{
    uint32_t new_size = block_get_size(block) + TLSF_BLOCK_OVERHEAD + block_get_size(next);
    block_set_size(block, new_size);

    block_header_t *next_next = block_next_phys(block);
    if (!block_is_sentinel(next_next))
        next_next->prev_phys = block;

    return block;
}

/* ── Public API ────────────────────────────────────────── */

bool kernel_tlsf_initialize(void *buffer, size_t size)
{
    if (!buffer || size < (TLSF_BLOCK_OVERHEAD + TLSF_MIN_BLOCK_SIZE + TLSF_BLOCK_OVERHEAD))
        return false;

    /* Alignment check. */
    uintptr_t buf_addr = (uintptr_t) buffer;
    if (buf_addr & TLSF_ALIGN_MASK)
    {
        uintptr_t aligned = (buf_addr + TLSF_ALIGN_MASK) & ~((uintptr_t) TLSF_ALIGN_MASK);
        size -= (size_t) (aligned - buf_addr);
        buffer = (void *) aligned;
    }

    /* Zero control structure. */
    for (uint32_t i = 0u; i < sizeof(g_tlsf); ++i)
        ((uint8_t *) &g_tlsf)[i] = 0u;

    g_tlsf.pool_start = (uint8_t *) buffer;
    g_tlsf.pool_end = (uint8_t *) buffer + size;
    g_tlsf.pool_size = (uint32_t) size;

    /* Create sentinel block at the end (zero-size, used). */
    block_header_t *sentinel = (block_header_t *) (g_tlsf.pool_end - TLSF_BLOCK_OVERHEAD);
    sentinel->size = 0u; /* size=0, free=0, prev_free=0 */
    sentinel->prev_phys = NULL;

    /* Create the initial free block spanning the entire pool. */
    block_header_t *first = (block_header_t *) buffer;
    uint32_t usable = (uint32_t) size - TLSF_BLOCK_OVERHEAD - TLSF_BLOCK_OVERHEAD;
    first->size = 0u;
    block_set_size(first, usable);
    block_set_free(first);
    first->prev_phys = NULL;
    first->free_prev = NULL;
    first->free_next = NULL;

    sentinel->prev_phys = first;

    /* Insert the initial block into the free lists. */
    free_list_insert(first);

    g_tlsf.free_bytes = usable;
    g_tlsf.initialized = true;
    return true;
}

void *kernel_tlsf_alloc(size_t request_size)
{
    if (!g_tlsf.initialized || request_size == 0u)
        return NULL;

    uint32_t t0 = tlsf_rdtsc_low();

    /* Round up to minimum and align. */
    uint32_t size = (uint32_t) request_size;
    if (size < TLSF_MIN_BLOCK_SIZE)
        size = TLSF_MIN_BLOCK_SIZE;
    size = (size + TLSF_ALIGN_MASK) & ~TLSF_ALIGN_MASK;

    uint32_t fl, sl;
    mapping_search(size, &fl, &sl);

    block_header_t *block = find_suitable_block(&fl, &sl);
    if (!block)
    {
        ++g_tlsf.failed_alloc_count;
        return NULL;
    }

    /* Remove from free list. */
    free_list_remove(block);

    /* Split if the block is much larger than needed. */
    uint32_t bsize = block_get_size(block);
    if (bsize >= size + TLSF_BLOCK_OVERHEAD + TLSF_MIN_BLOCK_SIZE)
    {
        block_header_t *rest = block_split(block, size);
        block_set_free(rest);
        block_set_prev_used(rest); /* this block will be used */
        free_list_insert(rest);
        g_tlsf.free_bytes -= (size + TLSF_BLOCK_OVERHEAD);
    }
    else
    {
        g_tlsf.free_bytes -= bsize;
    }

    /* Mark block as used. */
    block_set_used(block);

    /* Mark next physical block's prev_free flag to 0. */
    block_header_t *next = block_next_phys(block);
    if (!block_is_sentinel(next))
        block_set_prev_used(next);

    ++g_tlsf.alloc_count;

    uint32_t t1 = tlsf_rdtsc_low();
    uint32_t cycles = t1 - t0;
    if (cycles > g_tlsf.wcet_alloc_cycles)
        g_tlsf.wcet_alloc_cycles = cycles;

    return block_to_ptr(block);
}

void kernel_tlsf_free(void *ptr)
{
    if (!ptr || !g_tlsf.initialized)
        return;

    uint32_t t0 = tlsf_rdtsc_low();

    block_header_t *block = ptr_to_block(ptr);
    uint32_t added_free_bytes = block_get_size(block);

    /* Coalesce with next block if free. */
    block_header_t *next = block_next_phys(block);
    if (!block_is_sentinel(next) && block_is_free(next))
    {
        free_list_remove(next);
        added_free_bytes += TLSF_BLOCK_OVERHEAD;
        block = block_absorb_next(block, next);
    }

    /* Coalesce with previous block if free. */
    if (block_is_prev_free(block) && block->prev_phys)
    {
        block_header_t *prev = block->prev_phys;
        if (block_is_free(prev))
        {
            free_list_remove(prev);
            added_free_bytes += TLSF_BLOCK_OVERHEAD;
            prev = block_absorb_next(prev, block);
            block = prev;
        }
    }

    /* Mark as free and insert into free list. */
    block_set_free(block);
    free_list_insert(block);

    g_tlsf.free_bytes += added_free_bytes;

    /* Mark next physical block's prev_free flag. */
    block_header_t *next_after = block_next_phys(block);
    if (!block_is_sentinel(next_after))
        block_set_prev_free(next_after);

    ++g_tlsf.free_op_count;

    uint32_t t1 = tlsf_rdtsc_low();
    uint32_t cycles = t1 - t0;
    if (cycles > g_tlsf.wcet_free_cycles)
        g_tlsf.wcet_free_cycles = cycles;
}

bool kernel_tlsf_owns(const void *ptr)
{
    if (!ptr || !g_tlsf.initialized)
        return false;
    const uint8_t *p = (const uint8_t *) ptr;
    return p >= g_tlsf.pool_start && p < g_tlsf.pool_end;
}

bool kernel_tlsf_is_initialized(void)        { return g_tlsf.initialized; }
uint32_t kernel_tlsf_get_pool_size(void)     { return g_tlsf.pool_size; }
uint32_t kernel_tlsf_get_free_bytes(void)    { return g_tlsf.free_bytes; }
uint32_t kernel_tlsf_get_alloc_count(void)   { return g_tlsf.alloc_count; }
uint32_t kernel_tlsf_get_free_count(void)    { return g_tlsf.free_op_count; }
uint32_t kernel_tlsf_get_failed_alloc_count(void) { return g_tlsf.failed_alloc_count; }
uint32_t kernel_tlsf_get_wcet_alloc_cycles(void)  { return g_tlsf.wcet_alloc_cycles; }
uint32_t kernel_tlsf_get_wcet_free_cycles(void)    { return g_tlsf.wcet_free_cycles; }
