/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file kstd.hpp
 * @brief kstd umbrella: the freestanding C++ standard-library subset the engine links against.
 *
 * The split, established empirically against the i686-elf gcc 14.2 freestanding
 * libstdc++:
 *
 *   Provided directly by the freestanding libstdc++ (just #include <...>):
 *     <type_traits> <concepts> <bit> <span> <array> <bitset> <optional>
 *     <expected> <utility> <tuple> <cstddef> <cstdint> <cstdlib> <limits>
 *     <new> <initializer_list> <source_location> <atomic> <string_view>
 *     <ratio> <compare> <coroutine> <functional> <memory> <algorithm>
 *     <numbers> <numeric>
 *
 *   Provided here by kstd (NOT in the freestanding subset):
 *     <vector>        -> kstd::vector
 *     <string>        -> kstd::string
 *     <unordered_map> -> kstd::unordered_map
 *     <mutex>/<thread> (locking only) -> kstd::mutex / lock_guard / unique_lock
 *     std::function   -> kstd::inplace_function (heap-free, deterministic)
 *
 *   Routed to the kernel C library / kernel facilities by the engine's lpl/std
 *   umbrella (not here, since they need libk / serial / CORDIC):
 *     <cstring> -> <string.h>   (libk: memcpy/memset/strlen...)
 *     <cstdio>  -> kernel serial logging
 *     <cmath>   -> <math.h> / CORDIC LUTs (authoritative trig must use CORDIC)
 *     <chrono>  -> clock_backend (clock_* + rdtsc)
 *
 * This header is the kernel-side foundation; the engine's lpl/std/* umbrella (in
 * LplPlugin, kernel target) maps the engine's std:: call sites onto these.
 *
 * Underneath sits the C++ runtime itself, libkxx/src/cxx_runtime.cpp: global
 * operator new/delete routed to kmalloc/kfree and the Itanium ABI hooks reduced to
 * kernel-appropriate behaviour. It is built -ffreestanding -fno-exceptions -fno-rtti
 * -fno-threadsafe-statics, so no exception-throwing, RTTI or guarded-static-init
 * symbol is ever emitted, and it includes no <cstddef>/<new>: sizes use the
 * compiler-intrinsic __SIZE_TYPE__ so the definitions match the implicit
 * declarations of the global operators.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-25
 **************************************************************************/

#ifndef KSTD_KSTD_HPP_
#define KSTD_KSTD_HPP_

#include <kstd/allocator.hpp>
#include <kstd/inplace_function.hpp>
#include <kstd/mutex.hpp>
#include <kstd/string.hpp>
#include <kstd/support.hpp>
#include <kstd/unordered_map.hpp>
#include <kstd/vector.hpp>

#endif // KSTD_KSTD_HPP_
