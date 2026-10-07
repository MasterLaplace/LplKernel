#include <kernel/memory/pool_allocator.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(pool_allocator, KERNEL_TEST_STAGE_INITIALIZATION);

/** Most objects the exhaustion test holds at once; the boot's pool is smaller. */
#define POOL_TEST_MAXIMUM_OBJECTS 256u

/**
 * @brief Every object is served until the pool runs out, the next request is refused and counted,
 *        and an object freed and served again reads as poison.
 */
KERNEL_TEST(exhaustion_is_refused_and_counted)
{
    if (!kernel_test_check(test, kernel_pool_allocator_is_initialized(), "the pool is initialised"))
        return;

    void *objects[POOL_TEST_MAXIMUM_OBJECTS] = {0};
    const uint32_t capacity = kernel_pool_get_capacity();
    const uint32_t free_before = kernel_pool_get_free_count();
    const uint32_t failures_before = kernel_pool_get_failed_alloc_count();
    uint32_t served = 0u;

    if (!kernel_test_check(test, capacity <= POOL_TEST_MAXIMUM_OBJECTS, "the pool fits in the test's table"))
        return;

    while (served < capacity && (objects[served] = kernel_pool_alloc()) != NULL)
        ++served;

    void *const beyond = kernel_pool_alloc();

    for (uint32_t index = 0u; index < served; ++index)
        kernel_pool_free(objects[index]);

    kernel_test_check(test, served == capacity, "every object of the pool is served");
    kernel_test_check(test, beyond == NULL, "a request past the last one is refused");
    kernel_test_check(test, kernel_pool_get_failed_alloc_count() == failures_before + 1u, "and counted once");
    kernel_test_check(test, kernel_pool_get_free_count() == free_before, "freeing them all restores the free count");

    uint8_t *const again = (uint8_t *) kernel_pool_alloc();

    if (!kernel_test_check(test, again != NULL, "an object is served again"))
        return;
    kernel_test_check(test, kernel_pool_get_object_size() <= sizeof(void *) + 4u || again[sizeof(void *) + 4u] == 0xDFu,
                      "past its free-list link, a freed object reads as poison");
    kernel_pool_free(again);
}

/**
 * @brief The second free of the same object is refused.
 */
KERNEL_TEST(double_free_is_refused)
{
    if (!kernel_test_check(test, kernel_pool_allocator_is_initialized(), "the pool is initialised"))
        return;

    void *const object = kernel_pool_alloc();

    if (!kernel_test_check(test, object != NULL, "an object is served"))
        return;
    kernel_test_check(test, kernel_pool_free(object), "the first free is accepted");
    kernel_test_check(test, !kernel_pool_free(object), "the second free of the same object is refused");
}
