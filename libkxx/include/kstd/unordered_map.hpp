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
 * @file unordered_map.hpp
 * @brief Freestanding, exception-free std::unordered_map work-alike.
 *
 * Separate chaining with a power-of-two bucket array and a load-factor-driven
 * rehash. Node storage comes from the kernel allocator; iteration order is
 * bucket-then-insertion and is therefore stable for a given insertion sequence
 * (matters for determinism).
 *
 * Scope: insert/emplace/operator[]/find/erase/contains/at/size/iterate. No node
 * handles, no bucket-interface, no custom max_load_factor tuning — bounded on
 * purpose, like the rest of kstd.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-25
 **************************************************************************/

#ifndef KSTD_UNORDERED_MAP_HPP_
#define KSTD_UNORDERED_MAP_HPP_

#include <cstddef>
#include <functional>
#include <new>
#include <utility>

#include <kstd/allocator.hpp>
#include <kstd/support.hpp>

namespace kstd {

template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class unordered_map {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = std::size_t;

private:
    struct Node {
        value_type entry;
        Node *next;

        template <typename... Args>
        Node(Node *next_node, Args &&...args) : entry(std::forward<Args>(args)...), next(next_node)
        {
        }
    };

    using NodeAllocator = KernelAllocator<Node>;
    using BucketAllocator = KernelAllocator<Node *>;

public:
    class iterator {
    public:
        iterator(Node **bucket, Node **bucket_end, Node *node) noexcept
            : _bucket(bucket), _bucket_end(bucket_end), _node(node)
        {
        }

        value_type &operator*() const noexcept { return _node->entry; }
        value_type *operator->() const noexcept { return &_node->entry; }

        iterator &operator++() noexcept
        {
            _node = _node->next;
            while (_node == nullptr && ++_bucket != _bucket_end)
                _node = *_bucket;
            return *this;
        }

        [[nodiscard]] bool operator==(const iterator &other) const noexcept { return _node == other._node; }
        [[nodiscard]] bool operator!=(const iterator &other) const noexcept { return _node != other._node; }

    private:
        Node **_bucket;
        Node **_bucket_end;
        Node *_node;
    };

    unordered_map() = default;

    unordered_map(const unordered_map &other)
    {
        reserve_buckets(other._bucket_count);
        for (const value_type &entry : const_cast<unordered_map &>(other))
            emplace(entry.first, entry.second);
    }

    unordered_map(unordered_map &&other) noexcept
        : _buckets(other._buckets), _bucket_count(other._bucket_count), _size(other._size)
    {
        other._buckets = nullptr;
        other._bucket_count = 0u;
        other._size = 0u;
    }

    unordered_map &operator=(const unordered_map &other)
    {
        if (this != &other)
        {
            clear_and_free();
            reserve_buckets(other._bucket_count);
            for (const value_type &entry : const_cast<unordered_map &>(other))
                emplace(entry.first, entry.second);
        }
        return *this;
    }

    unordered_map &operator=(unordered_map &&other) noexcept
    {
        if (this != &other)
        {
            clear_and_free();
            _buckets = other._buckets;
            _bucket_count = other._bucket_count;
            _size = other._size;
            other._buckets = nullptr;
            other._bucket_count = 0u;
            other._size = 0u;
        }
        return *this;
    }

    ~unordered_map() { clear_and_free(); }

    [[nodiscard]] size_type size() const noexcept { return _size; }
    [[nodiscard]] bool empty() const noexcept { return _size == 0u; }

    [[nodiscard]] iterator begin() noexcept
    {
        if (_bucket_count == 0u)
            return end();
        Node **bucket = _buckets;
        Node **const bucket_end = _buckets + _bucket_count;
        while (bucket != bucket_end && *bucket == nullptr)
            ++bucket;
        return iterator(bucket, bucket_end, bucket == bucket_end ? nullptr : *bucket);
    }

    [[nodiscard]] iterator end() noexcept
    {
        Node **const bucket_end = _buckets + _bucket_count;
        return iterator(bucket_end, bucket_end, nullptr);
    }

    [[nodiscard]] iterator find(const Key &key) noexcept
    {
        if (_bucket_count == 0u)
            return end();
        const size_type index = bucket_index(key);
        for (Node *node = _buckets[index]; node != nullptr; node = node->next)
            if (KeyEqual{}(node->entry.first, key))
                return iterator(_buckets + index, _buckets + _bucket_count, node);
        return end();
    }

    [[nodiscard]] bool contains(const Key &key) noexcept { return find(key) != end(); }

    [[nodiscard]] Value &at(const Key &key)
    {
        const iterator it = find(key);
        if (it == end())
            kstd::fatal("kstd::unordered_map::at: key not found");
        return it->second;
    }

    Value &operator[](const Key &key)
    {
        ensure_capacity();
        const size_type index = bucket_index(key);
        for (Node *node = _buckets[index]; node != nullptr; node = node->next)
            if (KeyEqual{}(node->entry.first, key))
                return node->entry.second;
        return insert_node(index, key, Value())->entry.second;
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(const Key &key, Args &&...args)
    {
        ensure_capacity();
        const size_type index = bucket_index(key);
        for (Node *node = _buckets[index]; node != nullptr; node = node->next)
            if (KeyEqual{}(node->entry.first, key))
                return {iterator(_buckets + index, _buckets + _bucket_count, node), false};
        Node *const node = insert_node(index, key, std::forward<Args>(args)...);
        return {iterator(_buckets + index, _buckets + _bucket_count, node), true};
    }

    std::pair<iterator, bool> insert(const value_type &entry) { return emplace(entry.first, entry.second); }

    /**
     * @brief Erases the element at @p position (the std::unordered_map node-based contract).
     *
     * @details Advances first, then unlinks: erasing one node leaves every other node's
     *          storage valid, so the advanced iterator stays good.
     *
     * @param position The element to erase.
     * @return An iterator to the element that followed it.
     */
    iterator erase(iterator position)
    {
        iterator next = position;
        ++next;
        erase(position->first);
        return next;
    }

    size_type erase(const Key &key) noexcept
    {
        if (_bucket_count == 0u)
            return 0u;
        const size_type index = bucket_index(key);
        Node *previous = nullptr;
        for (Node *node = _buckets[index]; node != nullptr; previous = node, node = node->next)
        {
            if (KeyEqual{}(node->entry.first, key))
            {
                if (previous == nullptr)
                    _buckets[index] = node->next;
                else
                    previous->next = node->next;
                destroy_node(node);
                --_size;
                return 1u;
            }
        }
        return 0u;
    }

    void clear() noexcept
    {
        for (size_type i = 0u; i < _bucket_count; ++i)
        {
            Node *node = _buckets[i];
            while (node != nullptr)
            {
                Node *const next = node->next;
                destroy_node(node);
                node = next;
            }
            _buckets[i] = nullptr;
        }
        _size = 0u;
    }

private:
    static constexpr size_type INITIAL_BUCKETS = 8u;

    /**
     * @brief The bucket a key hashes to.
     *
     * @note The bucket count is always a power of two, so masking is the modulo.
     *
     * @param key The key.
     * @return Its bucket index.
     */
    [[nodiscard]] size_type bucket_index(const Key &key) const noexcept { return Hash{}(key) & (_bucket_count - 1u); }

    /**
     * @brief Makes room for one more entry, doubling the buckets before the load factor passes 1.
     */
    void ensure_capacity()
    {
        if (_bucket_count == 0u)
            reserve_buckets(INITIAL_BUCKETS);
        else if (_size + 1u > _bucket_count)
            reserve_buckets(_bucket_count * 2u);
    }

    /**
     * @brief Grows the bucket array to at least @p new_bucket_count and rehashes into it.
     *
     * @details Existing nodes are relinked into the new array; the nodes move, their entries
     *          do not.
     *
     * @param new_bucket_count Buckets wanted; a power of two, at least INITIAL_BUCKETS.
     */
    void reserve_buckets(size_type new_bucket_count)
    {
        if (new_bucket_count < INITIAL_BUCKETS)
            new_bucket_count = INITIAL_BUCKETS;
        if (new_bucket_count <= _bucket_count)
            return;

        Node **const fresh = BucketAllocator().allocate(new_bucket_count);
        for (size_type i = 0u; i < new_bucket_count; ++i)
            fresh[i] = nullptr;

        for (size_type i = 0u; i < _bucket_count; ++i)
        {
            Node *node = _buckets[i];
            while (node != nullptr)
            {
                Node *const next = node->next;
                const size_type index = Hash{}(node->entry.first) & (new_bucket_count - 1u);
                node->next = fresh[index];
                fresh[index] = node;
                node = next;
            }
        }

        if (_buckets != nullptr)
            BucketAllocator().deallocate(_buckets, _bucket_count);
        _buckets = fresh;
        _bucket_count = new_bucket_count;
    }

    template <typename... Args>
    Node *insert_node(size_type index, const Key &key, Args &&...args)
    {
        Node *const node = NodeAllocator().allocate(1u);
        ::new (static_cast<void *>(node)) Node(_buckets[index], std::piecewise_construct,
                                               std::forward_as_tuple(key),
                                               std::forward_as_tuple(std::forward<Args>(args)...));
        _buckets[index] = node;
        ++_size;
        return node;
    }

    void destroy_node(Node *node) noexcept
    {
        node->~Node();
        NodeAllocator().deallocate(node, 1u);
    }

    void clear_and_free() noexcept
    {
        clear();
        if (_buckets != nullptr)
            BucketAllocator().deallocate(_buckets, _bucket_count);
        _buckets = nullptr;
        _bucket_count = 0u;
    }

    Node **_buckets = nullptr;
    size_type _bucket_count = 0u;
    size_type _size = 0u;
};

} // namespace kstd

#endif // KSTD_UNORDERED_MAP_HPP_
