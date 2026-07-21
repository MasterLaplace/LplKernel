/*
** LplKernel
** libkxx/include/kstd/support.hpp
**
** kstd shared support: the freestanding C++ container subset the
** engine module (libengine) needs but that the freestanding libstdc++ does not
** provide (<vector>, <string>, <unordered_map>). Everything here is built with
**   -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
** so there is no throwing: a contract violation (out of memory, out of range)
** routes to kstd::fatal(), which halts the machine.
**
** Layering rule: kstd depends ONLY on the freestanding libstdc++ subset
** and on this support layer. It must NOT include any engine (lpl) header, so
** the kernel can build it independently of LplPlugin.
*/

#ifndef KSTD_SUPPORT_HPP_
#define KSTD_SUPPORT_HPP_

#include <cstddef>

namespace kstd {

/*
** Non-recoverable kstd contract violation (OOM, out-of-range, bad state).
** Implemented in libkxx/src/support.cpp over the kernel's halt primitives.
** Never returns.
*/
[[noreturn]] void fatal(const char *reason) noexcept;

} // namespace kstd

#endif // KSTD_SUPPORT_HPP_
