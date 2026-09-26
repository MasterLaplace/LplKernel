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
 * @file exception_trigger.h
 * @brief Raising a CPU exception on purpose, for the exception smoke tests.
 *
 * Each routine executes the one instruction sequence that makes the processor
 * deliver its vector, so a test can prove the vector reaches its handler. None of
 * them returns in practice: the handlers they reach report and halt.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-09-26
 **************************************************************************/

#ifndef KERNEL_CPU_EXCEPTION_TRIGGER_H_
#define KERNEL_CPU_EXCEPTION_TRIGGER_H_

/**
 * @brief Raises #DB by single-stepping one instruction.
 *
 * @details Sets the trap flag and executes a NOP; the processor delivers the debug
 *          exception once the NOP completes.
 */
extern void exception_trigger_single_step(void);

/**
 * @brief Raises #BP with INT3.
 */
extern void exception_trigger_breakpoint(void);

/**
 * @brief Raises #UD with UD2, the instruction reserved to be invalid.
 */
extern void exception_trigger_invalid_opcode(void);

/**
 * @brief Raises #GP by reading memory through a null data segment.
 *
 * @details Loading the null selector into DS is allowed; using it is what faults.
 */
extern void exception_trigger_general_protection(void);

/**
 * @brief Delivers vector 8 with a software interrupt.
 *
 * @note INT n never pushes an error code, while the vector 8 stub expects the one a
 *       real double fault carries. The frame the handler decodes is therefore one word
 *       off: this proves the vector is wired, not that a double fault decodes.
 */
extern void exception_trigger_double_fault_vector(void);

#endif /* !KERNEL_CPU_EXCEPTION_TRIGGER_H_ */
