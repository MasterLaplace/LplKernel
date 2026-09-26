extern "C" {
void *kmalloc(__SIZE_TYPE__ size);
void kfree(void *pointer);
void asmutils_disable_interrupts(void);
void asmutils_halt(void);
}

namespace {

using kernel_size_t = __SIZE_TYPE__;

/**
 * @brief Marker stored immediately before an over-aligned block.
 *
 * @details Lets the matching aligned operator delete recover the original kmalloc pointer.
 */
struct AlignedAllocationHeader {
    void *base_pointer;
};

/**
 * @brief Fatal, non-recoverable C++ runtime error (out of memory, pure virtual call).
 *
 * @details Masks interrupts and halts forever. Never returns.
 */
[[noreturn]] void kernel_cxx_fatal(void)
{
    asmutils_disable_interrupts();
    for (;;)
        asmutils_halt();
}

void *allocate_aligned(kernel_size_t size, kernel_size_t alignment)
{
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);

    const kernel_size_t overhead = sizeof(AlignedAllocationHeader) + alignment - 1u;
    void *const base = kmalloc(size + overhead);

    if (base == nullptr)
        kernel_cxx_fatal();

    kernel_size_t raw = reinterpret_cast<kernel_size_t>(base) + sizeof(AlignedAllocationHeader);
    raw = (raw + (alignment - 1u)) & ~(alignment - 1u);

    void *const aligned = reinterpret_cast<void *>(raw);
    (reinterpret_cast<AlignedAllocationHeader *>(aligned))[-1].base_pointer = base;
    return aligned;
}

void free_aligned(void *pointer) noexcept
{
    if (pointer == nullptr)
        return;
    kfree((reinterpret_cast<AlignedAllocationHeader *>(pointer))[-1].base_pointer);
}

} // namespace

/** Minimal std declarations matching the operator new/delete signatures. */
namespace std {
enum class align_val_t : __SIZE_TYPE__ {};
struct nothrow_t {
    explicit nothrow_t() = default;
};

/**
 * @brief Halts: a reached terminate is an unrecoverable logic error.
 *
 * @details Referenced by the freestanding libstdc++ at every noexcept boundary (via
 *          std::__terminate). With -fno-exceptions there is nothing to unwind.
 */
[[noreturn]] void terminate() noexcept;
} // namespace std

[[noreturn]] void std::terminate() noexcept { kernel_cxx_fatal(); }

void *operator new(kernel_size_t size)
{
    void *const pointer = kmalloc(size == 0u ? 1u : size);
    if (pointer == nullptr)
        kernel_cxx_fatal();
    return pointer;
}

void *operator new[](kernel_size_t size)
{
    return ::operator new(size);
}

void operator delete(void *pointer) noexcept
{
    kfree(pointer);
}

void operator delete[](void *pointer) noexcept
{
    kfree(pointer);
}

/**
 * @brief Sized delete (C++14).
 *
 * @note The kernel heap recovers the size from its own block header, so the hint is
 *       ignored, here and in the array form.
 *
 * @param pointer Block to free.
 */
void operator delete(void *pointer, kernel_size_t) noexcept
{
    kfree(pointer);
}

void operator delete[](void *pointer, kernel_size_t) noexcept
{
    kfree(pointer);
}

void *operator new(kernel_size_t size, const std::nothrow_t &) noexcept
{
    return kmalloc(size == 0u ? 1u : size);
}

void *operator new[](kernel_size_t size, const std::nothrow_t &) noexcept
{
    return kmalloc(size == 0u ? 1u : size);
}

void operator delete(void *pointer, const std::nothrow_t &) noexcept
{
    kfree(pointer);
}

void operator delete[](void *pointer, const std::nothrow_t &) noexcept
{
    kfree(pointer);
}

void *operator new(kernel_size_t size, std::align_val_t alignment)
{
    return allocate_aligned(size == 0u ? 1u : size, static_cast<kernel_size_t>(alignment));
}

void *operator new[](kernel_size_t size, std::align_val_t alignment)
{
    return allocate_aligned(size == 0u ? 1u : size, static_cast<kernel_size_t>(alignment));
}

void operator delete(void *pointer, std::align_val_t) noexcept
{
    free_aligned(pointer);
}

void operator delete[](void *pointer, std::align_val_t) noexcept
{
    free_aligned(pointer);
}

void operator delete(void *pointer, kernel_size_t, std::align_val_t) noexcept
{
    free_aligned(pointer);
}

void operator delete[](void *pointer, kernel_size_t, std::align_val_t) noexcept
{
    free_aligned(pointer);
}

extern "C" {

/**
 * @brief Called if a pure virtual function is ever invoked: a hard logic error.
 */
[[noreturn]] void __cxa_pure_virtual(void)
{
    kernel_cxx_fatal();
}

/**
 * @brief Registers a static object's destructor for "program exit".
 *
 * @details The kernel does not exit, so static destructors never need to run: nothing is
 *          recorded, and success is reported so static initialisation proceeds.
 *
 * @note __dso_handle is provided by the toolchain's crtbegin.o, linked into the kernel image,
 *       so it is deliberately not defined here: a definition would be a duplicate symbol.
 *
 * @return 0, always.
 */
int __cxa_atexit(void (*)(void *), void *, void *)
{
    return 0;
}
}
