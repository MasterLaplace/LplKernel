/*
** LplKernel
** libkxx/include/kstd/inplace_function.hpp
**
** Fixed-capacity, heap-free std::function replacement. Stores any callable that
** fits in an inline byte buffer (default 32 bytes) and erases its type behind a
** vtable of function pointers. Because it never allocates, it is safe in the
** kernel and deterministic — the engine's undo/redo and event callbacks bind to
** it instead of std::function. A callable that does not fit is a compile error.
**
** Modelled on the proposed std::inplace_function (P0792). Move-only is avoided:
** the target must be copy-constructible (matches engine command/callback use).
*/

#ifndef KSTD_INPLACE_FUNCTION_HPP_
#define KSTD_INPLACE_FUNCTION_HPP_

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include <kstd/support.hpp>

namespace kstd {

// NOTE on Capacity and nesting: the inline buffer holds the target callable; the
// vtable pointer is stored separately, so sizeof(inplace_function) == Capacity +
// sizeof(void*). Consequently a callable that *captures* an inplace_function of the
// same Capacity does NOT fit (it is larger by the vtable pointer). Patterns that
// re-wrap a function inside a lambda stored in another function (e.g. EventBus)
// must give the OUTER function an explicitly larger Capacity. The default (64)
// covers typical engine command/callback closures.
template <typename Signature, std::size_t Capacity = 64u, std::size_t Alignment = alignof(void *)>
class inplace_function;

template <typename Return, typename... Args, std::size_t Capacity, std::size_t Alignment>
class inplace_function<Return(Args...), Capacity, Alignment> {
public:
    inplace_function() noexcept = default;

    inplace_function(decltype(nullptr)) noexcept {}

    template <typename Callable,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<Callable>, inplace_function> &&
                                          std::is_invocable_r_v<Return, std::decay_t<Callable> &, Args...>>>
    inplace_function(Callable &&callable)
    {
        using Target = std::decay_t<Callable>;
        static_assert(sizeof(Target) <= Capacity, "kstd::inplace_function: callable too large for inline buffer");
        static_assert(alignof(Target) <= Alignment, "kstd::inplace_function: callable over-aligned for inline buffer");

        ::new (static_cast<void *>(&_storage)) Target(std::forward<Callable>(callable));
        _vtable = &vtable_for<Target>;
    }

    inplace_function(const inplace_function &other)
    {
        if (other._vtable != nullptr)
        {
            other._vtable->copy(&other._storage, &_storage);
            _vtable = other._vtable;
        }
    }

    inplace_function(inplace_function &&other) noexcept
    {
        if (other._vtable != nullptr)
        {
            other._vtable->move(&other._storage, &_storage);
            _vtable = other._vtable;
            other.reset();
        }
    }

    inplace_function &operator=(const inplace_function &other)
    {
        if (this != &other)
        {
            reset();
            if (other._vtable != nullptr)
            {
                other._vtable->copy(&other._storage, &_storage);
                _vtable = other._vtable;
            }
        }
        return *this;
    }

    inplace_function &operator=(inplace_function &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            if (other._vtable != nullptr)
            {
                other._vtable->move(&other._storage, &_storage);
                _vtable = other._vtable;
                other.reset();
            }
        }
        return *this;
    }

    ~inplace_function() { reset(); }

    [[nodiscard]] explicit operator bool() const noexcept { return _vtable != nullptr; }

    Return operator()(Args... args) const
    {
        if (_vtable == nullptr)
            kstd::fatal("kstd::inplace_function: call to empty function");
        return _vtable->invoke(&_storage, std::forward<Args>(args)...);
    }

private:
    struct VTable {
        Return (*invoke)(const void *, Args &&...);
        void (*copy)(const void *, void *);
        void (*move)(void *, void *);
        void (*destroy)(void *);
    };

    template <typename Target>
    static Return invoke_impl(const void *storage, Args &&...args)
    {
        Target &target = *const_cast<Target *>(static_cast<const Target *>(storage));
        return static_cast<Return>(target(std::forward<Args>(args)...));
    }

    template <typename Target>
    static void copy_impl(const void *source, void *destination)
    {
        ::new (destination) Target(*static_cast<const Target *>(source));
    }

    template <typename Target>
    static void move_impl(void *source, void *destination)
    {
        ::new (destination) Target(std::move(*static_cast<Target *>(source)));
    }

    template <typename Target>
    static void destroy_impl(void *storage)
    {
        static_cast<Target *>(storage)->~Target();
    }

    template <typename Target>
    static constexpr VTable vtable_for = {&invoke_impl<Target>, &copy_impl<Target>, &move_impl<Target>,
                                          &destroy_impl<Target>};

    void reset() noexcept
    {
        if (_vtable != nullptr)
        {
            _vtable->destroy(&_storage);
            _vtable = nullptr;
        }
    }

    alignas(Alignment) std::byte _storage[Capacity];
    const VTable *_vtable = nullptr;
};

} // namespace kstd

#endif // KSTD_INPLACE_FUNCTION_HPP_
