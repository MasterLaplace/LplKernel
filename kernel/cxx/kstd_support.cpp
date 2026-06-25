/*
** LplKernel
** kernel/cxx/kstd_support.cpp
**
** Out-of-line support for kernel_std: the fatal-error sink. Kept in a .cpp (not
** inline in the header) so the dependency on the kernel halt primitives lives in
** exactly one translation unit and kernel_std headers stay self-contained.
*/

#include <kernel_std/support.hpp>

extern "C" {
void asmutils_disable_interrupts(void);
void asmutils_halt(void);
}

namespace kstd {

/*
** A kernel_std contract violation is a hard logic/resource error with no safe
** continuation in -fno-exceptions code. Mask interrupts and halt forever. The
** reason string is accepted for a future serial-logging panic but is currently
** unused (kernel_std must not depend on a particular logging facility).
*/
[[noreturn]] void fatal(const char *reason) noexcept
{
    (void)reason;
    asmutils_disable_interrupts();
    for (;;)
        asmutils_halt();
}

} // namespace kstd
