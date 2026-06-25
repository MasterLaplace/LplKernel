/*
** LplKernel
** kernel/include/kernel_std/allocator.hpp
**
** Default allocator for kernel_std containers. Routes through the global
** operator new/delete, which the kernel C++ runtime (kernel/cxx/cxx_runtime.cpp)
** maps onto kmalloc/kfree. Containers are still parameterised on the allocator
** type, so the engine's lpl/std umbrella can substitute an adapter over
** lpl::memory::IAllocator (arena / pool / pinned) without kernel_std knowing
** anything about engine types.
*/

#ifndef KERNEL_STD_ALLOCATOR_HPP_
#define KERNEL_STD_ALLOCATOR_HPP_

#include <cstddef>
#include <new>

#include <kernel_std/support.hpp>

namespace kstd {

/*
** Minimal C++ Allocator-named-requirement type over operator new/delete.
** Stateless: all instances compare equal, so containers may freely move blocks
** between allocator instances of the same type.
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

#endif // KERNEL_STD_ALLOCATOR_HPP_
