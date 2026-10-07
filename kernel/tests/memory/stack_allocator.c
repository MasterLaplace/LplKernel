#include <kernel/memory/stack_allocator.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(stack_allocator, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Rolling back to a marker frees everything pushed after it, and the space is served again.
 */
KERNEL_TEST(rollback_frees_what_followed_the_marker)
{
    if (!kernel_test_check(test, kernel_stack_allocator_is_initialized(), "the stack allocator is initialised"))
        return;

    const uint32_t start = kernel_stack_alloc_get_marker();
    void *const first = kernel_stack_alloc_push(1024u, 8u);
    const uint32_t after_first = kernel_stack_alloc_get_marker();
    void *const second = kernel_stack_alloc_push(2048u, 16u);

    kernel_stack_alloc_rollback(after_first);
    void *const reused = kernel_stack_alloc_push(1024u, 16u);
    kernel_stack_alloc_rollback(start);

    kernel_test_check(test, first != NULL && second != NULL, "two blocks are pushed");
    kernel_test_check(test, second != NULL && reused == second,
                      "after a rollback the next push takes the space the rolled-back block had");
    kernel_test_check(test, kernel_stack_alloc_get_marker() == start, "rolling back to the start empties it");
}
