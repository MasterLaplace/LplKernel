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
 * @file pmm_helper.h
 * @brief Serial reports of the physical memory manager.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-30
 **************************************************************************/

#ifndef KERNEL_MEMORY_PMM_HELPER_H
#define KERNEL_MEMORY_PMM_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_pmm_strategy_info(Serial_t *serial);
extern void write_pmm_pass_1_info(Serial_t *serial);
extern void write_pmm_pass_2_start_info(Serial_t *serial);
extern void write_pmm_ready_info(Serial_t *serial);
extern void write_pmm_buddy_info(Serial_t *serial);
extern void write_pmm_guard_info(Serial_t *serial);

#endif /* KERNEL_MEMORY_PMM_HELPER_H */
