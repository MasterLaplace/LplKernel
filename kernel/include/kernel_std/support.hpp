/*
** LplKernel
** kernel/include/kernel_std/support.hpp
**
** kernel_std (kstd) shared support: the freestanding C++ container subset the
** engine module (libengine) needs but that the freestanding libstdc++ does not
** provide (<vector>, <string>, <unordered_map>). Everything here is built with
**   -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
** so there is no throwing: a contract violation (out of memory, out of range)
** routes to kstd::fatal(), which halts the machine.
**
** Layering rule: kernel_std depends ONLY on the freestanding libstdc++ subset
** and on this support layer. It must NOT include any engine (lpl) header, so
** the kernel can build it independently of LplPlugin.
*/

#ifndef KERNEL_STD_SUPPORT_HPP_
#define KERNEL_STD_SUPPORT_HPP_

#include <cstddef>

namespace kstd {

/*
** Non-recoverable kernel_std contract violation (OOM, out-of-range, bad state).
** Implemented in kernel_std/support.cpp over the kernel's halt primitives.
** Never returns.
*/
[[noreturn]] void fatal(const char *reason) noexcept;

} // namespace kstd

#endif // KERNEL_STD_SUPPORT_HPP_
