#include <kernel/testing/test.h>

#include <fcntl.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

KERNEL_TEST_SUITE(host_undefined_behaviour, KERNEL_TEST_STAGE_INITIALIZATION);

static volatile int32_t undefined_behaviour_largest = INT32_MAX; /**< What the child adds one to. */
static volatile int32_t undefined_behaviour_sum;                 /**< Where the child puts that sum. */

/**
 * @brief Runs a child process that adds one to the largest int32_t, its error output sent to /dev/null, and gives
 *        how it ended, as waitpid tells it, or -1 when it could not run.
 */
static int undefined_behaviour_overflowing_child(void)
{
    int status = 0;
    const pid_t child = fork();

    if (child == 0)
    {
        dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);
        undefined_behaviour_sum = undefined_behaviour_largest + 1;
        _exit(0);
    }
    if (child < 0 || waitpid(child, &status, 0) != child)
        return -1;
    return status;
}

/**
 * @brief Undefined behaviour stops the harness instead of being reported and stepped over: a child process of it
 *        that overflows a signed integer ends with a failure.
 */
KERNEL_TEST(undefined_behaviour_stops_the_harness)
{
    const int status = undefined_behaviour_overflowing_child();

    kernel_test_check(test, status != -1, "the child process runs");
    kernel_test_check(test, status != -1 && !(WIFEXITED(status) && WEXITSTATUS(status) == 0),
                      "the signed overflow ends the child with a failure, not with exit status 0");
}
