/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Kernel stack overflow guard. A canary word sits immediately below
** stack_bottom (the growth-facing end of the bootstrap stack, see
** arch/i386/boot/boot.S). A stack overflow writes past stack_bottom into this
** word before it reaches the adjacent .bss globals, so a clobbered canary is a
** strong signal that the fault was a stack overflow rather than a wild pointer.
** This exists because Octree::radixSort once put 512 KiB of histograms on a
** 16 KiB kernel stack, silently corrupting lpl::core::gActiveLogger.
*/

#ifndef KERNEL_CPU_STACK_GUARD_H
#define KERNEL_CPU_STACK_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_STACK_GUARD_MAGIC 0xB7ACE5A1u

/* Defined in arch/i386/boot/boot.S (.bootstrap_stack, just below stack_bottom). */
extern volatile uint32_t stack_guard;

/** @brief Writes the canary. Call once, early in kernel_main. */
static inline void kernel_stack_guard_arm(void) { stack_guard = KERNEL_STACK_GUARD_MAGIC; }

/** @brief True while the canary is untouched (no stack overflow detected). */
static inline bool kernel_stack_guard_intact(void) { return stack_guard == KERNEL_STACK_GUARD_MAGIC; }

#endif /* KERNEL_CPU_STACK_GUARD_H */
