#include <kernel/boot/init_array.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(constructors, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The static constructors ran before kernel_main, whichever section the toolchain put them
 *        in: the engine's C++ is built on them.
 */
KERNEL_TEST(sentinel_constructor_ran)
{
    kernel_test_check(test, kernel_constructor_self_test_passed() != 0u,
                      "the sentinel constructor ran before kernel_main");
}
