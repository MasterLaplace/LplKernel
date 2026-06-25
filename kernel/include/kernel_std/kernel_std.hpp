/*
** LplKernel
** kernel/include/kernel_std/kernel_std.hpp
**
** kernel_std (kstd) umbrella — the freestanding C++ standard-library subset the
** engine module (libengine) links against in the kernel target.
**
** The split, established empirically against the i686-elf gcc 14.2 freestanding
** libstdc++ (see the i686-cpp-toolchain-facts memory):
**
**   Provided directly by the freestanding libstdc++ (just #include <...>):
**     <type_traits> <concepts> <bit> <span> <array> <bitset> <optional>
**     <expected> <utility> <tuple> <cstddef> <cstdint> <cstdlib> <limits>
**     <new> <initializer_list> <source_location> <atomic> <string_view>
**     <ratio> <compare> <coroutine> <functional> <memory> <algorithm>
**     <numbers> <numeric>
**
**   Provided here by kernel_std (NOT in the freestanding subset):
**     <vector>        -> kstd::vector
**     <string>        -> kstd::string
**     <unordered_map> -> kstd::unordered_map
**     <mutex>/<thread> (locking only) -> kstd::mutex / lock_guard / unique_lock
**     std::function   -> kstd::inplace_function (heap-free, deterministic)
**
**   Routed to the kernel C library / kernel facilities by the engine's lpl/std
**   umbrella (not here, since they need libk / serial / CORDIC):
**     <cstring> -> <string.h>   (libk: memcpy/memset/strlen...)
**     <cstdio>  -> kernel serial logging
**     <cmath>   -> <math.h> / CORDIC LUTs (authoritative trig must use CORDIC)
**     <chrono>  -> clock_backend (clock_* + rdtsc)
**
** This header is the kernel-side foundation; the engine's lpl/std/* umbrella
** (in LplPlugin, kernel target) maps the engine's std:: call sites onto these.
*/

#ifndef KERNEL_STD_KERNEL_STD_HPP_
#define KERNEL_STD_KERNEL_STD_HPP_

#include <kernel_std/allocator.hpp>
#include <kernel_std/inplace_function.hpp>
#include <kernel_std/mutex.hpp>
#include <kernel_std/string.hpp>
#include <kernel_std/support.hpp>
#include <kernel_std/unordered_map.hpp>
#include <kernel_std/vector.hpp>

#endif // KERNEL_STD_KERNEL_STD_HPP_
