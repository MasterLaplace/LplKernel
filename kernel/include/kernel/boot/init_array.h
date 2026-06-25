/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** C++ global constructor / destructor runtime (.init_array ABI)
*/

#ifndef KERNEL_BOOT_INIT_ARRAY_H
#define KERNEL_BOOT_INIT_ARRAY_H

#include <stdint.h>

/*
** Two C++ static-initialization ABIs coexist in this tree:
**
**   - Legacy `.ctors` (emitted by i686-elf gcc 10.2.0): walked by `_init`
**     (crti prologue + crtbegin's __do_global_ctors_aux + crtn epilogue), which
**     boot.S calls directly. `.init_array` is empty in this case.
**
**   - Modern `.init_array` (emitted by newer gcc / clang, like the ARM port):
**     `_init` does NOT walk it, so the kernel must iterate it explicitly. This
**     is what keeps the boot path correct after the planned toolchain upgrade.
**
** Calling both `_init` (from boot.S) and kernel_run_global_constructors() is
** safe: exactly one of `.ctors` / `.init_array` is populated for any given
** compiler, so no constructor runs twice.
*/

/* Run every entry in `.init_array`, in registration order. No-op when the
   active toolchain emits constructors into the legacy `.ctors` section. */
void kernel_run_global_constructors(void);

/* Run every entry in `.fini_array`, in reverse registration order. */
void kernel_run_global_destructors(void);

/*
** Boot-time self-test: a sentinel constructor records a magic value when the
** static-initialization machinery fires. Returns 1 once that constructor has
** run (i.e. after `_init` and/or kernel_run_global_constructors()), 0 otherwise.
*/
uint8_t kernel_constructor_self_test_passed(void);

#endif /* !KERNEL_BOOT_INIT_ARRAY_H */
