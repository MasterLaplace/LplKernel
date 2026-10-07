#include <kernel/drivers/serial.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(serial, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The divisor read back from the port is the one its speed asks for.
 *
 * @details QEMU's 16550 ignores the divisor for its own timing but returns it on read, so this is
 *          the one way a boot under emulation can see the speed a real UART would run at. A divisor
 *          whose high byte repeats the low one turns 9600 baud into about 37.
 */
KERNEL_TEST(divisor_matches_the_speed)
{
    const uint32_t expected = (com1.speed != 0u) ? (BASE_SERIAL_SPEED / com1.speed) : 0u;
    const uint16_t divisor = serial_read_divisor(&com1);

    kernel_test_check(test, com1.initialized != 0u, "the port is initialised");
    kernel_test_check(test, expected != 0u, "the port has a speed");
    kernel_test_check(test, divisor == expected, "the divisor read back is the one the speed asks for");
    kernel_test_measure(test, "baud", com1.speed);
    kernel_test_measure(test, "divisor", divisor);
}
