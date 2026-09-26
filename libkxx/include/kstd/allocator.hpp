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
 * @file allocator.hpp
 * @brief Default allocator for kstd containers.
 *
 * Routes through the global operator new/delete, which the kernel C++ runtime
 * (libkxx/src/cxx_runtime.cpp) maps onto kmalloc/kfree. Containers are still
 * parameterised on the allocator type, so the engine's lpl/std umbrella can
 * substitute an adapter over lpl::memory::IAllocator (arena / pool / pinned) without
 * kstd knowing anything about engine types.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-25
 **************************************************************************/

#ifndef KSTD_ALLOCATOR_HPP_
#define KSTD_ALLOCATOR_HPP_

#include <cstddef>
#include <new>

#include <kstd/support.hpp>

namespace kstd {

/**
 * @brief Minimal C++ Allocator-named-requirement type over operator new/delete.
 *
 * Stateless: all instances compare equal, so containers may freely move blocks between allocator
 * instances of the same type.
 */
template <typename T>
class KernelAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    constexpr KernelAllocator() noexcept = default;

    template <typename U>
    constexpr KernelAllocator(const KernelAllocator<U> &) noexcept
    {
    }

    [[nodiscard]] T *allocate(size_type count)
    {
        if (count == 0u)
            return nullptr;
        if (count > (static_cast<size_type>(-1) / sizeof(T)))
            kstd::fatal("kstd::KernelAllocator: allocation size overflow");

        void *const block = ::operator new(count * sizeof(T), std::align_val_t{alignof(T)});
        return static_cast<T *>(block);
    }

    void deallocate(T *pointer, size_type) noexcept
    {
        ::operator delete(pointer, std::align_val_t{alignof(T)});
    }

    template <typename U>
    struct rebind {
        using other = KernelAllocator<U>;
    };
};

template <typename T, typename U>
constexpr bool operator==(const KernelAllocator<T> &, const KernelAllocator<U> &) noexcept
{
    return true;
}

template <typename T, typename U>
constexpr bool operator!=(const KernelAllocator<T> &, const KernelAllocator<U> &) noexcept
{
    return false;
}

} // namespace kstd

#endif // KSTD_ALLOCATOR_HPP_
