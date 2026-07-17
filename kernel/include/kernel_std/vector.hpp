/*
** LplKernel
** kernel/include/kernel_std/vector.hpp
**
** Freestanding, exception-free std::vector work-alike for the engine module.
** Contiguous storage, geometric growth, allocator-parameterised. Out-of-memory
** and out-of-range route to kstd::fatal() (there is no throwing in the kernel).
**
** Intentionally NOT a drop-in for every std::vector corner (no allocator
** propagation traits, no incomplete-type support): it covers the engine's use
** (push/emplace/reserve/index/iterate/clear) with deterministic behaviour.
*/

#ifndef KERNEL_STD_VECTOR_HPP_
#define KERNEL_STD_VECTOR_HPP_

#include <cstddef>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

#include <kernel_std/allocator.hpp>
#include <kernel_std/support.hpp>

namespace kstd {

template <typename T, typename Allocator = KernelAllocator<T>>
class vector {
public:
    using value_type = T;
    using allocator_type = Allocator;
    using size_type = std::size_t;
    using reference = T &;
    using const_reference = const T &;
    using pointer = T *;
    using const_pointer = const T *;
    using iterator = T *;
    using const_iterator = const T *;

    vector() noexcept = default;

    explicit vector(const Allocator &allocator) noexcept : _allocator(allocator) {}

    explicit vector(size_type count, const T &value = T(), const Allocator &allocator = Allocator())
        : _allocator(allocator)
    {
        reserve(count);
        for (size_type i = 0u; i < count; ++i)
            emplace_back(value);
    }

    vector(std::initializer_list<T> init, const Allocator &allocator = Allocator()) : _allocator(allocator)
    {
        reserve(init.size());
        for (const T &value : init)
            emplace_back(value);
    }

    // Iterator-range constructor. The enable_if disambiguates from the
    // (count, value) constructor when called with two integral arguments.
    template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    vector(InputIt first, InputIt last, const Allocator &allocator = Allocator()) : _allocator(allocator)
    {
        for (; first != last; ++first)
            emplace_back(*first);
    }

    vector(const vector &other) : _allocator(other._allocator)
    {
        reserve(other._size);
        for (size_type i = 0u; i < other._size; ++i)
            emplace_back(other._data[i]);
    }

    vector(vector &&other) noexcept
        : _data(other._data), _size(other._size), _capacity(other._capacity), _allocator(other._allocator)
    {
        other._data = nullptr;
        other._size = 0u;
        other._capacity = 0u;
    }

    vector &operator=(const vector &other)
    {
        if (this == &other)
            return *this;
        clear();
        reserve(other._size);
        for (size_type i = 0u; i < other._size; ++i)
            emplace_back(other._data[i]);
        return *this;
    }

    vector &operator=(vector &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy_and_free();
        _data = other._data;
        _size = other._size;
        _capacity = other._capacity;
        _allocator = other._allocator;
        other._data = nullptr;
        other._size = 0u;
        other._capacity = 0u;
        return *this;
    }

    ~vector() { destroy_and_free(); }

    [[nodiscard]] size_type size() const noexcept { return _size; }
    [[nodiscard]] size_type capacity() const noexcept { return _capacity; }
    [[nodiscard]] bool empty() const noexcept { return _size == 0u; }

    [[nodiscard]] reference operator[](size_type index) noexcept { return _data[index]; }
    [[nodiscard]] const_reference operator[](size_type index) const noexcept { return _data[index]; }

    [[nodiscard]] reference at(size_type index)
    {
        if (index >= _size)
            kstd::fatal("kstd::vector::at: index out of range");
        return _data[index];
    }
    [[nodiscard]] const_reference at(size_type index) const
    {
        if (index >= _size)
            kstd::fatal("kstd::vector::at: index out of range");
        return _data[index];
    }

    [[nodiscard]] reference front() noexcept { return _data[0]; }
    [[nodiscard]] const_reference front() const noexcept { return _data[0]; }
    [[nodiscard]] reference back() noexcept { return _data[_size - 1u]; }
    [[nodiscard]] const_reference back() const noexcept { return _data[_size - 1u]; }

    [[nodiscard]] pointer data() noexcept { return _data; }
    [[nodiscard]] const_pointer data() const noexcept { return _data; }

    [[nodiscard]] iterator begin() noexcept { return _data; }
    [[nodiscard]] iterator end() noexcept { return _data + _size; }
    [[nodiscard]] const_iterator begin() const noexcept { return _data; }
    [[nodiscard]] const_iterator end() const noexcept { return _data + _size; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return _data; }
    [[nodiscard]] const_iterator cend() const noexcept { return _data + _size; }

    void reserve(size_type new_capacity)
    {
        if (new_capacity <= _capacity)
            return;
        reallocate(new_capacity);
    }

    void push_back(const T &value) { emplace_back(value); }
    void push_back(T &&value) { emplace_back(std::move(value)); }

    template <typename... Args>
    reference emplace_back(Args &&...args)
    {
        if (_size == _capacity)
            reallocate(_capacity == 0u ? 1u : _capacity * 2u);
        T *const slot = _data + _size;
        ::new (static_cast<void *>(slot)) T(std::forward<Args>(args)...);
        ++_size;
        return *slot;
    }

    void pop_back() noexcept
    {
        --_size;
        _data[_size].~T();
    }

    // Erase one element, shifting the tail down. Returns an iterator to the
    // element that followed the erased one (end() if the last was erased).
    iterator erase(iterator position)
    {
        for (iterator current = position; current + 1 != end(); ++current)
            *current = std::move(*(current + 1));
        --_size;
        _data[_size].~T();
        return position;
    }

    void clear() noexcept
    {
        for (size_type i = 0u; i < _size; ++i)
            _data[i].~T();
        _size = 0u;
    }

    void swap(vector &other) noexcept
    {
        T *const data = _data;
        const size_type size = _size;
        const size_type capacity = _capacity;
        _data = other._data;
        _size = other._size;
        _capacity = other._capacity;
        other._data = data;
        other._size = size;
        other._capacity = capacity;
    }

    void resize(size_type new_size, const T &value = T())
    {
        if (new_size < _size)
        {
            for (size_type i = new_size; i < _size; ++i)
                _data[i].~T();
            _size = new_size;
            return;
        }
        reserve(new_size);
        while (_size < new_size)
            emplace_back(value);
    }

private:
    void reallocate(size_type new_capacity)
    {
        T *const fresh = _allocator.allocate(new_capacity);
        /* Honour the out-of-memory contract stated at the top of this file.
           Without this the failure is silent and far worse than a halt: _data
           would be set to nullptr while _capacity claimed the requested size,
           so the next emplace_back placement-news through a null pointer, and
           resize()'s `while (_size < new_size) emplace_back(...)` never
           terminates because the growth never takes. */
        if (fresh == nullptr)
            fatal("kstd::vector: out of memory growing storage");

        for (size_type i = 0u; i < _size; ++i)
        {
            ::new (static_cast<void *>(fresh + i)) T(std::move_if_noexcept(_data[i]));
            _data[i].~T();
        }
        if (_data != nullptr)
            _allocator.deallocate(_data, _capacity);
        _data = fresh;
        _capacity = new_capacity;
    }

    void destroy_and_free() noexcept
    {
        clear();
        if (_data != nullptr)
            _allocator.deallocate(_data, _capacity);
        _data = nullptr;
        _capacity = 0u;
    }

    T *_data = nullptr;
    size_type _size = 0u;
    size_type _capacity = 0u;
    [[no_unique_address]] Allocator _allocator{};
};

} // namespace kstd

#endif // KERNEL_STD_VECTOR_HPP_
