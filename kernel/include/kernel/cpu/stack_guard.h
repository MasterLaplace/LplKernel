/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file stack_guard.h
 * @brief Kernel stack overflow guard.
 *
 * A canary word sits immediately below stack_bottom (the growth-facing end of the
 * bootstrap stack, see arch/i386/boot/boot.S). A stack overflow writes past
 * stack_bottom into this word before it reaches the adjacent .bss globals, so a
 * clobbered canary is a strong signal that the fault was a stack overflow rather
 * than a wild pointer.
 *
 * This exists because Octree::radixSort once put 512 KiB of histograms on a 16 KiB
 * kernel stack, silently corrupting lpl::core::gActiveLogger.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-07-17
 **************************************************************************/

#ifndef KERNEL_CPU_STACK_GUARD_H
#define KERNEL_CPU_STACK_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_STACK_GUARD_MAGIC 0xB7ACE5A1u

/** Defined in arch/i386/boot/boot.S (.bootstrap_stack, just below stack_bottom). */
extern volatile uint32_t stack_guard;

/**
 * @brief Writes the canary. Call once, early in kernel_main.
 *
 * Arm the stack-overflow canary before running any engine/smoke code, which
 * is where deep C++ call chains (software rasterizer, physics sort) live.
 */
static inline void kernel_stack_guard_arm(void) { stack_guard = KERNEL_STACK_GUARD_MAGIC; }

/**
 * @brief True while the canary is untouched (no stack overflow detected).
 *
 * @return True if the canary is intact, false if it has been clobbered.
 */
static inline bool kernel_stack_guard_intact(void) { return stack_guard == KERNEL_STACK_GUARD_MAGIC; }

#endif /* KERNEL_CPU_STACK_GUARD_H */
