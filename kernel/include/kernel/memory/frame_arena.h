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
 * @file frame_arena.h
 * @brief Pre-allocated frame arena allocator.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_FRAME_ARENA_H_
#define KERNEL_MEMORY_FRAME_ARENA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern bool kernel_frame_arena_initialize(uint32_t capacity_bytes);

extern void *kernel_frame_arena_alloc(uint32_t size, uint32_t align);

extern void kernel_frame_arena_reset(void);

extern bool kernel_frame_arena_is_initialized(void);

extern uint32_t kernel_frame_arena_get_capacity_bytes(void);

extern uint32_t kernel_frame_arena_get_used_bytes(void);

extern uint32_t kernel_frame_arena_get_peak_used_bytes(void);

extern uint32_t kernel_frame_arena_get_reset_count(void);

extern uint32_t kernel_frame_arena_get_failed_alloc_count(void);

extern void kernel_frame_arena_set_frame_budget(uint32_t budget_bytes);

extern uint32_t kernel_frame_arena_get_budget_exceeded_count(void);

extern uint32_t kernel_frame_arena_get_wcet_alloc_cycles(void);

extern uint32_t kernel_frame_arena_get_wcet_reset_cycles(void);

#endif /* !KERNEL_MEMORY_FRAME_ARENA_H_ */
