/*
** LplKernel
** kernel/include/kernel_std/mutex.hpp
**
** Single-threaded kernel mutex facade. The kernel is single-threaded first (per
** the convergence plan: a job_system interface with an inline executor), so a
** lock is a no-op that preserves the std::mutex / std::lock_guard call sites in
** the engine without pulling in <thread>. When a real kernel scheduler lands,
** these route to a kernel spinlock behind the same interface.
*/

#ifndef KERNEL_STD_MUTEX_HPP_
#define KERNEL_STD_MUTEX_HPP_

namespace kstd {

class mutex {
public:
    constexpr mutex() noexcept = default;
    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;

    void lock() noexcept {}
    bool try_lock() noexcept { return true; }
    void unlock() noexcept {}
};

// Recursive variant: identical no-op semantics under single-threaded execution.
class recursive_mutex {
public:
    constexpr recursive_mutex() noexcept = default;
    recursive_mutex(const recursive_mutex &) = delete;
    recursive_mutex &operator=(const recursive_mutex &) = delete;

    void lock() noexcept {}
    bool try_lock() noexcept { return true; }
    void unlock() noexcept {}
};

template <typename Mutex>
class lock_guard {
public:
    explicit lock_guard(Mutex &mutex) noexcept : _mutex(mutex) { _mutex.lock(); }
    ~lock_guard() { _mutex.unlock(); }

    lock_guard(const lock_guard &) = delete;
    lock_guard &operator=(const lock_guard &) = delete;

private:
    Mutex &_mutex;
};

template <typename Mutex>
class unique_lock {
public:
    unique_lock() noexcept = default;
    explicit unique_lock(Mutex &mutex) noexcept : _mutex(&mutex), _owns(true) { _mutex->lock(); }
    ~unique_lock()
    {
        if (_owns && _mutex != nullptr)
            _mutex->unlock();
    }

    unique_lock(unique_lock &&other) noexcept : _mutex(other._mutex), _owns(other._owns)
    {
        other._mutex = nullptr;
        other._owns = false;
    }

    unique_lock &operator=(unique_lock &&other) noexcept
    {
        if (this != &other)
        {
            if (_owns && _mutex != nullptr)
                _mutex->unlock();
            _mutex = other._mutex;
            _owns = other._owns;
            other._mutex = nullptr;
            other._owns = false;
        }
        return *this;
    }

    void lock() noexcept
    {
        _mutex->lock();
        _owns = true;
    }
    void unlock() noexcept
    {
        _mutex->unlock();
        _owns = false;
    }
    [[nodiscard]] bool owns_lock() const noexcept { return _owns; }

private:
    Mutex *_mutex = nullptr;
    bool _owns = false;
};

} // namespace kstd

#endif // KERNEL_STD_MUTEX_HPP_
