#include <kernel/testing/test.h>

#include <fcntl.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

KERNEL_TEST_SUITE(host_undefined_behaviour, KERNEL_TEST_STAGE_INITIALIZATION);

static const volatile int32_t undefined_behaviour_largest = INT32_MAX; /**< What the child adds to, less one. */
static volatile int32_t undefined_behaviour_sum;                       /**< Where the child puts that sum. */

/**
 * @brief Runs a child process that adds @p addend to the largest int32_t less one, its error output sent to
 *        /dev/null, and gives how it ended, as waitpid tells it, or -1 when it could not run: an addend of 1 stays in
 *        range, one of 2 overflows.
 */
static int undefined_behaviour_child(int32_t addend)
{
    int status = 0;
    const pid_t child = fork();

    if (child == 0)
    {
        dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);
        undefined_behaviour_sum = undefined_behaviour_largest - 1 + addend;
        _exit(0);
    }
    if (child < 0 || waitpid(child, &status, 0) != child)
        return -1;
    return status;
}

/**
 * @brief Undefined behaviour stops the harness instead of being reported and stepped over: a child process of it
 *        that overflows a signed integer ends with a failure, where the same child that stays in range exits 0.
 */
KERNEL_TEST(undefined_behaviour_stops_the_harness)
{
    const int in_range = undefined_behaviour_child(1);
    const int overflow = undefined_behaviour_child(2);

    if (!kernel_test_check(test, in_range != -1 && overflow != -1, "both child processes run"))
        return;
    kernel_test_check(test, WIFEXITED(in_range) && WEXITSTATUS(in_range) == 0,
                      "the child that stays in range exits with status 0");
    kernel_test_check(test, !(WIFEXITED(overflow) && WEXITSTATUS(overflow) == 0),
                      "the signed overflow ends the child with a failure, not with exit status 0");
}
