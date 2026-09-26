/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file support.hpp
 * @brief kstd shared support: the fatal-error sink and the layering rule.
 *
 * kstd is the freestanding C++ container subset the engine module (libengine)
 * needs but that the freestanding libstdc++ does not provide (<vector>, <string>,
 * <unordered_map>). Everything is built with
 *   -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
 * so there is no throwing: a contract violation (out of memory, out of range) routes
 * to kstd::fatal(), which halts the machine. The sink is out of line, in
 * libkxx/src/support.cpp, so the dependency on the kernel halt primitives lives in
 * exactly one translation unit and the kstd headers stay self-contained.
 *
 * Layering rule: kstd depends ONLY on the freestanding libstdc++ subset and on this
 * support layer. It must NOT include any engine (lpl) header, so the kernel can build
 * it independently of LplPlugin.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-25
 **************************************************************************/

#ifndef KSTD_SUPPORT_HPP_
#define KSTD_SUPPORT_HPP_

#include <cstddef>

namespace kstd {

/**
 * @brief Non-recoverable kstd contract violation (OOM, out-of-range, bad state).
 *
 * Implemented in libkxx/src/support.cpp over the kernel's halt primitives. Never returns.
 */
[[noreturn]] void fatal(const char *reason) noexcept;

} // namespace kstd

#endif // KSTD_SUPPORT_HPP_
