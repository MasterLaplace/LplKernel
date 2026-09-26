#include <kstd/support.hpp>

extern "C" {
void asmutils_disable_interrupts(void);
void asmutils_halt(void);
}

namespace kstd {

/**
 * @brief Halts on a kstd contract violation.
 *
 * @details A contract violation is a hard logic/resource error with no safe continuation in
 *          -fno-exceptions code: interrupts are masked and the processor halts forever.
 *
 * @note The reason string is accepted for a future serial-logging panic but is currently
 *       unused, since kstd must not depend on a particular logging facility.
 *
 * @param reason What was violated.
 */
[[noreturn]] void fatal(const char *reason) noexcept
{
    (void)reason;
    asmutils_disable_interrupts();
    for (;;)
        asmutils_halt();
}

} // namespace kstd
