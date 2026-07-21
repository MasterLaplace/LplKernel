/*
** LplKernel
** libkxx/include/kstd/string.hpp
**
** Freestanding, exception-free std::string work-alike (char only). Heap-backed
** contiguous storage with a small-buffer optimisation, NUL-terminated, geometric
** growth via the kernel allocator. Out-of-range routes to kstd::fatal().
**
** Covers the engine's text use (construct/append/compare/c_str/index/substr) but
** is deliberately not a full std::basic_string: no traits template, no locale, no
** allocator template (always KernelAllocator) — bounded scope on purpose.
*/

#ifndef KSTD_STRING_HPP_
#define KSTD_STRING_HPP_

#include <cstddef>
#include <new>
#include <string_view>
#include <utility>

#include <kstd/allocator.hpp>
#include <kstd/support.hpp>

namespace kstd {

class string {
public:
    using size_type = std::size_t;
    static constexpr size_type npos = static_cast<size_type>(-1);

    string() noexcept { _inline[0] = '\0'; }

    string(const char *text) { assign(text, c_length(text)); }

    string(const char *text, size_type length) { assign(text, length); }

    string(std::string_view view) { assign(view.data(), view.size()); }

    string(const string &other) { assign(other.data(), other._size); }

    string(string &&other) noexcept { take(other); }

    string &operator=(const string &other)
    {
        if (this != &other)
            assign(other.data(), other._size);
        return *this;
    }

    string &operator=(string &&other) noexcept
    {
        if (this != &other)
        {
            release();
            take(other);
        }
        return *this;
    }

    string &operator=(const char *text)
    {
        assign(text, c_length(text));
        return *this;
    }

    ~string() { release(); }

    [[nodiscard]] size_type size() const noexcept { return _size; }
    [[nodiscard]] size_type length() const noexcept { return _size; }
    [[nodiscard]] size_type capacity() const noexcept { return _capacity; }
    [[nodiscard]] bool empty() const noexcept { return _size == 0u; }

    [[nodiscard]] char *data() noexcept { return is_inline() ? _inline : _heap; }
    [[nodiscard]] const char *data() const noexcept { return is_inline() ? _inline : _heap; }
    [[nodiscard]] const char *c_str() const noexcept { return data(); }

    [[nodiscard]] char &operator[](size_type index) noexcept { return data()[index]; }
    [[nodiscard]] char operator[](size_type index) const noexcept { return data()[index]; }

    [[nodiscard]] char &at(size_type index)
    {
        if (index >= _size)
            kstd::fatal("kstd::string::at: index out of range");
        return data()[index];
    }

    [[nodiscard]] operator std::string_view() const noexcept { return std::string_view(data(), _size); }
    [[nodiscard]] std::string_view view() const noexcept { return std::string_view(data(), _size); }

    [[nodiscard]] char *begin() noexcept { return data(); }
    [[nodiscard]] char *end() noexcept { return data() + _size; }
    [[nodiscard]] const char *begin() const noexcept { return data(); }
    [[nodiscard]] const char *end() const noexcept { return data() + _size; }

    void clear() noexcept
    {
        _size = 0u;
        data()[0] = '\0';
    }

    void reserve(size_type new_capacity)
    {
        if (new_capacity <= _capacity)
            return;
        grow_to(new_capacity);
    }

    void push_back(char character) { append(&character, 1u); }

    string &append(const char *text, size_type length)
    {
        if (length == 0u)
            return *this;
        if (_size + length > _capacity)
            grow_to(_size + length);
        char *const dst = data();
        __builtin_memcpy(dst + _size, text, length);
        _size += length;
        dst[_size] = '\0';
        return *this;
    }

    string &append(const char *text) { return append(text, c_length(text)); }
    string &append(std::string_view view) { return append(view.data(), view.size()); }

    string &operator+=(char character)
    {
        push_back(character);
        return *this;
    }
    string &operator+=(const char *text) { return append(text); }
    string &operator+=(std::string_view view) { return append(view); }

    [[nodiscard]] bool operator==(std::string_view other) const noexcept { return view() == other; }
    [[nodiscard]] bool operator==(const string &other) const noexcept { return view() == other.view(); }

private:
    static constexpr size_type INLINE_CAPACITY = 23u; // 24-byte buffer incl. NUL

    [[nodiscard]] bool is_inline() const noexcept { return _capacity == INLINE_CAPACITY; }

    static size_type c_length(const char *text) noexcept
    {
        size_type length = 0u;
        if (text != nullptr)
            while (text[length] != '\0')
                ++length;
        return length;
    }

    void assign(const char *text, size_type length)
    {
        if (length > _capacity)
            grow_to(length);
        char *const dst = data();
        __builtin_memcpy(dst, text, length);
        _size = length;
        dst[_size] = '\0';
    }

    // Grow heap storage to hold at least `minimum` chars (+1 for NUL), geometric.
    void grow_to(size_type minimum)
    {
        size_type new_capacity = _capacity == 0u ? INLINE_CAPACITY : _capacity;
        while (new_capacity < minimum)
            new_capacity *= 2u;

        char *const fresh = KernelAllocator<char>().allocate(new_capacity + 1u);
        const char *const old = data();
        __builtin_memcpy(fresh, old, _size);
        fresh[_size] = '\0';

        if (!is_inline())
            KernelAllocator<char>().deallocate(_heap, _capacity + 1u);
        _heap = fresh;
        _capacity = new_capacity;
    }

    void release() noexcept
    {
        if (!is_inline())
            KernelAllocator<char>().deallocate(_heap, _capacity + 1u);
    }

    void take(string &other) noexcept
    {
        _size = other._size;
        _capacity = other._capacity;
        if (other.is_inline())
        {
            for (size_type i = 0u; i <= other._size; ++i)
                _inline[i] = other._inline[i];
        }
        else
        {
            _heap = other._heap;
            other._heap = nullptr;
        }
        other._size = 0u;
        other._capacity = INLINE_CAPACITY;
        other._inline[0] = '\0';
    }

    union {
        char _inline[INLINE_CAPACITY + 1u];
        char *_heap;
    };
    size_type _size = 0u;
    size_type _capacity = INLINE_CAPACITY;
};

[[nodiscard]] inline bool operator==(const char *lhs, const string &rhs) noexcept
{
    return rhs == std::string_view(lhs);
}

} // namespace kstd

#endif // KSTD_STRING_HPP_
