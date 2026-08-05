/**
 * @file tensor_arena.c
 * @brief One large, bounded region for weights and activations.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/ai/tensor_arena.h>

#include <kernel/memory/heap.h>

static void *tensor_arena_base = NULL;
static size_t tensor_arena_size = 0u;
static size_t tensor_arena_high_water = 0u;

bool kernel_tensor_arena_initialize(size_t bytes)
{
    if (bytes == 0u)
        return false;

    if (tensor_arena_base != NULL)
        return tensor_arena_size >= bytes;

    /* One allocation, at boot, while the heap is still uncontended. Doing this
       lazily on the first inference would put the largest allocation the kernel
       ever makes in the middle of a running world, which is exactly when it is
       least likely to be servable. */
    void *const block = kmalloc(bytes);
    if (block == NULL)
        return false;

    tensor_arena_base = block;
    tensor_arena_size = bytes;
    tensor_arena_high_water = 0u;
    return true;
}

void kernel_tensor_arena_release(void)
{
    if (tensor_arena_base == NULL)
        return;
    kfree(tensor_arena_base);
    tensor_arena_base = NULL;
    tensor_arena_size = 0u;
}

void *kernel_tensor_arena_base(void) { return tensor_arena_base; }

size_t kernel_tensor_arena_size(void) { return tensor_arena_size; }

bool kernel_tensor_arena_ready(void) { return tensor_arena_base != NULL; }

void kernel_tensor_arena_record_used(size_t bytes)
{
    if (bytes > tensor_arena_high_water)
        tensor_arena_high_water = bytes;
}

size_t kernel_tensor_arena_high_water(void) { return tensor_arena_high_water; }
