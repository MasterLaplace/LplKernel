#include <kstd/support.hpp>

extern "C" {
[[noreturn]] void interrupt_exception_panic(const char *reason, const void *caller);
}

namespace kstd {

/**
 * @brief Halts on a kstd contract violation, through the kernel's panic report.
 *
 * @details A contract violation is a hard logic/resource error with no safe continuation in
 *          -fno-exceptions code: the kernel reports @p reason and the address fatal was called
 *          from on its serial port, masks interrupts and halts forever.
 *
 * @param reason What was violated.
 */
[[noreturn]] void fatal(const char *reason) noexcept { interrupt_exception_panic(reason, __builtin_return_address(0)); }

} // namespace kstd
