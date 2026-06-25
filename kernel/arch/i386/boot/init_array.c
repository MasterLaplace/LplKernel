/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** C++ global constructor / destructor runtime (.init_array ABI)
*/

#include <kernel/boot/init_array.h>

typedef void (*kernel_constructor_function_t)(void);

/*
** Boundary symbols emitted by the linker script (see arch/i386/linker.ld).
** When the active toolchain emits constructors into the legacy `.ctors`
** section instead of `.init_array`, the section is empty and
** __init_array_start == __init_array_end, so the loop below is a bounded no-op.
*/
extern kernel_constructor_function_t __init_array_start[];
extern kernel_constructor_function_t __init_array_end[];
extern kernel_constructor_function_t __fini_array_start[];
extern kernel_constructor_function_t __fini_array_end[];

void kernel_run_global_constructors(void)
{
    for (kernel_constructor_function_t *entry = __init_array_start; entry != __init_array_end; ++entry)
        (*entry)();
}

void kernel_run_global_destructors(void)
{
    /* Destructors run in reverse registration order. */
    for (kernel_constructor_function_t *entry = __fini_array_end; entry != __fini_array_start;)
        (*--entry)();
}

/*
** Constructor self-test sentinel.
**
** This constructor is enrolled through whichever ABI the active toolchain uses
** (`.ctors` on gcc 10.2.0, `.init_array` after the upgrade), so a passing
** self-test exercises the live static-initialization path end to end.
*/
#define KERNEL_CONSTRUCTOR_SENTINEL_MAGIC 0xC0DE5EEDu

static volatile uint32_t kernel_constructor_sentinel = 0u;

__attribute__((constructor)) static void kernel_constructor_sentinel_register(void)
{
    kernel_constructor_sentinel = KERNEL_CONSTRUCTOR_SENTINEL_MAGIC;
}

uint8_t kernel_constructor_self_test_passed(void)
{
    return (uint8_t) (kernel_constructor_sentinel == KERNEL_CONSTRUCTOR_SENTINEL_MAGIC);
}
