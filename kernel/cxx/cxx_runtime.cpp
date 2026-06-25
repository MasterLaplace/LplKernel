/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Freestanding C++ runtime support (operator new/delete + Itanium C++ ABI stubs)
**
** This translation unit provides the minimal C++ runtime the freestanding
** engine module (libengine) needs to link into the kernel image. There is no
** hosted C++ runtime: heap operators route to the kernel allocator (kmalloc /
** kfree) and the ABI personality hooks are reduced to kernel-appropriate
** behaviour.
**
** Build contract (enforced by the libengine / kernel build):
**   -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
** so no exception-throwing, RTTI or guarded-static-init symbols are emitted.
**
** Intentionally includes no <cstddef>/<new> headers: those belong to a hosted
** libstdc++ that is not present. Sizes use the compiler-intrinsic __SIZE_TYPE__
** (identical to std::size_t) so the definitions match the implicit
** declarations of the global operators.
*/

extern "C" {
void *kmalloc(__SIZE_TYPE__ size);
void kfree(void *pointer);
void asmutils_disable_interrupts(void);
void asmutils_halt(void);
}

namespace {

using kernel_size_t = __SIZE_TYPE__;

/* Marker stored immediately before an over-aligned block so the matching
   aligned operator delete can recover the original kmalloc pointer. */
struct AlignedAllocationHeader {
    void *base_pointer;
};

/*
** Fatal, non-recoverable C++ runtime error (out of memory, pure virtual call).
**
** Declared weak so a later kernel build can override it with a logging panic;
** the default simply masks interrupts and halts forever. Never returns.
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

/* Minimal std declarations matching the operator new/delete signatures. */
namespace std {
enum class align_val_t : __SIZE_TYPE__ {};
struct nothrow_t {
    explicit nothrow_t() = default;
};
}

/* ---- Non-aligned operators ---- */

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

/* Sized deletes (C++14): the kernel heap recovers the size from its own block
   header, so the hint is ignored. */
void operator delete(void *pointer, kernel_size_t) noexcept
{
    kfree(pointer);
}

void operator delete[](void *pointer, kernel_size_t) noexcept
{
    kfree(pointer);
}

/* ---- nothrow operators ---- */

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

/* ---- Over-aligned operators (C++17): support alignas(N) types ---- */

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

/* ---- Itanium C++ ABI hooks ---- */

extern "C" {

/* Called if a pure virtual function is ever invoked: a hard logic error. */
[[noreturn]] void __cxa_pure_virtual(void)
{
    kernel_cxx_fatal();
}

/*
** Registration of static-object destructors at "program exit". The kernel does
** not exit, so static destructors never need to run: record nothing and report
** success so static initialization proceeds.
*/
int __cxa_atexit(void (*)(void *), void *, void *)
{
    return 0;
}

/* Note: __dso_handle is provided by the toolchain's crtbegin.o (linked into the
   kernel image), so it is intentionally NOT defined here to avoid a duplicate
   symbol. */
}
