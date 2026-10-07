#include <kernel/memory/frame_arena.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(frame_arena, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Two blocks are served apart, a reset rewinds to the first, and the arena never hands out
 *        more than it holds.
 */
KERNEL_TEST(allocate_reset_and_capacity)
{
    if (!kernel_test_check(test, kernel_frame_arena_is_initialized(), "the frame arena is initialised"))
        return;

    const uint32_t resets_before = kernel_frame_arena_get_reset_count();
    const uint32_t failures_before = kernel_frame_arena_get_failed_alloc_count();
    void *const first = kernel_frame_arena_alloc(64u, 8u);
    void *const second = kernel_frame_arena_alloc(96u, 16u);

    kernel_test_check(test, first != NULL && second != NULL && first != second, "two blocks are served apart");

    kernel_frame_arena_reset();
    kernel_test_check(test, first != NULL && kernel_frame_arena_alloc(64u, 8u) == first,
                      "a reset rewinds to the first block");
    kernel_test_check(test, kernel_frame_arena_get_reset_count() == resets_before + 1u, "the reset is counted");
    kernel_test_check(test, kernel_frame_arena_get_failed_alloc_count() == failures_before,
                      "nothing that fitted was refused");

    kernel_frame_arena_reset();
    const uint32_t capacity = kernel_frame_arena_get_capacity_bytes();
    (void) kernel_frame_arena_alloc(capacity, 8u);
    kernel_test_check(test, kernel_frame_arena_get_used_bytes() <= capacity,
                      "asking for the whole capacity never takes more than the arena holds");
    kernel_frame_arena_reset();
}

/**
 * @brief Inside a frame budget, a request that would exceed it is refused and counted, and lifting
 *        the budget lifts the limit.
 */
KERNEL_TEST(budget_refuses_and_counts_overruns)
{
    if (!kernel_test_check(test, kernel_frame_arena_is_initialized(), "the frame arena is initialised"))
        return;

    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(0u);

    const uint32_t overruns_before = kernel_frame_arena_get_budget_exceeded_count();
    const uint32_t failures_before = kernel_frame_arena_get_failed_alloc_count();

    kernel_frame_arena_set_frame_budget(64u);
    void *const within = kernel_frame_arena_alloc(32u, 8u);
    void *const beyond = kernel_frame_arena_alloc(48u, 8u);
    const uint32_t overruns = kernel_frame_arena_get_budget_exceeded_count() - overruns_before;
    const uint32_t failures = kernel_frame_arena_get_failed_alloc_count() - failures_before;
    void *const still_within = kernel_frame_arena_alloc(16u, 8u);

    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(0u);
    void *const unconstrained = kernel_frame_arena_alloc(128u, 8u);
    kernel_frame_arena_reset();

    kernel_test_check(test, within != NULL, "a request within the budget is served");
    kernel_test_check(test, beyond == NULL, "a request that would exceed it is refused");
    kernel_test_check(test, overruns == 1u && failures == 1u, "the refusal is counted as one overrun and one failure");
    kernel_test_check(test, still_within != NULL && still_within != within, "what still fits is served after it");
    kernel_test_check(test, unconstrained != NULL, "with no budget the same request is served");
}

/**
 * @brief A reset writes the poison pattern over every byte the frame used.
 *
 * @details 64 bytes of a fresh frame are filled with 0xBB, the arena is reset, and every byte must
 *          read 0xAA. Tests are only built with the poisoning, so the check always runs.
 */
KERNEL_TEST(reset_poisons_what_the_frame_used)
{
    if (!kernel_test_check(test, kernel_frame_arena_is_initialized(), "the frame arena is initialised"))
        return;

    kernel_frame_arena_reset();
    uint8_t *const bytes = (uint8_t *) kernel_frame_arena_alloc(64u, 8u);

    if (!kernel_test_check(test, bytes != NULL, "64 bytes are served"))
        return;

    for (uint32_t index = 0u; index < 64u; ++index)
        bytes[index] = 0xBBu;
    kernel_frame_arena_reset();

    uint32_t mismatches = 0u;

    for (uint32_t index = 0u; index < 64u; ++index)
        mismatches += (bytes[index] != 0xAAu) ? 1u : 0u;
    kernel_test_check(test, mismatches == 0u, "every byte the frame used reads as poison after the reset");
}

/**
 * @brief A thousand frames of three aligned blocks each are all served.
 */
KERNEL_TEST(thousand_frames_are_served)
{
    if (!kernel_test_check(test, kernel_frame_arena_is_initialized(), "the frame arena is initialised"))
        return;

    uint32_t frames_served = 0u;

    for (uint32_t frame = 0u; frame < 1000u; ++frame)
    {
        kernel_frame_arena_reset();
        if (!kernel_frame_arena_alloc(64u, 8u) || !kernel_frame_arena_alloc(128u, 16u) ||
            !kernel_frame_arena_alloc(256u, 32u))
            break;
        ++frames_served;
    }
    kernel_frame_arena_reset();

    kernel_test_check(test, frames_served == 1000u, "a thousand frames of three blocks are all served");
}
